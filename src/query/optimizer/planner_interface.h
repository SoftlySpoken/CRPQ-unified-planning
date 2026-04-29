#pragma once

#include <memory>
#include <set>
#include <vector>

#include "query/optimizer/plan/plan.h"
#include "query/parser/op/sparql/ops.h"
#include "query/optimizer/planner_metrics.h"

/**
 * Abstract interface for query planners.
 * Allows switching between different planning strategies while
 * maintaining the same interface and execution pipeline.
 */
class PlannerInterface {
public:
    virtual ~PlannerInterface() = default;

    /**
     * Create an execution plan for the given basic graph pattern.
     *
     * @param op_basic_graph_pattern The pattern to plan
     * @param base_plans Vector of base plans (triples/paths) to optimize
     * @param safe_assigned_vars Variables that are already bound from outer contexts
     * @param binding_size Size of the binding context
     * @param metrics Optional metrics collector for performance tracking
     * @return A Plan that can be executed, or nullptr if planning fails
     */
    virtual std::unique_ptr<Plan> create_plan(
        SPARQL::OpBasicGraphPattern& op_basic_graph_pattern,
        std::vector<std::unique_ptr<Plan>>& base_plans,
        const std::set<VarId>& safe_assigned_vars,
        uint64_t binding_size,
        PlannerMetrics* metrics = nullptr
    ) = 0;

    /**
     * Get the name of this planner for identification and logging.
     */
    virtual const std::string& get_name() const = 0;

    /**
     * Check if this planner can handle the given pattern.
     * Some planners may not support certain types of queries.
     */
    virtual bool can_handle(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const = 0;

    /**
     * Get estimated cost for planning this pattern.
     * Used for cost-based planner selection.
     */
    virtual double estimate_cost(const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern) const = 0;
};

/**
 * Factory for creating planner instances based on configuration.
 */
class PlannerFactory {
public:
    enum class PlannerType {
        ORIGINAL,
        CUSTOM,
        AUTO  // Choose based on query characteristics
    };

    /**
     * Create a planner instance based on the type.
     */
    static std::unique_ptr<PlannerInterface> create_planner(PlannerType type);

    /**
     * Create the appropriate planner based on query characteristics and configuration.
     */
    static std::unique_ptr<PlannerInterface> create_auto_planner(
        const SPARQL::OpBasicGraphPattern& op_basic_graph_pattern
    );

    /**
     * Get the configured default planner type from global configuration.
     */
    static PlannerType get_default_planner_type();
};