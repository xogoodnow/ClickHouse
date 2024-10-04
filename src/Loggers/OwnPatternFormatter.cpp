#include "OwnPatternFormatter.h"

#include <functional>
#include <IO/WriteBufferFromString.h>
#include <IO/WriteHelpers.h>
#include <Common/HashTable/Hash.h>
#include <base/terminalColors.h>


OwnPatternFormatter::OwnPatternFormatter(bool color_, const std::string & format_)
    : Poco::PatternFormatter(""), timestamp_format(format_), color(color_)
{
}



void OwnPatternFormatter::formatExtended(const DB::ExtendedLogMessage & msg_ext, std::string & text) const
{
    DB::WriteBufferFromString(wb, text);

    const Poco::Message & msg = msg_ext.base;

    // Check the timestamp format
    if (timestamp_format == "ISO_8601")
    {
        // Use ISO 8601 formatting
        std::time_t time = msg_ext.time_seconds;
        std::tm tm;
        localtime_r(&time, &tm);  // Convert time to local time

        // Format as ISO 8601 (YYYY-MM-DDTHH:MM:SS.mmmZ)
        DB::writeChar('[', wb);
        DB::writeChar('0' + tm.tm_year + 1900, wb); // Year
        DB::writeChar('-', wb);
        DB::writeChar('0' + tm.tm_mon + 1, wb);     // Month
        DB::writeChar('-', wb);
        DB::writeChar('0' + tm.tm_mday, wb);        // Day
        DB::writeChar('T', wb);
        DB::writeChar('0' + tm.tm_hour, wb);        // Hour
        DB::writeChar(':', wb);
        DB::writeChar('0' + tm.tm_min, wb);         // Minute
        DB::writeChar(':', wb);
        DB::writeChar('0' + tm.tm_sec, wb);         // Second
        DB::writeChar('.', wb);
        DB::writeChar('0' + ((msg_ext.time_microseconds / 100000) % 10), wb); // Microseconds
        DB::writeChar(']', wb);
    }
    else
    {
        // Default UNIX formatting
        DB::writeDateTimeText<'.', ':'>(msg_ext.time_seconds, wb, server_timezone);
        DB::writeChar('.', wb);
        DB::writeChar('0' + ((msg_ext.time_microseconds / 100000) % 10), wb);
        DB::writeChar('0' + ((msg_ext.time_microseconds / 10000) % 10), wb);
        DB::writeChar('0' + ((msg_ext.time_microseconds / 1000) % 10), wb);
        DB::writeChar('0' + ((msg_ext.time_microseconds / 100) % 10), wb);
        DB::writeChar('0' + ((msg_ext.time_microseconds / 10) % 10), wb);
        DB::writeChar('0' + ((msg_ext.time_microseconds / 1) % 10), wb);
    }

    writeCString(" [ ", wb);
    if (color)
        writeString(setColor(intHash64(msg_ext.thread_id)), wb);
    DB::writeIntText(msg_ext.thread_id, wb);
    if (color)
        writeCString(resetColor(), wb);
    writeCString(" ] ", wb);

    writeCString("{", wb);
    if (color)
        writeString(setColor(std::hash<std::string>()(msg_ext.query_id)), wb);
    DB::writeString(msg_ext.query_id, wb);
    if (color)
        writeCString(resetColor(), wb);
    writeCString("} ", wb);

    writeCString("<", wb);
    int priority = static_cast<int>(msg.getPriority());
    if (color)
        writeCString(setColorForLogPriority(priority), wb);
    DB::writeString(getPriorityName(priority), wb);
    if (color)
        writeCString(resetColor(), wb);
    writeCString("> ", wb);
    if (color)
        writeString(setColor(std::hash<std::string>()(msg.getSource())), wb);
    DB::writeString(msg.getSource(), wb);
    if (color)
        writeCString(resetColor(), wb);
    writeCString(": ", wb);
    DB::writeString(msg.getText(), wb);
}


void OwnPatternFormatter::format(const Poco::Message & msg, std::string & text)
{
    formatExtended(DB::ExtendedLogMessage::getFrom(msg), text);
}
