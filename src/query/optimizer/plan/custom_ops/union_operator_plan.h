#pragma once

#include "query/optimizer/plan/two_column_plan.h"
#include "query/var_id.h"
// #include "storage/index/object_id.h"
#include <memory>
#include <vector>

class UnionOperatorPlan : public TwoColumnPlan {
public:
    // Union processing modes (matching UnionOperator::UnionMode)
    enum class UnionMode {
        UNION_ALL,      // No duplicate elimination
        UNION_DISTINCT, // With duplicate elimination
        UNION_ORDERED   // Maintain order from children
    };

    // Constructor with two variables
    UnionOperatorPlan(
        std::vector<std::unique_ptr<Plan>> children,
        VarId col1_var,
        VarId col2_var,
        bool eliminate_duplicates = true,
        bool _lazy = false
    );

    // Constructor with constant first column
    UnionOperatorPlan(
        std::vector<std::unique_ptr<Plan>> children,
        ObjectId constant_col1_value,
        VarId col2_var,
        bool eliminate_duplicates = true,
        bool _lazy = false
    );
    // Constructor with constant second column
    UnionOperatorPlan(
        std::vector<std::unique_ptr<Plan>> children,
        VarId col1_var,
        ObjectId constant_col2_value,
        bool eliminate_duplicates = true,
        bool _lazy = false
    );

    UnionOperatorPlan(const UnionOperatorPlan& other);

    std::unique_ptr<Plan> clone() const override {
        return std::make_unique<UnionOperatorPlan>(*this);
    }

    // only meant to be used by base plans, not joins
    int relation_size() const override { return 0; }

    double estimate_cost() const override { return estimated_cost; }
    double estimate_output_size() const override { return estimated_output_size; }

    std::set<VarId> get_vars() const override;
    void set_input_vars(const std::set<VarId>& input_vars) override;

    void add_child(std::unique_ptr<Plan> c) { children.emplace_back(std::move(c)); }

    std::unique_ptr<BindingIter> get_binding_iter() const override;

    bool get_leapfrog_iter(std::vector<std::unique_ptr<LeapfrogIter>>&,
                           std::vector<VarId>&,
                           uint_fast32_t&) const override { return false; }

    void print(std::ostream& os, int indent) const override;

    // Union-specific methods
    void set_union_mode(UnionMode mode);
    void set_duplicate_elimination(bool enable);
    UnionMode get_union_mode() const { return union_mode; }

private:
    std::vector<std::unique_ptr<Plan>> children;
    bool eliminate_duplicates;
    UnionMode union_mode;

    double estimated_cost;
    double estimated_output_size;
};