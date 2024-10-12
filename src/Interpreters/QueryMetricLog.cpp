#include <base/getFQDNOrHostName.h>
#include <Common/CurrentThread.h>
#include <Common/DateLUT.h>
#include <Common/DateLUTImpl.h>
#include <DataTypes/DataTypeDate.h>
#include <DataTypes/DataTypeDateTime.h>
#include <DataTypes/DataTypeDateTime64.h>
#include <DataTypes/DataTypeLowCardinality.h>
#include <DataTypes/DataTypeString.h>
#include <DataTypes/DataTypesNumber.h>
#include <Interpreters/Context.h>
#include <Interpreters/QueryMetricLog.h>
#include <Interpreters/PeriodicLog.h>
#include <Interpreters/ProcessList.h>
#include <Parsers/ExpressionElementParsers.h>
#include <Parsers/parseQuery.h>

#include <chrono>
#include <mutex>


namespace DB
{

static auto logger = getLogger("QueryMetricLog");

ColumnsDescription QueryMetricLogElement::getColumnsDescription()
{
    ColumnsDescription result;
    ParserCodec codec_parser;

    result.add({"query_id",
                std::make_shared<DataTypeString>(),
                parseQuery(codec_parser, "(ZSTD(1))", 0, DBMS_DEFAULT_MAX_PARSER_DEPTH, DBMS_DEFAULT_MAX_PARSER_BACKTRACKS),
                "Query ID."});
    result.add({"hostname",
                std::make_shared<DataTypeLowCardinality>(std::make_shared<DataTypeString>()),
                parseQuery(codec_parser, "(ZSTD(1))", 0, DBMS_DEFAULT_MAX_PARSER_DEPTH, DBMS_DEFAULT_MAX_PARSER_BACKTRACKS),
                "Hostname of the server executing the query."});
    result.add({"event_date",
                std::make_shared<DataTypeDate>(),
                parseQuery(codec_parser, "(Delta(2), ZSTD(1))", 0, DBMS_DEFAULT_MAX_PARSER_DEPTH, DBMS_DEFAULT_MAX_PARSER_BACKTRACKS),
                "Event date."});
    result.add({"event_time",
                std::make_shared<DataTypeDateTime>(),
                parseQuery(codec_parser, "(Delta(4), ZSTD(1))", 0, DBMS_DEFAULT_MAX_PARSER_DEPTH, DBMS_DEFAULT_MAX_PARSER_BACKTRACKS),
                "Event time."});
    result.add({"event_time_microseconds",
                std::make_shared<DataTypeDateTime64>(6),
                parseQuery(codec_parser, "(Delta(4), ZSTD(1))", 0, DBMS_DEFAULT_MAX_PARSER_DEPTH, DBMS_DEFAULT_MAX_PARSER_BACKTRACKS),
                "Event time with microseconds resolution."});
    result.add({"memory_usage",
                std::make_shared<DataTypeUInt64>(),
                "Amount of RAM the query uses. It might not include some types of dedicated memory."});
    result.add({"peak_memory_usage",
                std::make_shared<DataTypeUInt64>(),
                "Maximum amount of RAM the query used."});

    for (size_t i = 0, end = ProfileEvents::end(); i < end; ++i)
    {
        auto name = fmt::format("ProfileEvent_{}", ProfileEvents::getName(ProfileEvents::Event(i)));
        const auto * comment = ProfileEvents::getDocumentation(ProfileEvents::Event(i));
        result.add({std::move(name), std::make_shared<DataTypeUInt64>(), comment});
    }

    return result;
}

void QueryMetricLogElement::appendToBlock(MutableColumns & columns) const
{
    size_t column_idx = 0;

    columns[column_idx++]->insert(query_id);
    columns[column_idx++]->insert(getFQDNOrHostName());
    columns[column_idx++]->insert(DateLUT::instance().toDayNum(event_time).toUnderType());
    columns[column_idx++]->insert(event_time);
    columns[column_idx++]->insert(event_time_microseconds);
    columns[column_idx++]->insert(memory_usage);
    columns[column_idx++]->insert(peak_memory_usage);

    for (size_t i = 0, end = ProfileEvents::end(); i < end; ++i)
        columns[column_idx++]->insert(profile_events[i]);
}

void QueryMetricLog::shutdown()
{
    Base::shutdown();
}

void QueryMetricLog::startQuery(const String & query_id, TimePoint query_start_time, UInt64 interval_milliseconds)
{
    QueryMetricLogStatus status;
    status.interval_milliseconds = interval_milliseconds;
    status.next_collect_time = query_start_time + std::chrono::milliseconds(interval_milliseconds);

    const auto & profile_events = CurrentThread::getProfileEvents();
    for (ProfileEvents::Event i = ProfileEvents::Event(0), end = ProfileEvents::end(); i < end; ++i)
        status.last_profile_events[i] = profile_events[i].load(std::memory_order_relaxed);

    auto context = getContext();
    const auto & process_list = context->getProcessList();
    status.task = context->getQueryMetricLogPool().createTask("QueryMetricLog", [this, &process_list, query_id] {
        auto current_time = std::chrono::system_clock::now();
        const auto query_info = process_list.getQueryInfo(query_id, false, true, false);
        if (!query_info)
            return;

        auto elem = createLogMetricElement(query_id, *query_info, current_time);
        if (elem)
            add(std::move(elem.value()));
    });

    status.task->scheduleAfter(interval_milliseconds);

    std::lock_guard lock(queries_mutex);
    queries.emplace(query_id, std::move(status));
}

void QueryMetricLog::finishQuery(const String & query_id, QueryStatusInfoPtr query_info)
{
    std::unique_lock lock(queries_mutex);
    auto it = queries.find(query_id);

    /// finishQuery may be called from logExceptionBeforeStart when the query has not even started
    /// yet, so its corresponding startQuery is never called.
    if (it == queries.end())
        return;

    if (query_info)
    {
        auto elem = createLogMetricElement(query_id, *query_info, std::chrono::system_clock::now(), false);
        if (elem)
            add(std::move(elem.value()));
    }

    /// Take ownership of the task, setting finished to true so that we don't try to schedule it anymore.
    auto task = std::move(it->second.task);
    it->second.finished = true;

    /// Make sure to always deactivate the task without locking queries_mutex to prevent deadlock.
    lock.unlock();
    task->deactivate();
    lock.lock();
    queries.erase(query_id);
}

std::optional<QueryMetricLogElement> QueryMetricLog::createLogMetricElement(const String & query_id, const QueryStatusInfo & query_info, TimePoint current_time, bool schedule_next)
{
    std::lock_guard lock(queries_mutex);
    auto query_status_it = queries.find(query_id);

    /// The query might have finished while the scheduled task is running.
    if (query_status_it == queries.end())
        return {};

    QueryMetricLogElement elem;
    elem.event_time = timeInSeconds(current_time);
    elem.event_time_microseconds = timeInMicroseconds(current_time);
    elem.query_id = query_status_it->first;
    elem.memory_usage = query_info.memory_usage > 0 ? query_info.memory_usage : 0;
    elem.peak_memory_usage = query_info.peak_memory_usage > 0 ? query_info.peak_memory_usage : 0;

    auto & query_status = query_status_it->second;
    if (query_info.profile_counters)
    {
        for (ProfileEvents::Event i = ProfileEvents::Event(0), end = ProfileEvents::end(); i < end; ++i)
        {
            const auto & new_value = (*(query_info.profile_counters))[i];
            elem.profile_events[i] = new_value - query_status.last_profile_events[i];
            query_status.last_profile_events[i] = new_value;
        }
    }
    else
    {
        elem.profile_events = query_status.last_profile_events;
    }

    if (!query_status.finished && schedule_next)
    {
        query_status.next_collect_time += std::chrono::milliseconds(query_status.interval_milliseconds);
        const auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(query_status.next_collect_time - std::chrono::system_clock::now()).count();
        query_status.task->scheduleAfter(wait_time);
    }

    return elem;
}

}
