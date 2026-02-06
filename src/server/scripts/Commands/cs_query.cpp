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

/* cs_query.cpp -- Structured JSON query commands for SOAP state inspection.
 *
 * All commands are Console::Yes so they work via SOAP without a player session.
 * Enable with QueryCommands.Enable = 1 in worldserver.conf.
 *
 * Commands:
 *   .query nearby <map> <x> <y> <z> [radius]   - creatures near a position
 *   .query creature <entry>                     - live creature instances by entry
 *   .query instance <mapId>                     - instance boss states
 *   .query auras <playerName>                   - active auras on a player
 *   .query server                               - server status
 */

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "GameTime.h"
#include "InstanceScript.h"
#include "Map.h"
#include "MapInstanced.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "StringConvert.h"
#include "UpdateTime.h"
#include "WorldSessionMgr.h"
#include <set>

using namespace Acore::ChatCommands;

// Escape a string for safe JSON embedding
static std::string JsonEscape(std::string const& str)
{
    std::string result;
    result.reserve(str.size() + 2);
    for (char c : str)
    {
        switch (c)
        {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c; break;
        }
    }
    return result;
}

// Format a float with one decimal place, trimming trailing zeros
static std::string JsonFloat(float val)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%.1f", val);
    return buf;
}

static bool IsQueryEnabled(ChatHandler* handler)
{
    if (!sConfigMgr->GetOption<bool>("QueryCommands.Enable", false))
    {
        handler->SendSysMessage("Query commands are disabled.");
        return false;
    }
    return true;
}

class query_commandscript : public CommandScript
{
public:
    query_commandscript() : CommandScript("query_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable queryCommandTable =
        {
            { "nearby",    HandleQueryNearbyCommand,    SEC_GAMEMASTER, Console::Yes },
            { "creature",  HandleQueryCreatureCommand,  SEC_GAMEMASTER, Console::Yes },
            { "instance",  HandleQueryInstanceCommand,  SEC_GAMEMASTER, Console::Yes },
            { "auras",     HandleQueryAurasCommand,     SEC_GAMEMASTER, Console::Yes },
            { "server",    HandleQueryServerCommand,    SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "query", queryCommandTable }
        };
        return commandTable;
    }

    // .query nearby <map> <x> <y> <z> [radius]
    static bool HandleQueryNearbyCommand(ChatHandler* handler, uint32 mapId, float x, float y, float z, Optional<float> radiusArg)
    {
        if (!IsQueryEnabled(handler))
            return true;

        float maxRadius = sConfigMgr->GetOption<float>("QueryCommands.MaxRadius", 200.0f);
        uint32 maxResults = sConfigMgr->GetOption<uint32>("QueryCommands.MaxResults", 50);
        float radius = radiusArg.value_or(50.0f);
        if (radius > maxRadius)
            radius = maxRadius;
        if (radius <= 0.0f)
            radius = 1.0f;

        float radiusSq = radius * radius;

        Map* map = sMapMgr->FindBaseNonInstanceMap(mapId);
        if (!map)
        {
            handler->SendSysMessage("{\"cmd\":\"nearby\",\"error\":\"Map " + std::to_string(mapId) + " not found or not loaded\"}");
            return true;
        }

        std::string json = "{\"cmd\":\"nearby\",\"map\":" + std::to_string(mapId)
            + ",\"pos\":[" + JsonFloat(x) + "," + JsonFloat(y) + "," + JsonFloat(z) + "]"
            + ",\"radius\":" + JsonFloat(radius)
            + ",\"results\":[";

        uint32 count = 0;
        auto& creatureStore = map->GetCreatureBySpawnIdStore();
        for (auto const& pair : creatureStore)
        {
            Creature* creature = pair.second;
            if (!creature)
                continue;

            float cx = creature->GetPositionX();
            float cy = creature->GetPositionY();
            float cz = creature->GetPositionZ();
            float dx = cx - x;
            float dy = cy - y;
            float dz = cz - z;
            float distSq = dx * dx + dy * dy + dz * dz;

            if (distSq > radiusSq)
                continue;

            if (count >= maxResults)
                break;

            if (count > 0)
                json += ",";

            json += "{\"entry\":" + std::to_string(creature->GetEntry())
                + ",\"name\":\"" + JsonEscape(creature->GetName()) + "\""
                + ",\"guid\":" + std::to_string(creature->GetSpawnId())
                + ",\"x\":" + JsonFloat(cx)
                + ",\"y\":" + JsonFloat(cy)
                + ",\"z\":" + JsonFloat(cz)
                + ",\"hp\":" + std::to_string(creature->GetHealth())
                + ",\"maxHp\":" + std::to_string(creature->GetMaxHealth())
                + ",\"alive\":" + (creature->IsAlive() ? "true" : "false")
                + ",\"combat\":" + (creature->IsInCombat() ? "true" : "false")
                + "}";
            ++count;
        }

        json += "],\"count\":" + std::to_string(count) + "}";
        handler->SendSysMessage(json);
        return true;
    }

