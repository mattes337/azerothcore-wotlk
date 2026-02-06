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

#include "AppenderJsonl.h"
#include "Log.h"
#include "LogMessage.h"
#include "StringConvert.h"
#include "Timer.h"
#include <algorithm>
#include <cctype>
#include <cstring>

AppenderJsonl::AppenderJsonl(uint8 id, std::string const& name, LogLevel level, AppenderFlags flags, std::vector<std::string_view> const& args) :
    Appender(id, name, level, flags),
    logfile(nullptr),
    _logDir(sLog->GetLogsDir()),
    _maxFileSize(0),
    _fileSize(0)
{
    if (args.size() < 4)
    {
        throw InvalidAppenderArgsException(Acore::StringFormat("Log::CreateAppenderFromConfig: Missing file name for appender {}", name));
    }

    _fileName.assign(args[3]);

    std::string mode = "a";
    if (4 < args.size())
    {
        mode.assign(args[4]);
    }

    if (flags & APPENDER_FLAGS_USE_TIMESTAMP)
    {
        std::size_t dot_pos = _fileName.find_last_of('.');
        if (dot_pos != std::string::npos)
        {
            _fileName.insert(dot_pos, sLog->GetLogsTimestamp());
        }
        else
        {
            _fileName += sLog->GetLogsTimestamp();
        }
    }

    if (5 < args.size())
    {
        if (Optional<uint32> size = Acore::StringTo<uint32>(args[5]))
        {
            _maxFileSize = *size;
        }
        else
        {
            throw InvalidAppenderArgsException(Acore::StringFormat("Log::CreateAppenderFromConfig: Invalid size '{}' for appender {}", args[5], name));
        }
    }

    _dynamicName = std::string::npos != _fileName.find("%s");
    _backup = (flags & APPENDER_FLAGS_MAKE_FILE_BACKUP) != 0;

    if (!_dynamicName)
    {
        logfile = OpenFile(_fileName, mode, (mode == "w") && _backup);
    }
}

AppenderJsonl::~AppenderJsonl()
{
    CloseFile();
}

void AppenderJsonl::_write(LogMessage const* message)
{
    // Map severity
    char const* sevStr;
    switch (message->level)
    {
        case LOG_LEVEL_FATAL:    sevStr = "fatal"; break;
        case LOG_LEVEL_ERROR:    sevStr = "error"; break;
        case LOG_LEVEL_WARN:     sevStr = "warn";  break;
        case LOG_LEVEL_INFO:     sevStr = "info";  break;
        case LOG_LEVEL_DEBUG:    sevStr = "debug"; break;
        case LOG_LEVEL_TRACE:    sevStr = "trace"; break;
        default:                 sevStr = "unknown"; break;
    }

    std::string cat = MapLoggerToCategory(message->type);
    std::string ctx = ExtractContext(message->type, message->text);

    // Format timestamp as ISO 8601 (reuse mtime from LogMessage)
    std::string ts = Acore::Time::TimeToTimestampStr(message->mtime, "%Y-%m-%dT%X");

    // Build JSON line
    std::string jsonLine;
    jsonLine.reserve(256);
    jsonLine += "{\"ts\":\"";
    jsonLine += ts;
    jsonLine += "\",\"sev\":\"";
    jsonLine += sevStr;
    jsonLine += "\",\"cat\":\"";
    jsonLine += JsonEscape(cat);
    jsonLine += "\",\"msg\":\"";
    jsonLine += JsonEscape(message->text);
    jsonLine += "\"";

    if (!ctx.empty())
    {
        jsonLine += ",\"ctx\":{";
        jsonLine += ctx;
        jsonLine += "}";
    }

    jsonLine += "}";

    bool exceedMaxSize = _maxFileSize > 0 && (_fileSize.load() + jsonLine.size()) > _maxFileSize;

    if (_dynamicName)
    {
        char namebuf[ACORE_PATH_MAX];
        snprintf(namebuf, ACORE_PATH_MAX, _fileName.c_str(), message->param1.c_str());

        // always use "a" with dynamic name otherwise it could delete the log we wrote in last _write() call
        FILE* file = OpenFile(namebuf, "a", _backup || exceedMaxSize);
        if (!file)
        {
            return;
        }

        fprintf(file, "%s\n", jsonLine.c_str());
        fflush(file);
        _fileSize += uint64(jsonLine.size() + 1);
        fclose(file);

        return;
    }
    else if (exceedMaxSize)
    {
        logfile = OpenFile(_fileName, "w", true);
    }

    if (!logfile)
    {
        return;
    }

    fprintf(logfile, "%s\n", jsonLine.c_str());
    fflush(logfile);
    _fileSize += uint64(jsonLine.size() + 1);
}

