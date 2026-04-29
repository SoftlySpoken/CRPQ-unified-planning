#pragma once

#include "query/optimizer/plan/two_column_plan.h"

class KCOperatorPlan : public TwoColumnPlan {
public:
    // Constructor with variable col1
    KCOperatorPlan(
        std::unique_ptr<Plan> child,
        VarId col1_var,
        VarId col2_var,
        bool epsilon = false,
        bool lazy = false
    );

    // Constructor with constant col1
    KCOperatorPlan(
        std::unique_ptr<Plan> child,
        ObjectId constant_col1_value,
        VarId col2_var,
        bool epsilon = false,
        bool lazy = false
    );
    // Constructor with constant col2
    KCOperatorPlan(
        std::unique_ptr<Plan> child,
        VarId col1_var,
        ObjectId constant_col2_value,
        bool epsilon = false,
        bool lazy = false
    );

    KCOperatorPlan(const KCOperatorPlan& other) :
        TwoColumnPlan(other),
        child               (other.child->clone()),
        estimated_cost      (other.estimated_cost),
        estimated_output_size (other.estimated_output_size) {
        }

    std::unique_ptr<Plan> clone() const override {
        return std::make_unique<KCOperatorPlan>(*this);
    }

    // only meant to be used by base plans, not joins
    int relation_size() const override { return 0; }

    double estimate_cost() const override { return estimated_cost; }
    double estimate_output_size() const override { return estimated_output_size; }

    std::set<VarId> get_vars() const override;
    void set_input_vars(const std::set<VarId>& input_vars) override;

    std::unique_ptr<BindingIter> get_binding_iter() const override;

    bool get_leapfrog_iter(std::vector<std::unique_ptr<LeapfrogIter>>&,
                           std::vector<VarId>&,
                           uint_fast32_t&) const override { return false; }

    void print(std::ostream& os, int indent) const override;
    std::unique_ptr<Plan> get_child() const { return child->clone(); }

private:
    std::unique_ptr<Plan> child;

    double estimated_cost;
    double estimated_output_size;
};