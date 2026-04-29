#include "ti_two_way_operator_plan.h"

#include "query/executor/binding_iter/custom_ops/ti_two_way_operator.h"

// Constructor with variable col1
TITwoWayOperatorPlan::TITwoWayOperatorPlan(
    std::unique_ptr<Plan> _leftmost_operand,
    std::unique_ptr<Plan> _edge_path_operand,
    VarId col1_var,
    VarId col2_var,
    bool epsilon_
) :
    TwoColumnPlan(col1_var, col2_var),
    leftmost_operand  (std::move(_leftmost_operand)),
    edge_path_operand (std::move(_edge_path_operand)),
    epsilon(epsilon_)
{
    // TI Two-Way is simpler than full TI, but still involves join operations
    estimated_cost = leftmost_operand->estimate_cost() + edge_path_operand->estimate_cost();    // [TODO: refine]
    estimated_cost *= 2.0; // Join overhead

    // Output size is typically smaller due to join selectivity
    estimated_output_size = leftmost_operand->estimate_output_size() *
                           edge_path_operand->estimate_output_size() * 0.1; // [TODO: refine]
}

// Constructor with constant col2
TITwoWayOperatorPlan::TITwoWayOperatorPlan(
    std::unique_ptr<Plan> _leftmost_operand,
    std::unique_ptr<Plan> _edge_path_operand,
    VarId col1_var,
    ObjectId constant_col2_value,
    bool epsilon_
) :
    TwoColumnPlan(col1_var, constant_col2_value),
    leftmost_operand  (std::move(_leftmost_operand)),
    edge_path_operand (std::move(_edge_path_operand)),
    epsilon(epsilon_)
{
    // TI Two-Way is simpler than full TI, but still involves join operations
    estimated_cost = leftmost_operand->estimate_cost() + edge_path_operand->estimate_cost();    // [TODO: refine]
    estimated_cost *= 2.0; // Join overhead

    // Output size is typically smaller due to join selectivity
    estimated_output_size = leftmost_operand->estimate_output_size() *
                           edge_path_operand->estimate_output_size() * 0.1; // [TODO: refine]
}

// Constructor with constant col1
TITwoWayOperatorPlan::TITwoWayOperatorPlan(
    std::unique_ptr<Plan> _leftmost_operand,
    std::unique_ptr<Plan> _edge_path_operand,
    ObjectId constant_col1_value,
    VarId col2_var,
    bool epsilon_
) :
    TwoColumnPlan(constant_col1_value, col2_var),
    leftmost_operand  (std::move(_leftmost_operand)),
    edge_path_operand (std::move(_edge_path_operand)),
    epsilon(epsilon_)
{
    // TI Two-Way is simpler than full TI, but still involves join operations
    estimated_cost = leftmost_operand->estimate_cost() + edge_path_operand->estimate_cost();    // [TODO: refine]
    estimated_cost *= 2.0; // Join overhead

    // Output size is typically smaller due to join selectivity
    estimated_output_size = leftmost_operand->estimate_output_size() *
                           edge_path_operand->estimate_output_size() * 0.1; // [TODO: refine]
}

void TITwoWayOperatorPlan::print(std::ostream& os, int indent) const {
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << "TITwoWayOperator(\n";

    leftmost_operand->print(os, indent + 2);
    os << ",\n";
    edge_path_operand->print(os, indent + 2);

    os << "\n";
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << ")";
}

std::set<VarId> TITwoWayOperatorPlan::get_vars() const {
    // Return the variables defined by this TwoColumnPlan
    return get_two_column_vars();
}

void TITwoWayOperatorPlan::set_input_vars(const std::set<VarId>& input_vars) {
    leftmost_operand->set_input_vars(input_vars);
    edge_path_operand->set_input_vars(input_vars);
}

std::unique_ptr<BindingIter> TITwoWayOperatorPlan::get_binding_iter() const {
    // Need to cast operands to TwoColumnBindingIter for TI Two-Way operator
    auto leftmost_iter = leftmost_operand->get_binding_iter();
    auto edge_path_iter = edge_path_operand->get_binding_iter();

    auto two_column_leftmost = dynamic_cast<TwoColumnBindingIter*>(leftmost_iter.release());
    auto two_column_edge_path = dynamic_cast<TwoColumnBindingIter*>(edge_path_iter.release());

    if (!two_column_leftmost || !two_column_edge_path) {
        throw std::runtime_error("TITwoWayOperator requires TwoColumnBindingIter operands");
    }

    // Use the column information from the TwoColumnPlan base class
    if (has_constant_col1) {
        return std::make_unique<CustomOps::TITwoWayOperator>(
            std::unique_ptr<TwoColumnBindingIter>(two_column_leftmost),
            std::unique_ptr<TwoColumnBindingIter>(two_column_edge_path),
            constant_col1_value,
            col2_var,
            epsilon, this->lazy
        );
    } else if (has_constant_col2) {
        return std::make_unique<CustomOps::TITwoWayOperator>(
            std::unique_ptr<TwoColumnBindingIter>(two_column_leftmost),
            std::unique_ptr<TwoColumnBindingIter>(two_column_edge_path),
            col1_var,
            constant_col2_value,
            epsilon, this->lazy
        );
    } else {
        return std::make_unique<CustomOps::TITwoWayOperator>(
            std::unique_ptr<TwoColumnBindingIter>(two_column_leftmost),
            std::unique_ptr<TwoColumnBindingIter>(two_column_edge_path),
            col1_var,
            col2_var,
            epsilon, this->lazy
        );
    }
}