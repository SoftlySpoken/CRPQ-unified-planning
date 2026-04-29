#include "planner_interface.h"
#include "planner_config.h"
#include "original_planner_adapter.h"
#include "custom_planner_adapter.h"

std::unique_ptr<PlannerInterface> PlannerFactory::create_planner(PlannerType type) {
    switch (type) {
        case PlannerType::ORIGINAL:
            return std::make_unique<OriginalPlannerAdapter>();
        case PlannerType::CUSTOM:
            return std::make_unique<CustomPlannerAdapter>();
        case PlannerType::AUTO:
            // For now, AUTO defaults to ORIGINAL
            // In the future, this could use heuristics to choose
            return std::make_unique<OriginalPlannerAdapter>();
        default:
            return std::make_unique<OriginalPlannerAdapter>();
    }
}

std::unique_ptr<PlannerInterface> PlannerFactory::create_auto_planner(
    const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern
) {
    // Simple heuristics for auto-selection
    // In a more sophisticated system, this could analyze:
    // - Number of triple patterns
    // - Presence of path patterns
    // - Query complexity metrics
    // - Historical performance data

    const auto& config = PlannerConfig::get_instance();

    // If custom planner is explicitly enabled, prefer it
    if (config.custom_planner_enabled) {
        auto custom_planner = std::make_unique<CustomPlannerAdapter>();
        if (custom_planner->can_handle(op_basic_graph_pattern)) {
            return std::move(custom_planner);
        }
    }

    // Default to original planner
    return std::make_unique<OriginalPlannerAdapter>();
}

PlannerFactory::PlannerType PlannerFactory::get_default_planner_type() {
    const auto& config = PlannerConfig::get_instance();

    if (config.custom_planner_enabled) {
        return PlannerType::CUSTOM;
    } else {
        return PlannerType::ORIGINAL;
    }
}