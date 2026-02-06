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
#include "DBCStores.h"
#include "Log.h"

using namespace Acore::ChatCommands;

class reload_dbc_commandscript : public CommandScript
{
public:
    reload_dbc_commandscript() : CommandScript("reload_dbc_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable reloadDbcCommandTable =
        {
            { "list", HandleReloadDbcListCommand, SEC_ADMINISTRATOR, Console::Yes },
            { "",     HandleReloadDbcCommand,     SEC_ADMINISTRATOR, Console::Yes },
        };
        static ChatCommandTable reloadCommandTable =
        {
            { "dbc", reloadDbcCommandTable },
        };
        static ChatCommandTable commandTable =
        {
            { "reload", reloadCommandTable },
        };
        return commandTable;
    }

    static bool HandleReloadDbcCommand(ChatHandler* handler, Optional<std::string> storeName)
    {
        if (!sConfigMgr->GetOption<bool>("HotReload.DbcReload.Enable", false))
        {
            handler->PSendSysMessage("DBC reload is disabled. Set HotReload.DbcReload.Enable = 1 in worldserver.conf");
            return true;
        }

        if (!storeName || storeName->empty())
        {
            handler->PSendSysMessage("Usage: .reload dbc <StoreName>");
            handler->PSendSysMessage("Use '.reload dbc list' to see available stores.");
            return true;
        }

        std::string const& name = *storeName;

        // DBC stores are loaded from binary files at server startup into global memory.
        // Most stores have active pointers held by game objects (spells, auras, items, etc.)
        // and cannot be safely freed or reloaded at runtime without risking use-after-free crashes.
        //
        // For database-backed overrides (spell_dbc, item_template, creature_template, etc.),
        // use the existing .reload commands which safely re-read from MySQL.

        if (name == "Spell" || name == "spell")
        {
            handler->PSendSysMessage("Spell DBC cannot be reloaded at runtime (active pointer references from SpellInfo, Auras, etc.).");
            handler->PSendSysMessage("For spell_dbc table changes, use: .reload all spell");
            return true;
        }
        else if (name == "Item" || name == "item")
        {
            handler->PSendSysMessage("Item DBC cannot be reloaded at runtime.");
            handler->PSendSysMessage("For item_template changes, use: .reload all item");
            return true;
        }
        else if (name == "CreatureFamily" || name == "creature_family")
        {
            handler->PSendSysMessage("CreatureFamily DBC cannot be safely reloaded at runtime (referenced by active pets).");
            handler->PSendSysMessage("Server restart required for CreatureFamily DBC changes.");
            return true;
        }
        else if (name == "AreaTable" || name == "area_table")
        {
            handler->PSendSysMessage("AreaTable DBC cannot be reloaded at runtime (referenced by active zones and players).");
            handler->PSendSysMessage("Server restart required for AreaTable DBC changes.");
            return true;
        }
        else if (name == "Map" || name == "map")
        {
            handler->PSendSysMessage("Map DBC cannot be reloaded at runtime (referenced by active map instances).");
            handler->PSendSysMessage("Server restart required for Map DBC changes.");
            return true;
        }
        else if (name == "Talent" || name == "talent")
        {
            handler->PSendSysMessage("Talent DBC cannot be reloaded at runtime (referenced by active player talent data).");
            handler->PSendSysMessage("Server restart required for Talent DBC changes.");
            return true;
        }
        else
        {
            handler->PSendSysMessage("Unknown or unsupported DBC store: %s", name.c_str());
            handler->PSendSysMessage("Use '.reload dbc list' to see available information.");
            handler->PSendSysMessage("Note: Most DBC stores require server restart. For DB-backed data, use .reload commands.");
            return true;
        }
    }

    static bool HandleReloadDbcListCommand(ChatHandler* handler)
    {
        handler->PSendSysMessage("=== DBC Store Reload Status ===");
        handler->PSendSysMessage("Most DBC stores are loaded from binary files at startup and cannot be safely");
        handler->PSendSysMessage("reloaded at runtime due to active pointer references held by game objects.");
        handler->PSendSysMessage("");
        handler->PSendSysMessage("For database-backed changes, use these existing reload commands:");
        handler->PSendSysMessage("  .reload all spell       - Reload spell data from DB");
        handler->PSendSysMessage("  .reload all item        - Reload item data from DB");
        handler->PSendSysMessage("  .reload all quest       - Reload quest data from DB");
        handler->PSendSysMessage("  .reload all npc         - Reload NPC data from DB");
        handler->PSendSysMessage("  .reload all loot        - Reload loot tables from DB");
        handler->PSendSysMessage("  .reload all gossips     - Reload gossip menus from DB");
        handler->PSendSysMessage("  .reload smart_scripts   - Reload SmartAI scripts from DB");
        handler->PSendSysMessage("  .reload conditions      - Reload condition system from DB");
        handler->PSendSysMessage("  .reload creature_text   - Reload creature text from DB");
        handler->PSendSysMessage("  .reload broadcast_text  - Reload broadcast text from DB");
        handler->PSendSysMessage("  .reload waypoint_data   - Reload waypoint data from DB");
        handler->PSendSysMessage("  .reload trainer         - Reload trainer data from DB");
        handler->PSendSysMessage("  .reload npc_vendor      - Reload vendor data from DB");
        handler->PSendSysMessage("");
        handler->PSendSysMessage("Binary DBC stores (require server restart):");
        handler->PSendSysMessage("  Spell, Item, Map, AreaTable, Talent, CreatureFamily, SkillLine, etc.");
        return true;
    }
};

void AddSC_reload_dbc_commandscript()
{
    new reload_dbc_commandscript();
}
