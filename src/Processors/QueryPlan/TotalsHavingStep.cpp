#include <Processors/QueryPlan/TotalsHavingStep.h>
#include <Processors/Transforms/DistinctTransform.h>
#include <Processors/Transforms/TotalsHavingTransform.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Interpreters/ExpressionActions.h>
#include <IO/Operators.h>
#include <Common/JSONBuilder.h>
#include <Core/SettingsEnums.h>

namespace DB
{

static ITransformingStep::Traits getTraits(bool has_filter)
{
    return ITransformingStep::Traits
    {
        {
            .returns_single_stream = true,
            .preserves_number_of_streams = false,
            .preserves_sorting = true,
        },
        {
            .preserves_number_of_rows = !has_filter,
        }
    };
}

TotalsHavingStep::TotalsHavingStep(
    const Header & input_header_,
    const AggregateDescriptions & aggregates_,
    bool overflow_row_,
    std::optional<ActionsDAG> actions_dag_,
    const std::string & filter_column_,
    bool remove_filter_,
    TotalsMode totals_mode_,
    double auto_include_threshold_,
    bool final_)
    : ITransformingStep(
        input_header_,
        TotalsHavingTransform::transformHeader(
            input_header_,
            actions_dag_ ? &*actions_dag_ : nullptr,
            filter_column_,
            remove_filter_,
            final_,
            getAggregatesMask(input_header_, aggregates_)),
        getTraits(!filter_column_.empty()))
    , aggregates(aggregates_)
    , overflow_row(overflow_row_)
    , actions_dag(std::move(actions_dag_))
    , filter_column_name(filter_column_)
    , remove_filter(remove_filter_)
    , totals_mode(totals_mode_)
    , auto_include_threshold(auto_include_threshold_)
    , final(final_)
{
}

void TotalsHavingStep::transformPipeline(QueryPipelineBuilder & pipeline, const BuildQueryPipelineSettings & settings)
{
    auto expression_actions = actions_dag ? std::make_shared<ExpressionActions>(std::move(*actions_dag), settings.getActionsSettings()) : nullptr;

    auto totals_having = std::make_shared<TotalsHavingTransform>(
        pipeline.getHeader(),
        getAggregatesMask(pipeline.getHeader(), aggregates),
        overflow_row,
        expression_actions,
        filter_column_name,
        remove_filter,
        totals_mode,
        auto_include_threshold,
        final);

    pipeline.addTotalsHavingTransform(std::move(totals_having));
}

static String totalsModeToString(TotalsMode totals_mode, double auto_include_threshold)
{
    switch (totals_mode)
    {
        case TotalsMode::BEFORE_HAVING:
            return "before_having";
        case TotalsMode::AFTER_HAVING_INCLUSIVE:
            return "after_having_inclusive";
        case TotalsMode::AFTER_HAVING_EXCLUSIVE:
            return "after_having_exclusive";
        case TotalsMode::AFTER_HAVING_AUTO:
            return "after_having_auto threshold " + std::to_string(auto_include_threshold);
    }
}

void TotalsHavingStep::describeActions(FormatSettings & settings) const
{
    String prefix(settings.offset, ' ');
    settings.out << prefix << "Filter column: " << filter_column_name;
    if (remove_filter)
        settings.out << " (removed)";
    settings.out << '\n';
    settings.out << prefix << "Mode: " << totalsModeToString(totals_mode, auto_include_threshold) << '\n';

    if (actions_dag)
    {
        bool first = true;
        if (actions_dag)
        {
            auto expression = std::make_shared<ExpressionActions>(actions_dag->clone());
            for (const auto & action : expression->getActions())
            {
                settings.out << prefix << (first ? "Actions: "
                                                : "         ");
                first = false;
                settings.out << action.toString() << '\n';
            }
        }
    }
}

void TotalsHavingStep::describeActions(JSONBuilder::JSONMap & map) const
{
    map.add("Mode", totalsModeToString(totals_mode, auto_include_threshold));
    if (actions_dag)
    {
        map.add("Filter column", filter_column_name);
        if (actions_dag)
        {
            auto expression = std::make_shared<ExpressionActions>(actions_dag->clone());
            map.add("Expression", expression->toTree());
        }
    }
}

void TotalsHavingStep::updateOutputHeader()
{
    output_header =
        TotalsHavingTransform::transformHeader(
            input_headers.front(),
            getActions(),
            filter_column_name,
            remove_filter,
            final,
            getAggregatesMask(input_headers.front(), aggregates));
}


}
