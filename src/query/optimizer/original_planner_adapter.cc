#include "original_planner_adapter.h"
#include "planner_config.h"
#include <iostream>

const std::string OriginalPlannerAdapter::NAME = "original";

std::unique_ptr<Plan> OriginalPlannerAdapter::create_plan(
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
        // Set input vars for all base plans (matching original logic)
        for (auto& plan : base_plans) {
            plan->set_input_vars(safe_assigned_vars);
        }

        std::unique_ptr<BindingIter> binding_iter = nullptr;
        std::unique_ptr<Plan> root_plan = nullptr;

        // Try to use leapfrog if there is a join (matching original logic)
        if (base_plans.size() > 1) {
            if (safe_assigned_vars.size() > 0) {
                binding_iter = LeapfrogOptimizer::try_get_iter_with_assigned(base_plans, binding_size);
            } else {
                binding_iter = LeapfrogOptimizer::try_get_iter_without_assigned(base_plans, binding_size);
            }
        }

        // If leapfrog failed or not applicable, use greedy optimizer
        if (binding_iter == nullptr) {
            root_plan = GreedyOptimizer::get_plan(base_plans);
            if (metrics) {
                metrics->estimated_cost = root_plan ? root_plan->estimate_cost() : 0.0;
                metrics->plan_description = "GreedyOptimizer plan";
            }
            if (metrics) {
                metrics->end_planning();
            }
            return root_plan;
        } else {
            // Wrap the BindingIter in a Plan
            auto vars = extract_vars_from_plans(base_plans);
            auto cost = calculate_simple_cost(base_plans);
            auto plan = std::make_unique<BindingIterPlan>(std::move(binding_iter), vars, cost);

            if (metrics) {
                metrics->estimated_cost = cost;
                metrics->plan_description = "LeapfrogOptimizer plan";
                metrics->end_planning();
            }

            return std::move(plan);
        }
    }
    catch (const std::exception& e) {
        if (metrics) {
            metrics->set_error("Original planner failed: " + std::string(e.what()));
            metrics->end_planning();
        }

        const auto& config = PlannerConfig::get_instance();
        if (config.is_verbose()) {
            std::cerr << "OriginalPlannerAdapter error: " << e.what() << std::endl;
        }

        throw;
    }
}

double OriginalPlannerAdapter::estimate_cost(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const {
    // Simple cost estimate based on number of patterns
    // In a real implementation, this could be more sophisticated
    double base_cost = 1.0;
    base_cost += op_basic_graph_pattern.triples.size() * 10.0;
    base_cost += op_basic_graph_pattern.paths.size() * 20.0; // Path queries are typically more expensive
    return base_cost;
}

std::set<VarId> OriginalPlannerAdapter::extract_vars_from_plans(
    const std::vector<std::unique_ptr<Plan>>& base_plans
) const {
    std::set<VarId> all_vars;
    for (const auto& plan : base_plans) {
        auto plan_vars = plan->get_vars();
        all_vars.insert(plan_vars.begin(), plan_vars.end());
    }
    return all_vars;
}

double OriginalPlannerAdapter::calculate_simple_cost(
    const std::vector<std::unique_ptr<Plan>>& base_plans
) const {
    double total_cost = 1.0;
    for (const auto& plan : base_plans) {
        total_cost += plan->estimate_cost();
    }
    return total_cost;
}