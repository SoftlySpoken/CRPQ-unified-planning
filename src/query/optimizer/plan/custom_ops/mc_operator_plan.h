#pragma once

#include "query/optimizer/plan/plan.h"

class MCOperatorPlan : public Plan {
public:
    MCOperatorPlan(
        std::unique_ptr<Plan> child_A,
        std::unique_ptr<Plan> child_B,
        bool left_epsilon = false,
        bool right_epsilon = false
    );

    MCOperatorPlan(const MCOperatorPlan& other) :
        Plan(other),
        child_A             (other.child_A->clone()),
        child_B             (other.child_B->clone()),
        left_epsilon        (other.left_epsilon),
        right_epsilon       (other.right_epsilon),
        estimated_cost      (other.estimated_cost),
        estimated_output_size (other.estimated_output_size) { }

    std::unique_ptr<Plan> clone() const override {
        return std::make_unique<MCOperatorPlan>(*this);
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

private:
    std::unique_ptr<Plan> child_A;
    std::unique_ptr<Plan> child_B;
    bool left_epsilon;
    bool right_epsilon;

    double estimated_cost;
    double estimated_output_size;
};