    // .query creature <entry>
    static bool HandleQueryCreatureCommand(ChatHandler* handler, uint32 entry)
    {
        if (!IsQueryEnabled(handler))
            return true;

        uint32 maxResults = sConfigMgr->GetOption<uint32>("QueryCommands.MaxResults", 50);

        CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(entry);
        if (!cInfo)
        {
            handler->SendSysMessage("{\"cmd\":\"creature\",\"error\":\"Creature template " + std::to_string(entry) + " not found\"}");
            return true;
        }

        std::string json = "{\"cmd\":\"creature\",\"entry\":" + std::to_string(entry)
            + ",\"name\":\"" + JsonEscape(cInfo->Name) + "\""
            + ",\"results\":[";

        uint32 count = 0;
        bool limitReached = false;

        sMapMgr->DoForAllMaps([&](Map* map)
        {
            if (limitReached)
                return;

            auto& creatureStore = map->GetCreatureBySpawnIdStore();
            for (auto const& pair : creatureStore)
            {
                if (limitReached)
                    return;

                Creature* creature = pair.second;
                if (!creature || creature->GetEntry() != entry)
                    continue;

                if (count >= maxResults)
                {
                    limitReached = true;
                    return;
                }

                if (count > 0)
                    json += ",";

                json += "{\"guid\":" + std::to_string(creature->GetSpawnId())
                    + ",\"map\":" + std::to_string(map->GetId())
                    + ",\"instanceId\":" + std::to_string(map->GetInstanceId())
                    + ",\"x\":" + JsonFloat(creature->GetPositionX())
                    + ",\"y\":" + JsonFloat(creature->GetPositionY())
                    + ",\"z\":" + JsonFloat(creature->GetPositionZ())
                    + ",\"hp\":" + std::to_string(creature->GetHealth())
                    + ",\"maxHp\":" + std::to_string(creature->GetMaxHealth())
                    + ",\"alive\":" + (creature->IsAlive() ? "true" : "false")
                    + ",\"combat\":" + (creature->IsInCombat() ? "true" : "false")
                    + ",\"aiName\":\"" + JsonEscape(creature->GetAIName()) + "\""
                    + "}";
                ++count;
            }
        });

        json += "],\"count\":" + std::to_string(count) + "}";
        handler->SendSysMessage(json);
        return true;
    }

    // .query instance <mapId>
    static bool HandleQueryInstanceCommand(ChatHandler* handler, uint32 mapId)
    {
        if (!IsQueryEnabled(handler))
            return true;

        std::string json = "{\"cmd\":\"instance\",\"mapId\":" + std::to_string(mapId)
            + ",\"instances\":[";

        uint32 instanceCount = 0;

        sMapMgr->DoForAllMapsWithMapId(mapId, [&](Map* map)
        {
            InstanceMap* instanceMap = map->ToInstanceMap();
            if (!instanceMap)
                return;

            if (instanceCount > 0)
                json += ",";

            json += "{\"instanceId\":" + std::to_string(map->GetInstanceId())
                + ",\"mapName\":\"" + JsonEscape(map->GetMapName()) + "\""
                + ",\"players\":" + std::to_string(map->GetPlayersCountExceptGMs());

            InstanceScript* script = instanceMap->GetInstanceScript();
            if (script)
            {
                uint32 encounterCount = script->GetEncounterCount();
                json += ",\"encounterCount\":" + std::to_string(encounterCount)
                    + ",\"completedMask\":" + std::to_string(script->GetCompletedEncounterMask())
                    + ",\"bosses\":[";

                for (uint32 i = 0; i < encounterCount; ++i)
                {
                    if (i > 0)
                        json += ",";

                    EncounterState state = script->GetBossState(i);
                    json += "{\"id\":" + std::to_string(i)
                        + ",\"state\":\"" + InstanceScript::GetBossStateName(state) + "\""
                        + ",\"stateId\":" + std::to_string(static_cast<uint8>(state))
                        + "}";
                }
                json += "]";
            }
            else
            {
                json += ",\"script\":null";
            }

            json += "}";
            ++instanceCount;
        });

        // If no instances found, also try the base map for non-instanced dungeon maps
        if (instanceCount == 0)
        {
            Map* baseMap = sMapMgr->FindBaseNonInstanceMap(mapId);
            if (baseMap)
            {
                json += "{\"instanceId\":0"
                    ",\"mapName\":\"" + JsonEscape(baseMap->GetMapName()) + "\""
                    + ",\"players\":" + std::to_string(baseMap->GetPlayersCountExceptGMs())
                    + ",\"type\":\"continent\"}";
                ++instanceCount;
            }
        }

        json += "],\"count\":" + std::to_string(instanceCount) + "}";
        handler->SendSysMessage(json);
        return true;
    }

