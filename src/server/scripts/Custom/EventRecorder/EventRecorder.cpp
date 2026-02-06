/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "EventRecorder.h"
#include "Config.h"
#include "Creature.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "StringFormat.h"
#include <chrono>
#include <filesystem>

EventRecorder::EventRecorder() = default;
EventRecorder::~EventRecorder()
{
    if (_outFile.is_open())
        _outFile.close();
}

EventRecorder* EventRecorder::instance()
{
    static EventRecorder inst;
    return &inst;
}

void EventRecorder::LoadConfig()
{
    _enabled = sConfigMgr->GetOption<bool>("EventRecorder.Enable", false);
    _outputDir = sConfigMgr->GetOption<std::string>("EventRecorder.OutputDir", "recordings");
    _maxEvents = sConfigMgr->GetOption<uint32>("EventRecorder.MaxEvents", 100000);
    float defaultRadius = sConfigMgr->GetOption<float>("EventRecorder.DefaultRadius", 0.0f);

    if (defaultRadius > 0.0f)
        _radius = defaultRadius;

    if (_enabled)
        LOG_INFO("server", "EventRecorder: Enabled (output: {}, max events: {})", _outputDir, _maxEvents);
}

bool EventRecorder::IsEnabled() const
{
    return _enabled;
}

bool EventRecorder::IsActive() const
{
    return _active;
}

bool EventRecorder::IsActiveForMap(uint32 mapId, uint32 instanceId) const
{
    if (!_active)
        return false;

    if (_mapFilter != 0 && _mapFilter != mapId)
        return false;

    if (_instanceFilter != 0 && _instanceFilter != instanceId)
        return false;

    return true;
}

bool EventRecorder::StartSession(std::string const& sessionName, Player* player, uint32 mapFilter, float radius)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_active)
        return false;

    if (!_enabled)
        return false;

    // Create output directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(_outputDir, ec);
    if (ec)
    {
        LOG_ERROR("server", "EventRecorder: Failed to create output directory '{}': {}", _outputDir, ec.message());
        return false;
    }

    // Build filename with timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &time);
#else
    localtime_r(&time, &tm_buf);
#endif
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y%m%d_%H%M%S", &tm_buf);

    std::string filename = Acore::StringFormat("{}/{}_{}.jsonl", _outputDir, sessionName, timeBuf);

    _outFile.open(filename, std::ios::out | std::ios::trunc);
    if (!_outFile.is_open())
    {
        LOG_ERROR("server", "EventRecorder: Failed to open output file '{}'", filename);
        return false;
    }

    _sessionName = sessionName;
    _eventCount = 0;
    _mapFilter = mapFilter;

    if (player)
    {
        _recorderX = player->GetPositionX();
        _recorderY = player->GetPositionY();
        _recorderZ = player->GetPositionZ();
        _recorderMapId = player->GetMapId();
        _instanceFilter = player->GetInstanceId();

        if (mapFilter == 0)
            _mapFilter = player->GetMapId();
    }

    if (radius > 0.0f)
        _radius = radius;
    else if (_radius <= 0.0f)
        _radius = sConfigMgr->GetOption<float>("EventRecorder.DefaultRadius", 0.0f);

    _sessionStart = std::chrono::steady_clock::now();
    _active = true;

    // Write start event
    std::string startEvent = Acore::StringFormat(
        "{{\"t\":0.000,\"event\":\"record_start\",\"session\":\"{}\",\"map\":{},\"file\":\"{}\"}}",
        _sessionName, _mapFilter, filename);
    WriteEvent(startEvent);

    LOG_INFO("server", "EventRecorder: Started session '{}' (map: {}, radius: {:.1f}, file: {})",
        _sessionName, _mapFilter, _radius, filename);

    return true;
}

bool EventRecorder::StopSession()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!_active)
        return false;

    double duration = GetSessionTime();

    // Write stop event
    std::string stopEvent = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"record_stop\",\"session\":\"{}\",\"duration\":{:.3f},\"events_captured\":{}}}",
        duration, _sessionName, duration, _eventCount);
    WriteEvent(stopEvent);

    _outFile.close();
    _active = false;

    LOG_INFO("server", "EventRecorder: Stopped session '{}' ({:.1f}s, {} events)",
        _sessionName, duration, _eventCount);

    // Reset filters
    _mapFilter = 0;
    _instanceFilter = 0;
    _radius = 0.0f;
    _recorderX = 0.0f;
    _recorderY = 0.0f;
    _recorderZ = 0.0f;
    _recorderMapId = 0;

    return true;
}

std::string EventRecorder::GetSessionInfo() const
{
    if (!_active)
        return "No active recording session.";

    double elapsed = GetSessionTime();
    return Acore::StringFormat(
        "Session: '{}' | Map: {} | Events: {} / {} | Elapsed: {:.1f}s | Radius: {:.1f}",
        _sessionName, _mapFilter, _eventCount, _maxEvents, elapsed, _radius);
}

void EventRecorder::RecordEnterCombat(Unit* unit, Unit* victim)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!PassesFilters(unit))
        return;

    std::string json = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"enter_combat\",\"source\":{},\"target\":{}}}",
        GetSessionTime(), FormatUnit(unit), FormatUnit(victim));
    WriteEvent(json);
}

void EventRecorder::RecordLeaveCombat(Creature* creature)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!PassesFilters(creature))
        return;

    std::string json = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"leave_combat\",\"source\":{}}}",
        GetSessionTime(), FormatUnit(creature));
    WriteEvent(json);
}

void EventRecorder::RecordEvade(Unit* unit, uint8 evadeReason)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!PassesFilters(unit))
        return;

    std::string json = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"evade\",\"source\":{},\"reason\":{}}}",
        GetSessionTime(), FormatUnit(unit), evadeReason);
    WriteEvent(json);
}

