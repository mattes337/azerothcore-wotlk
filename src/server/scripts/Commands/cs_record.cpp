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
#include "Custom/EventRecorder/EventRecorder.h"
#include "Player.h"
#include "StringConvert.h"
#include <sstream>

using namespace Acore::ChatCommands;

class record_commandscript : public CommandScript
{
public:
    record_commandscript() : CommandScript("record_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable recordCommandTable =
        {
            { "start",  HandleRecordStartCommand,  SEC_GAMEMASTER, Console::No },
            { "stop",   HandleRecordStopCommand,    SEC_GAMEMASTER, Console::No },
            { "status", HandleRecordStatusCommand,  SEC_GAMEMASTER, Console::Yes },
        };
        static ChatCommandTable commandTable =
        {
            { "record", recordCommandTable }
        };
        return commandTable;
    }

    static bool HandleRecordStartCommand(ChatHandler* handler, Tail args)
    {
        if (!sEventRecorder->IsEnabled())
        {
            handler->PSendSysMessage("Event Recorder is disabled. Set EventRecorder.Enable = 1 in worldserver.conf.");
            return true;
        }

        if (sEventRecorder->IsActive())
        {
            handler->PSendSysMessage("A recording session is already active. Stop it first with .record stop");
            return true;
        }

        std::string argsStr(args);
        if (argsStr.empty())
        {
            handler->PSendSysMessage("Usage: .record start <session-name> [--map <mapId>] [--radius <float>]");
            return true;
        }

        // Parse session name (first token)
        std::string sessionName;
        uint32 mapFilter = 0;
        float radius = 0.0f;

        // Tokenize args
        std::istringstream stream(argsStr);
        std::string token;

        // First token is the session name
        stream >> sessionName;

        // Parse optional flags
        while (stream >> token)
        {
            if (token == "--map")
            {
                std::string mapStr;
                if (stream >> mapStr)
                {
                    Optional<uint32> mapVal = Acore::StringTo<uint32>(mapStr);
                    if (mapVal)
                        mapFilter = *mapVal;
                    else
                    {
                        handler->PSendSysMessage("Invalid map ID: {}", mapStr);
                        return true;
                    }
                }
            }
            else if (token == "--radius")
            {
                std::string radiusStr;
                if (stream >> radiusStr)
                {
                    Optional<float> radiusVal = Acore::StringTo<float>(radiusStr);
                    if (radiusVal)
                        radius = *radiusVal;
                    else
                    {
                        handler->PSendSysMessage("Invalid radius: {}", radiusStr);
                        return true;
                    }
                }
            }
        }

        Player* player = handler->GetSession()->GetPlayer();

        if (sEventRecorder->StartSession(sessionName, player, mapFilter, radius))
        {
            handler->PSendSysMessage("Recording started: '{}'", sessionName);
            handler->PSendSysMessage("{}", sEventRecorder->GetSessionInfo());
        }
        else
        {
            handler->PSendSysMessage("Failed to start recording session.");
        }

        return true;
    }

    static bool HandleRecordStopCommand(ChatHandler* handler)
    {
        if (!sEventRecorder->IsActive())
        {
            handler->PSendSysMessage("No active recording session.");
            return true;
        }

        std::string info = sEventRecorder->GetSessionInfo();

        if (sEventRecorder->StopSession())
        {
            handler->PSendSysMessage("Recording stopped.");
            handler->PSendSysMessage("{}", info);
        }
        else
        {
            handler->PSendSysMessage("Failed to stop recording session.");
        }

        return true;
    }

    static bool HandleRecordStatusCommand(ChatHandler* handler)
    {
        if (!sEventRecorder->IsEnabled())
        {
            handler->PSendSysMessage("Event Recorder is disabled.");
            return true;
        }

        handler->PSendSysMessage("{}", sEventRecorder->GetSessionInfo());
        return true;
    }
};

void AddSC_record_commandscript()
{
    new record_commandscript();
}
