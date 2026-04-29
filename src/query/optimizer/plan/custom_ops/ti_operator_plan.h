#pragma once

#include "query/optimizer/plan/plan.h"
#include "query/optimizer/plan/two_column_plan.h"
#include <vector>

class TIOperatorPlan : public Plan {
public:
    TIOperatorPlan(
        std::unique_ptr<Plan> leftmost_operand,
        std::vector<std::unique_ptr<Plan>> edge_path_operands,
        bool _intersect_in_leftmost=false
    );

    TIOperatorPlan(const TIOperatorPlan& other);

    std::unique_ptr<Plan> clone() const override {
        return std::make_unique<TIOperatorPlan>(*this);
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
    bool get_const_col2() const {
        const auto *leftmost_two_col_ptr = dynamic_cast<TwoColumnPlan *>(leftmost_operand.get());
        if (leftmost_two_col_ptr && leftmost_two_col_ptr->get_has_constant_col2()) {
            return true;
        }
        for (const auto &ep : edge_path_operands) {
            const auto *ep_two_col_ptr = dynamic_cast<TwoColumnPlan *>(ep.get());
            if (ep_two_col_ptr && ep_two_col_ptr->get_has_constant_col2()) {
                return true;
            }
        }
        return false;
    }

private:
    std::unique_ptr<Plan> leftmost_operand;
    std::vector<std::unique_ptr<Plan>> edge_path_operands;
    bool intersect_in_leftmost;

    double estimated_cost;
    double estimated_output_size;
};