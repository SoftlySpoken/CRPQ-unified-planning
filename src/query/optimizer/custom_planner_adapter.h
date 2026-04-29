#pragma once

#include "planner_interface.h"
#include "query/optimizer/custom_model/custom_planner.h"

/**
 * Adapter that wraps the CustomPlanner to implement the PlannerInterface.
 *
 * This allows the custom planning strategy to be used through the new
 * unified planner interface, enabling fair comparison with the original planner.
 */
class CustomPlannerAdapter : public PlannerInterface {
private:
    static const std::string NAME;

public:
    /**
     * Create an execution plan using the custom planning strategy.
     * This directly calls CustomPlanner::get_plan() with the provided pattern.
     */
    std::unique_ptr<Plan> create_plan(
        SPARQL::OpBasicGraphPattern& op_basic_graph_pattern,
        std::vector<std::unique_ptr<Plan>>& base_plans,
        const std::set<VarId>& safe_assigned_vars,
        uint64_t binding_size,
        PlannerMetrics* metrics = nullptr
    ) override;

    const std::string& get_name() const override {
        return NAME;
    }

    bool can_handle(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const override;

    double estimate_cost(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const override;

private:
    /**
     * Check if the pattern contains complex features that the custom planner handles well.
     */
    bool has_complex_patterns(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const;

    /**
     * Check if the pattern size is within the custom planner's limits.
     * CustomPlanner uses a 32-bit bitset, so it's limited to 32 triple patterns.
     */
    bool is_within_size_limits(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const;
};