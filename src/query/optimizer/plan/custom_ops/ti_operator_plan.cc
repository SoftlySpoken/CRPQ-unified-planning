#include "ti_operator_plan.h"

#include "query/executor/binding_iter/custom_ops/ti_operator.h"

TIOperatorPlan::TIOperatorPlan(
    std::unique_ptr<Plan> _leftmost_operand,
    std::vector<std::unique_ptr<Plan>> _edge_path_operands,
    bool _intersect_in_leftmost
) :
    leftmost_operand(std::move(_leftmost_operand)),
    intersect_in_leftmost(_intersect_in_leftmost)
{
    edge_path_operands.reserve(_edge_path_operands.size());
    estimated_cost = leftmost_operand->estimate_cost();
    estimated_output_size = leftmost_operand->estimate_output_size();

    for (auto& operand : _edge_path_operands) {
        estimated_cost += operand->estimate_cost(); // [TODO: refine]
        // TI involves multi-way intersection, so output tends to be selective
        estimated_output_size *= 0.1; // Conservative selectivity estimate  [TODO: refine]
        edge_path_operands.push_back(std::move(operand));
    }

    // TI has intersection overhead
    estimated_cost *= 3.0;  // [TODO: refine]
}

TIOperatorPlan::TIOperatorPlan(const TIOperatorPlan& other) :
    Plan(other),
    leftmost_operand    (other.leftmost_operand->clone()),
    estimated_cost      (other.estimated_cost),
    estimated_output_size (other.estimated_output_size),
    intersect_in_leftmost (other.intersect_in_leftmost)
{
    edge_path_operands.reserve(other.edge_path_operands.size());
    for (const auto& operand : other.edge_path_operands) {
        edge_path_operands.push_back(operand->clone());
    }
}

void TIOperatorPlan::print(std::ostream& os, int indent) const {
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << "TIOperator(leftmost=\n";
    leftmost_operand->print(os, indent + 2);

    if (!edge_path_operands.empty()) {
        os << ",\n";
        for (int i = 0; i < indent + 2; ++i) {
            os << ' ';
        }
        os << "edge_paths=[\n";

        for (size_t i = 0; i < edge_path_operands.size(); ++i) {
            edge_path_operands[i]->print(os, indent + 4);
            if (i < edge_path_operands.size() - 1) {
                os << ",\n";
            }
        }

        os << "\n";
        for (int i = 0; i < indent + 2; ++i) {
            os << ' ';
        }
        os << "]";
    }

    os << "\n";
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << ")";
}

std::set<VarId> TIOperatorPlan::get_vars() const {
    auto result = leftmost_operand->get_vars();

    for (const auto& operand : edge_path_operands) {
        auto operand_vars = operand->get_vars();
        result.insert(operand_vars.begin(), operand_vars.end());
    }

    return result;
}

void TIOperatorPlan::set_input_vars(const std::set<VarId>& input_vars) {
    leftmost_operand->set_input_vars(input_vars);

    for (auto& operand : edge_path_operands) {
        operand->set_input_vars(input_vars);
    }
}

std::unique_ptr<BindingIter> TIOperatorPlan::get_binding_iter() const {
    auto leftmost_iter = leftmost_operand->get_binding_iter();

    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_path_iters;
    edge_path_iters.reserve(edge_path_operands.size());

    for (const auto& operand : edge_path_operands) {
        auto iter = operand->get_binding_iter();
        auto two_column_iter = dynamic_cast<TwoColumnBindingIter*>(iter.release());

        if (!two_column_iter) {
            throw std::runtime_error("TIOperator edge/path operands require TwoColumnBindingIter");
        }

        edge_path_iters.emplace_back(two_column_iter);
    }

    return std::make_unique<CustomOps::TIOperator>(
        std::move(leftmost_iter),
        std::move(edge_path_iters),
        intersect_in_leftmost
    );
}