#include "custom_planner_adapter.h"
#include "planner_config.h"
#include <iostream>
#include <unordered_set>

const std::string CustomPlannerAdapter::NAME = "custom";

std::unique_ptr<Plan> CustomPlannerAdapter::create_plan(
    SPARQL::OpBasicGraphPattern& op_basic_graph_pattern,
    std::vector<std::unique_ptr<Plan>>& base_plans,
    const std::set<VarId>& safe_assigned_vars,
    uint64_t binding_size,
    PlannerMetrics* metrics
) {
    if (metrics) {
        metrics->planner_type = NAME;
        metrics->num_triple_patterns = op_basic_graph_pattern.triples.size();
        metrics->num_path_patterns = op_basic_graph_pattern.paths.size();
        metrics->start_planning(NAME);
    }

    try {
        // Check if we can handle this pattern
        if (!can_handle(op_basic_graph_pattern)) {
            if (metrics) {
                metrics->set_error("CustomPlanner cannot handle this pattern (size limits or unsupported features)");
                metrics->end_planning();
            }
            throw std::runtime_error("CustomPlanner cannot handle this pattern");
        }

        // Note: CustomPlanner doesn't use the base_plans parameter directly
        // It reconstructs the plans internally from the OpBasicGraphPattern
        // This is different from the original planner approach, but maintains
        // the existing CustomPlanner logic

        auto plan = CustomPlanner::get_plan(op_basic_graph_pattern);

        if (metrics) {
            if (plan) {
                metrics->estimated_cost = plan->estimate_cost();
                metrics->plan_description = "CustomPlanner dynamic programming plan";
            } else {
                metrics->estimated_cost = 0.0;
                metrics->plan_description = "CustomPlanner failed to generate plan";
            }
            metrics->end_planning();
        }

        const auto& config = PlannerConfig::get_instance();
        if (config.is_verbose() && plan) {
            std::cout << "CustomPlanner generated plan with cost: " << plan->estimate_cost() << std::endl;
        }

        return plan;
    }
    catch (const std::exception& e) {
        if (metrics) {
            metrics->set_error("Custom planner failed: " + std::string(e.what()));
            metrics->end_planning();
        }

        const auto& config = PlannerConfig::get_instance();
        if (config.is_verbose()) {
            std::cerr << "CustomPlannerAdapter error: " << e.what() << std::endl;
        }

        throw;
    }
}

bool CustomPlannerAdapter::can_handle(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const {
    // Check size limits (CustomPlanner uses 32-bit bitset)
    if (!is_within_size_limits(op_basic_graph_pattern)) {
        return false;
    }

    // CustomPlanner can handle most basic graph patterns
    // It's specifically designed for complex join patterns and path queries
    return true;
}

double CustomPlannerAdapter::estimate_cost(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const {
    // CustomPlanner typically performs better on complex queries
    // but may have overhead on simple queries
    double base_cost = 5.0; // Slightly higher base cost due to DP overhead

    size_t total_patterns = op_basic_graph_pattern.triples.size() + op_basic_graph_pattern.paths.size();

    if (total_patterns <= 2) {
        // Simple queries - may be slower due to overhead
        base_cost += total_patterns * 15.0;
    } else if (has_complex_patterns(op_basic_graph_pattern)) {
        // Complex queries - likely faster due to better optimization
        base_cost += total_patterns * 5.0;
    } else {
        // Medium complexity
        base_cost += total_patterns * 8.0;
    }

    return base_cost;
}

bool CustomPlannerAdapter::has_complex_patterns(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const {
    // Consider patterns complex if they have:
    // 1. Path queries (CustomPlanner handles these well)
    // 2. Many triple patterns (benefits from DP optimization)
    // 3. Complex variable sharing patterns

    if (!op_basic_graph_pattern.paths.empty()) {
        return true; // Path queries are complex
    }

    if (op_basic_graph_pattern.triples.size() >= 4) {
        return true; // Many triples benefit from DP
    }

    // Check for complex variable sharing patterns
    std::unordered_set<VarId> variables;
    int variable_occurrences = 0;

    for (const auto& triple : op_basic_graph_pattern.triples) {
        if (triple.subject.is_var()) {
            variables.insert(triple.subject.get_var());
            variable_occurrences++;
        }
        if (triple.predicate.is_var()) {
            variables.insert(triple.predicate.get_var());
            variable_occurrences++;
        }
        if (triple.object.is_var()) {
            variables.insert(triple.object.get_var());
            variable_occurrences++;
        }
    }

    // If variables are reused frequently, it's a complex pattern
    double avg_occurrences = variables.empty() ? 0.0 : (double)variable_occurrences / variables.size();
    return avg_occurrences > 2.0;
}

bool CustomPlannerAdapter::is_within_size_limits(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const {
    // CustomPlanner uses a 32-bit bitset for pattern combinations
    // So it's limited to 32 triple patterns total
    size_t total_patterns = op_basic_graph_pattern.triples.size() + op_basic_graph_pattern.paths.size();
    return total_patterns <= BITSET_SIZE;
}