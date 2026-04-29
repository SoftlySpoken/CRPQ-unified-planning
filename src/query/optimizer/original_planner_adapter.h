#pragma once

#include "planner_interface.h"
#include "query/optimizer/plan/join_order/greedy_optimizer.h"
#include "query/optimizer/plan/join_order/leapfrog_optimizer.h"

/**
 * Adapter that wraps the original planning logic (LeapfrogOptimizer + GreedyOptimizer)
 * to implement the PlannerInterface.
 *
 * This allows the original planning strategy to be used through the new
 * unified planner interface, enabling fair comparison with custom planners.
 */
class OriginalPlannerAdapter : public PlannerInterface {
private:
    static const std::string NAME;

public:
    /**
     * Create an execution plan using the original planning strategy.
     * This follows the same logic as the original BindingIterConstructor:
     * 1. Try LeapfrogOptimizer first (returns BindingIter directly)
     * 2. Fall back to GreedyOptimizer if leapfrog fails (returns Plan)
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

    bool can_handle(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const override {
        // Original planner can handle any basic graph pattern
        return true;
    }

    double estimate_cost(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const override;

private:
    /**
     * Helper class to wrap a BindingIter as a Plan.
     * This is needed because LeapfrogOptimizer returns BindingIter directly,
     * but we need to return Plan from the interface.
     */
    class BindingIterPlan : public Plan {
    private:
        mutable std::unique_ptr<BindingIter> binding_iter_;
        std::set<VarId> vars_;
        double cost_;

    public:
        BindingIterPlan(std::unique_ptr<BindingIter> binding_iter,
                       const std::set<VarId>& vars,
                       double cost = 1.0)
            : binding_iter_(std::move(binding_iter)), vars_(vars), cost_(cost) {}

        std::unique_ptr<BindingIter> get_binding_iter() const override {
            return std::move(binding_iter_);
        }

        double estimate_cost() const override {
            return cost_;
        }

        double estimate_output_size() const override {
            return cost_; // Simple approximation
        }

        std::set<VarId> get_vars() const override {
            return vars_;
        }

        std::unique_ptr<Plan> clone() const override {
            // Note: BindingIter cannot be cloned, so we return a new BindingIterPlan
            // with nullptr for binding_iter_. This is a limitation of this wrapper.
            return std::make_unique<BindingIterPlan>(nullptr, vars_, cost_);
        }

        void set_input_vars(const std::set<VarId>& input_vars) override {
            // This implementation is a no-op since BindingIter doesn't support
            // input variable assignment after construction
            (void)input_vars; // Suppress unused parameter warning
        }

        bool get_leapfrog_iter(std::vector<std::unique_ptr<LeapfrogIter>>& leapfrog_iters,
                               std::vector<VarId>& var_order,
                               uint_fast32_t& enumeration_level) const override {
            // BindingIterPlan wraps a BindingIter, so leapfrog optimization is not applicable
            (void)leapfrog_iters;     // Suppress unused parameter warnings
            (void)var_order;
            (void)enumeration_level;
            return false;
        }

        int relation_size() const override {
            // Return 1 as this represents a single relation/binding iterator
            return 1;
        }

        void print(std::ostream& os, int indent) const override {
            os << std::string(indent, ' ') << "OriginalPlannerBindingIter(cost=" << cost_ << ")" << std::endl;
        }
    };

    /**
     * Extract all variables from base plans for the BindingIterPlan.
     */
    std::set<VarId> extract_vars_from_plans(const std::vector<std::unique_ptr<Plan>>& base_plans) const;

    /**
     * Calculate a simple cost estimate based on the number of patterns.
     */
    double calculate_simple_cost(const std::vector<std::unique_ptr<Plan>>& base_plans) const;
};