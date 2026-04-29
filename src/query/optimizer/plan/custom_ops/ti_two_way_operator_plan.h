#pragma once

#include "query/optimizer/plan/two_column_plan.h"

class TITwoWayOperatorPlan : public TwoColumnPlan {
public:
    // Constructor with variable col1
    TITwoWayOperatorPlan(
        std::unique_ptr<Plan> leftmost_operand,
        std::unique_ptr<Plan> edge_path_operand,
        VarId col1_var,
        VarId col2_var,
        bool epsilon_=false
    );

    // Constructor with constant col1
    TITwoWayOperatorPlan(
        std::unique_ptr<Plan> leftmost_operand,
        std::unique_ptr<Plan> edge_path_operand,
        ObjectId constant_col1_value,
        VarId col2_var,
        bool epsilon_=false
    );
    TITwoWayOperatorPlan(
        std::unique_ptr<Plan> leftmost_operand,
        std::unique_ptr<Plan> edge_path_operand,
        VarId col1_var,
        ObjectId constant_col2_value,
        bool epsilon_=false
    );

    TITwoWayOperatorPlan(const TITwoWayOperatorPlan& other) :
        TwoColumnPlan(other),
        leftmost_operand    (other.leftmost_operand->clone()),
        edge_path_operand   (other.edge_path_operand->clone()),
        epsilon             (other.epsilon),
        estimated_cost      (other.estimated_cost),
        estimated_output_size (other.estimated_output_size) {
        }

    std::unique_ptr<Plan> clone() const override {
        return std::make_unique<TITwoWayOperatorPlan>(*this);
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
        const auto *leftmost = dynamic_cast<TwoColumnPlan *>(leftmost_operand.get());
        if (leftmost) {
            return leftmost->get_has_constant_col2();
        }
        const auto *ep = dynamic_cast<TwoColumnPlan *>(edge_path_operand.get());
        if (ep) {
            return ep->get_has_constant_col2();
        }
        return false;
    }

private:
    std::unique_ptr<Plan> leftmost_operand;
    std::unique_ptr<Plan> edge_path_operand;
    bool epsilon;

    double estimated_cost;
    double estimated_output_size;
};