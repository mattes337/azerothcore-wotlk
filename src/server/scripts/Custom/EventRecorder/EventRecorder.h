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

#ifndef EVENT_RECORDER_H
#define EVENT_RECORDER_H

#include "Define.h"
#include "Duration.h"
#include <string>
#include <fstream>
#include <mutex>

class Creature;
class Player;
class Unit;
class Aura;
class AuraApplication;
class SpellInfo;

class EventRecorder
{
public:
    static EventRecorder* instance();

    bool IsEnabled() const;
    bool IsActive() const;
    bool IsActiveForMap(uint32 mapId, uint32 instanceId) const;

    // Session management
    bool StartSession(std::string const& sessionName, Player* player, uint32 mapFilter = 0, float radius = 0.0f);
    bool StopSession();
    std::string GetSessionInfo() const;

    // Event recording - only call these if IsActive() returns true
    void RecordEnterCombat(Unit* unit, Unit* victim);
    void RecordLeaveCombat(Creature* creature);
    void RecordEvade(Unit* unit, uint8 evadeReason);
    void RecordUnitDeath(Unit* unit, Unit* killer);
    void RecordDamage(Unit* attacker, Unit* victim, uint32 damage);
    void RecordHeal(Unit* healer, Unit* target, uint32 gain);
    void RecordAuraApply(Unit* unit, Aura* aura);
    void RecordAuraRemove(Unit* unit, AuraApplication* aurApp, uint8 removeMode);
    void RecordSpellCast(Player* player, SpellInfo const* spellInfo);

    // Load config
    void LoadConfig();

private:
    EventRecorder();
    ~EventRecorder();

    void WriteEvent(std::string const& jsonLine);
    std::string FormatUnit(Unit const* unit) const;
    std::string EscapeJson(std::string const& input) const;
    double GetSessionTime() const;
    bool PassesFilters(Unit const* unit) const;

    bool _enabled = false;
    bool _active = false;
    std::string _sessionName;
    std::string _outputDir;
    uint32 _maxEvents = 100000;
    uint32 _eventCount = 0;
    uint32 _mapFilter = 0;
    uint32 _instanceFilter = 0;
    float _radius = 0.0f;
    float _recorderX = 0.0f;
    float _recorderY = 0.0f;
    float _recorderZ = 0.0f;
    uint32 _recorderMapId = 0;
    TimePoint _sessionStart;
    std::ofstream _outFile;
    std::mutex _mutex;
};

#define sEventRecorder EventRecorder::instance()

#endif