FILE* AppenderJsonl::OpenFile(std::string const& filename, std::string const& mode, bool backup)
{
    std::string fullName(_logDir + filename);
    if (backup)
    {
        CloseFile();
        std::string newName(fullName);
        newName.push_back('.');
        newName.append(LogMessage::getTimeStr(GetEpochTime()));
        std::replace(newName.begin(), newName.end(), ':', '-');
        rename(fullName.c_str(), newName.c_str()); // no error handling... if we couldn't make a backup, just ignore
    }

    if (FILE* ret = fopen(fullName.c_str(), mode.c_str()))
    {
        _fileSize = ftell(ret);
        return ret;
    }

    return nullptr;
}

void AppenderJsonl::CloseFile()
{
    if (logfile)
    {
        fclose(logfile);
        logfile = nullptr;
    }
}

std::string AppenderJsonl::JsonEscape(std::string const& str)
{
    std::string result;
    result.reserve(str.size() + 16);

    for (char c : str)
    {
        switch (c)
        {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    result += buf;
                }
                else
                {
                    result += c;
                }
                break;
        }
    }

    return result;
}

std::string AppenderJsonl::MapLoggerToCategory(std::string const& loggerType)
{
    // Map known logger type prefixes to short category names
    if (loggerType.compare(0, 7, "scripts") == 0)
        return "script";
    if (loggerType.compare(0, 6, "spells") == 0)
        return "spell";
    if (loggerType.compare(0, 8, "entities") == 0 || loggerType.compare(0, 9, "creatures") == 0)
        return "creature";
    if (loggerType.compare(0, 3, "sql") == 0)
        return "db";
    if (loggerType.compare(0, 7, "network") == 0)
        return "network";
    if (loggerType.compare(0, 4, "maps") == 0)
        return "map";
    if (loggerType.compare(0, 6, "server") == 0)
        return "server";

    // Otherwise use the first segment before "."
    std::size_t dot = loggerType.find('.');
    if (dot != std::string::npos)
        return loggerType.substr(0, dot);

    return loggerType;
}

// Search for a pattern string followed by optional whitespace/punctuation then digits.
// Returns the extracted number as a string, or false if not found.
bool AppenderJsonl::FindNumberAfter(std::string const& text, char const* pattern, std::string& out)
{
    char const* pos = strstr(text.c_str(), pattern);
    if (!pos)
        return false;

    pos += strlen(pattern);

    // Skip optional whitespace, colon, equals
    while (*pos == ' ' || *pos == ':' || *pos == '=' || *pos == '\t')
        ++pos;

    if (!std::isdigit(static_cast<unsigned char>(*pos)))
        return false;

    char const* start = pos;
    while (std::isdigit(static_cast<unsigned char>(*pos)))
        ++pos;

    out.assign(start, pos);
    return true;
}

std::string AppenderJsonl::ExtractContext(std::string const& /*loggerType*/, std::string const& text)
{
    std::string result;
    std::string num;
    bool hasField = false;

    // Look for entry/creature ID patterns
    // Patterns: "entry 123", "Entry: 123", "entryorguid 123", "Entry 123"
    if (FindNumberAfter(text, "entryorguid", num) ||
        FindNumberAfter(text, "entry", num) ||
        FindNumberAfter(text, "Entry", num))
    {
        if (hasField) result += ",";
        result += "\"entry\":";
        result += num;
        hasField = true;
    }

    // Look for map ID patterns
    // Patterns: "map 36", "Map: 36", "Map 36"
    if (FindNumberAfter(text, "map", num) ||
        FindNumberAfter(text, "Map", num))
    {
        if (hasField) result += ",";
        result += "\"map\":";
        result += num;
        hasField = true;
    }

    // Look for spell ID patterns
    // Patterns: "spell 133", "Spell: 133", "Spell 133"
    if (FindNumberAfter(text, "spell", num) ||
        FindNumberAfter(text, "Spell", num))
    {
        if (hasField) result += ",";
        result += "\"spell\":";
        result += num;
        hasField = true;
    }

    // Look for GUID patterns
    // Patterns: "GUID 12345", "GUID: 12345"
    if (FindNumberAfter(text, "GUID", num))
    {
        if (hasField) result += ",";
        result += "\"guid\":";
        result += num;
        hasField = true;
    }

    return result;
}