    // .query auras <playerName>
    static bool HandleQueryAurasCommand(ChatHandler* handler, std::string playerName)
    {
        if (!IsQueryEnabled(handler))
            return true;

        Player* player = ObjectAccessor::FindPlayerByName(playerName);
        if (!player)
        {
            handler->SendSysMessage("{\"cmd\":\"auras\",\"error\":\"Player '" + JsonEscape(playerName) + "' not found or not online\"}");
            return true;
        }

        std::string json = "{\"cmd\":\"auras\",\"player\":\"" + JsonEscape(player->GetName()) + "\""
            + ",\"results\":[";

        Unit::AuraApplicationMap const& auras = player->GetAppliedAuras();
        uint32 count = 0;

        // Track which aura base pointers we have already emitted to avoid
        // duplicating multi-effect auras (the map is keyed by spell+effect).
        std::set<Aura const*> seen;

        for (auto const& pair : auras)
        {
            AuraApplication* aurApp = pair.second;
            Aura const* aura = aurApp->GetBase();
            if (!seen.insert(aura).second)
                continue;

            SpellInfo const* spellInfo = aura->GetSpellInfo();
            if (!spellInfo)
                continue;

            if (count > 0)
                json += ",";

            // Spell name -- use enUS (index 0)
            std::string spellName = spellInfo->SpellName[0] ? spellInfo->SpellName[0] : "";

            // Caster info
            ObjectGuid casterGuid = aura->GetCasterGUID();
            std::string casterType = casterGuid.IsPlayer() ? "player" : "creature";

            json += "{\"spellId\":" + std::to_string(aura->GetId())
                + ",\"name\":\"" + JsonEscape(spellName) + "\""
                + ",\"duration\":" + std::to_string(aura->GetDuration())
                + ",\"maxDuration\":" + std::to_string(aura->GetMaxDuration())
                + ",\"stacks\":" + std::to_string(aura->GetStackAmount())
                + ",\"charges\":" + std::to_string(aura->GetCharges())
                + ",\"casterType\":\"" + casterType + "\""
                + ",\"casterGuid\":\"" + casterGuid.ToString() + "\""
                + "}";
            ++count;
        }

        json += "],\"count\":" + std::to_string(count) + "}";
        handler->SendSysMessage(json);
        return true;
    }

    // .query server
    static bool HandleQueryServerCommand(ChatHandler* handler)
    {
        if (!IsQueryEnabled(handler))
            return true;

        uint32 uptime = GameTime::GetUptime().count();
        uint32 activeSessions = sWorldSessionMgr->GetActiveSessionCount();
        uint32 playerCount = sWorldSessionMgr->GetPlayerCount();
        uint32 queuedSessions = sWorldSessionMgr->GetQueuedSessionCount();
        uint32 maxSessions = sWorldSessionMgr->GetMaxActiveSessionCount();
        uint32 updateDiff = sWorldUpdateTime.GetLastUpdateTime();
        uint32 avgDiff = sWorldUpdateTime.GetAverageUpdateTime();

        // Count active maps
        uint32 mapCount = 0;
        sMapMgr->DoForAllMaps([&](Map* /*map*/)
        {
            ++mapCount;
        });

        std::string json = "{\"cmd\":\"server\""
            ",\"uptime\":" + std::to_string(uptime)
            + ",\"activeSessions\":" + std::to_string(activeSessions)
            + ",\"players\":" + std::to_string(playerCount)
            + ",\"queuedSessions\":" + std::to_string(queuedSessions)
            + ",\"maxSessions\":" + std::to_string(maxSessions)
            + ",\"updateDiff\":" + std::to_string(updateDiff)
            + ",\"avgDiff\":" + std::to_string(avgDiff)
            + ",\"activeMaps\":" + std::to_string(mapCount)
            + "}";

        handler->SendSysMessage(json);
        return true;
    }
};

void AddSC_query_commandscript()
{
    new query_commandscript();
}