void EventRecorder::RecordUnitDeath(Unit* unit, Unit* killer)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!PassesFilters(unit) && !PassesFilters(killer))
        return;

    std::string json = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"death\",\"source\":{},\"killer\":{}}}",
        GetSessionTime(), FormatUnit(unit), FormatUnit(killer));
    WriteEvent(json);
}

void EventRecorder::RecordDamage(Unit* attacker, Unit* victim, uint32 damage)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!PassesFilters(attacker) && !PassesFilters(victim))
        return;

    std::string json = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"damage\",\"source\":{},\"target\":{},\"amount\":{}}}",
        GetSessionTime(), FormatUnit(attacker), FormatUnit(victim), damage);
    WriteEvent(json);
}

void EventRecorder::RecordHeal(Unit* healer, Unit* target, uint32 gain)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!PassesFilters(healer) && !PassesFilters(target))
        return;

    std::string json = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"heal\",\"source\":{},\"target\":{},\"amount\":{}}}",
        GetSessionTime(), FormatUnit(healer), FormatUnit(target), gain);
    WriteEvent(json);
}

void EventRecorder::RecordAuraApply(Unit* unit, Aura* aura)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!PassesFilters(unit))
        return;

    if (!aura)
        return;

    SpellInfo const* spellInfo = aura->GetSpellInfo();
    std::string spellName = spellInfo && spellInfo->SpellName[0] ? EscapeJson(spellInfo->SpellName[0]) : "Unknown";
    uint32 spellId = spellInfo ? spellInfo->Id : 0;

    std::string json = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"aura_apply\",\"target\":{},\"spell_id\":{},\"spell_name\":\"{}\"}}",
        GetSessionTime(), FormatUnit(unit), spellId, spellName);
    WriteEvent(json);
}

void EventRecorder::RecordAuraRemove(Unit* unit, AuraApplication* aurApp, uint8 removeMode)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!PassesFilters(unit))
        return;

    if (!aurApp || !aurApp->GetBase())
        return;

    Aura* aura = aurApp->GetBase();
    SpellInfo const* spellInfo = aura->GetSpellInfo();
    std::string spellName = spellInfo && spellInfo->SpellName[0] ? EscapeJson(spellInfo->SpellName[0]) : "Unknown";
    uint32 spellId = spellInfo ? spellInfo->Id : 0;

    std::string json = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"aura_remove\",\"target\":{},\"spell_id\":{},\"spell_name\":\"{}\",\"remove_mode\":{}}}",
        GetSessionTime(), FormatUnit(unit), spellId, spellName, removeMode);
    WriteEvent(json);
}

void EventRecorder::RecordSpellCast(Player* player, SpellInfo const* spellInfo)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!PassesFilters(player))
        return;

    if (!spellInfo)
        return;

    std::string spellName = spellInfo->SpellName[0] ? EscapeJson(spellInfo->SpellName[0]) : "Unknown";

    std::string json = Acore::StringFormat(
        "{{\"t\":{:.3f},\"event\":\"spell_cast\",\"source\":{},\"spell_id\":{},\"spell_name\":\"{}\"}}",
        GetSessionTime(), FormatUnit(player), spellInfo->Id, spellName);
    WriteEvent(json);
}

void EventRecorder::WriteEvent(std::string const& jsonLine)
{
    // Caller must hold _mutex
    if (!_outFile.is_open())
        return;

    if (_eventCount >= _maxEvents)
    {
        if (_eventCount == _maxEvents)
        {
            LOG_ERROR("server", "EventRecorder: Max event limit ({}) reached for session '{}'", _maxEvents, _sessionName);
            _eventCount++; // Increment past limit so we only log once
        }
        return;
    }

    _outFile << jsonLine << "\n";
    _outFile.flush();
    _eventCount++;
}

std::string EventRecorder::FormatUnit(Unit const* unit) const
{
    if (!unit)
        return "null";

    std::string name = EscapeJson(unit->GetName());
    std::string guid = unit->GetGUID().ToString();

    if (unit->IsCreature())
    {
        return Acore::StringFormat(
            "{{\"type\":\"creature\",\"entry\":{},\"name\":\"{}\",\"guid\":\"{}\"}}",
            unit->GetEntry(), name, guid);
    }
    else if (unit->IsPlayer())
    {
        return Acore::StringFormat(
            "{{\"type\":\"player\",\"name\":\"{}\",\"guid\":\"{}\"}}",
            name, guid);
    }

    return Acore::StringFormat(
        "{{\"type\":\"unit\",\"entry\":{},\"name\":\"{}\",\"guid\":\"{}\"}}",
        unit->GetEntry(), name, guid);
}

std::string EventRecorder::EscapeJson(std::string const& input) const
{
    std::string output;
    output.reserve(input.size());
    for (char c : input)
    {
        switch (c)
        {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:   output += c; break;
        }
    }
    return output;
}

double EventRecorder::GetSessionTime() const
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _sessionStart);
    return elapsed.count() / 1000.0;
}

bool EventRecorder::PassesFilters(Unit const* unit) const
{
    if (!unit)
        return false;

    // Map filter
    if (_mapFilter != 0 && unit->GetMapId() != _mapFilter)
        return false;

    // Radius filter (only when radius > 0 and recorder position was set)
    if (_radius > 0.0f && unit->GetMapId() == _recorderMapId)
    {
        float dx = unit->GetPositionX() - _recorderX;
        float dy = unit->GetPositionY() - _recorderY;
        float dz = unit->GetPositionZ() - _recorderZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > _radius * _radius)
            return false;
    }

    return true;
}
