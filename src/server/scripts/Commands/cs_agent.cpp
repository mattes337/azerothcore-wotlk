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

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "Creature.h"
#include "DBCStores.h"
#include "Log.h"
#include "Player.h"
#include <ctime>
#include <fstream>
#include <mutex>

using namespace Acore::ChatCommands;

// Static mutex for thread-safe file writes
static std::mutex sAgentTaskMutex;

class agent_commandscript : public CommandScript
{
public:
    agent_commandscript() : CommandScript("agent_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable agentCommandTable =
        {
            { "bug",    HandleAgentBugCommand,    SEC_GAMEMASTER, Console::No },
            { "todo",   HandleAgentTodoCommand,   SEC_GAMEMASTER, Console::No },
            { "note",   HandleAgentNoteCommand,   SEC_GAMEMASTER, Console::No },
            { "list",   HandleAgentListCommand,   SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "agent", agentCommandTable },
        };
        return commandTable;
    }

private:
    enum TaskType { TASK_BUG, TASK_TODO, TASK_NOTE };

    static std::string JsonEscape(std::string const& str)
    {
        std::string result;
        result.reserve(str.size());
        for (char c : str)
        {
            switch (c)
            {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                default:   result += c;      break;
            }
        }
        return result;
    }

    static bool HandleAgentEntry(ChatHandler* handler, TaskType type, Tail description)
    {
        if (!sConfigMgr->GetOption<bool>("AgentTasks.Enable", false))
        {
            handler->SendSysMessage("Agent task system is disabled. Set AgentTasks.Enable = 1 in worldserver.conf");
            return true;
        }

        if (description.empty())
        {
            char const* typeStr = type == TASK_BUG ? "bug" : type == TASK_TODO ? "todo" : "note";
            handler->PSendSysMessage("Usage: .agent {} <description>", typeStr);
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
            return false;

        // Capture positional context
        uint32 mapId = player->GetMapId();
        uint32 zoneId = player->GetZoneId();
        uint32 areaId = player->GetAreaId();
        float x = player->GetPositionX();
        float y = player->GetPositionY();
        float z = player->GetPositionZ();

        // Get map name from DBC
        std::string mapName = "Unknown";
        MapEntry const* mapEntry = sMapStore.LookupEntry(mapId);
        if (mapEntry)
            mapName = mapEntry->name[DEFAULT_LOCALE];

        // Get zone name from DBC
        std::string zoneName = "Unknown";
        AreaTableEntry const* zoneEntry = sAreaTableStore.LookupEntry(zoneId);
        if (zoneEntry)
            zoneName = zoneEntry->area_name[DEFAULT_LOCALE];

        // Get target info if a unit is selected
        std::string targetInfo;
        if (Unit* target = player->GetSelectedUnit())
        {
            if (Creature* creature = target->ToCreature())
            {
                targetInfo = Acore::StringFormat("creature_template {} ({}) GUID {}",
                    creature->GetEntry(), creature->GetName(), creature->GetSpawnId());
            }
            else if (Player* targetPlayer = target->ToPlayer())
            {
                targetInfo = Acore::StringFormat("player {} (level {})",
                    targetPlayer->GetName(), targetPlayer->GetLevel());
            }
        }

        // Format timestamp
        std::time_t now = std::time(nullptr);
        char timeBuf[64];
        std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M", std::localtime(&now));

        // Determine type label
        char const* typeLabel;
        switch (type)
        {
            case TASK_BUG:  typeLabel = "BUG";  break;
            case TASK_TODO: typeLabel = "TODO"; break;
            case TASK_NOTE: typeLabel = "NOTE"; break;
            default:        typeLabel = "NOTE"; break;
        }

        std::string format = sConfigMgr->GetOption<std::string>("AgentTasks.Format", "markdown");
        std::string descStr(description);
        std::string entry;

        if (format == "jsonl")
        {
            entry = Acore::StringFormat(
                "{{\"type\":\"{}\",\"time\":\"{}\",\"desc\":\"{}\",\"map\":{},\"mapName\":\"{}\","
                "\"zone\":{},\"zoneName\":\"{}\",\"area\":{},\"pos\":[{:.1f},{:.1f},{:.1f}]",
                typeLabel, timeBuf, JsonEscape(descStr), mapId, JsonEscape(mapName),
                zoneId, JsonEscape(zoneName), areaId, x, y, z);
            if (!targetInfo.empty())
                entry += Acore::StringFormat(",\"target\":\"{}\"", JsonEscape(targetInfo));
            entry += "}";
        }
        else
        {
            // Markdown format (default)
            entry = Acore::StringFormat(
                "- [ ] **{}** [{}] {}\n"
                "  Map: {} ({}) | Zone: {} ({}) | Pos: {:.1f}, {:.1f}, {:.1f}",
                typeLabel, timeBuf, descStr,
                mapId, mapName, zoneName, zoneId, x, y, z);
            if (!targetInfo.empty())
                entry += Acore::StringFormat("\n  Target: {}", targetInfo);
        }

        // Write to file (thread-safe)
        std::string filePath = sConfigMgr->GetOption<std::string>("AgentTasks.OutputFile", "agent-tasks.md");

        {
            std::lock_guard<std::mutex> lock(sAgentTaskMutex);
            std::ofstream outFile(filePath, std::ios::app);
            if (!outFile.is_open())
            {
                handler->PSendSysMessage("Failed to open agent tasks file: {}", filePath);
                LOG_ERROR("server", "AgentTasks: Failed to open file: {}", filePath);
                return true;
            }
            outFile << entry << "\n";
            if (format != "jsonl")
                outFile << "\n"; // Extra blank line between markdown entries
        }

        handler->PSendSysMessage("Agent {} recorded: {}", typeLabel, descStr);
        LOG_INFO("server", "AgentTasks: {} recorded by {} - {}", typeLabel, player->GetName(), descStr);
        return true;
    }

public:
    static bool HandleAgentBugCommand(ChatHandler* handler, Tail description)
    {
        return HandleAgentEntry(handler, TASK_BUG, description);
    }

    static bool HandleAgentTodoCommand(ChatHandler* handler, Tail description)
    {
        return HandleAgentEntry(handler, TASK_TODO, description);
    }

    static bool HandleAgentNoteCommand(ChatHandler* handler, Tail description)
    {
        return HandleAgentEntry(handler, TASK_NOTE, description);
    }

    static bool HandleAgentListCommand(ChatHandler* handler)
    {
        std::string filePath = sConfigMgr->GetOption<std::string>("AgentTasks.OutputFile", "agent-tasks.md");
        handler->PSendSysMessage("Agent tasks file: {}", filePath);
        handler->PSendSysMessage("AgentTasks enabled: {}",
            sConfigMgr->GetOption<bool>("AgentTasks.Enable", false) ? "yes" : "no");
        return true;
    }
};

void AddSC_agent_commandscript()
{
    new agent_commandscript();
}
