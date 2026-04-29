#include "union_operator_plan.h"

#include "query/executor/binding_iter/custom_ops/union_operator.h"
#include "query/executor/binding_iter/two_column_binding_iter.h"

UnionOperatorPlan::UnionOperatorPlan(
    std::vector<std::unique_ptr<Plan>> _children,
    VarId _col1_var,
    VarId _col2_var,
    bool _eliminate_duplicates,
    bool _lazy
) :
    TwoColumnPlan(_col1_var, _col2_var),
    eliminate_duplicates(_eliminate_duplicates),
    union_mode(_eliminate_duplicates ? UnionMode::UNION_DISTINCT : UnionMode::UNION_ALL)
{
    lazy = _lazy;
    children.reserve(_children.size());
    estimated_cost = 0.0;
    estimated_output_size = 0.0;

    for (auto& child : _children) {
        estimated_cost += child->estimate_cost();
        estimated_output_size += child->estimate_output_size();
        children.push_back(std::move(child));
    }

    // If eliminating duplicates, add some overhead cost
    if (eliminate_duplicates) {
        estimated_cost *= 1.2; // 20% overhead for duplicate elimination
        estimated_output_size *= 0.8; // Assume some duplicates will be removed
    }
}

UnionOperatorPlan::UnionOperatorPlan(
    std::vector<std::unique_ptr<Plan>> _children,
    ObjectId _constant_col1_value,
    VarId _col2_var,
    bool _eliminate_duplicates,
    bool _lazy
) :
    TwoColumnPlan(_constant_col1_value, _col2_var),
    eliminate_duplicates(_eliminate_duplicates),
    union_mode(_eliminate_duplicates ? UnionMode::UNION_DISTINCT : UnionMode::UNION_ALL)
{
    lazy = _lazy;
    children.reserve(_children.size());
    estimated_cost = 0.0;
    estimated_output_size = 0.0;

    for (auto& child : _children) {
        estimated_cost += child->estimate_cost();
        estimated_output_size += child->estimate_output_size();
        children.push_back(std::move(child));
    }

    // If eliminating duplicates, add some overhead cost
    if (eliminate_duplicates) {
        estimated_cost *= 1.2; // 20% overhead for duplicate elimination
        estimated_output_size *= 0.8; // Assume some duplicates will be removed
    }
}
UnionOperatorPlan::UnionOperatorPlan(
    std::vector<std::unique_ptr<Plan>> _children,
    VarId _col1_var,
    ObjectId _constant_col2_value,
    bool _eliminate_duplicates,
    bool _lazy
) :
    TwoColumnPlan(_col1_var, _constant_col2_value),
    eliminate_duplicates(_eliminate_duplicates),
    union_mode(_eliminate_duplicates ? UnionMode::UNION_DISTINCT : UnionMode::UNION_ALL)
{
    lazy = _lazy;
    children.reserve(_children.size());
    estimated_cost = 0.0;
    estimated_output_size = 0.0;

    for (auto& child : _children) {
        estimated_cost += child->estimate_cost();
        estimated_output_size += child->estimate_output_size();
        children.push_back(std::move(child));
    }

    // If eliminating duplicates, add some overhead cost
    if (eliminate_duplicates) {
        estimated_cost *= 1.2; // 20% overhead for duplicate elimination
        estimated_output_size *= 0.8; // Assume some duplicates will be removed
    }
}

UnionOperatorPlan::UnionOperatorPlan(const UnionOperatorPlan& other) :
    TwoColumnPlan(other),
    eliminate_duplicates(other.eliminate_duplicates),
    union_mode(other.union_mode),
    estimated_cost(other.estimated_cost),
    estimated_output_size(other.estimated_output_size)
{
    children.reserve(other.children.size());
    for (const auto& child : other.children) {
        children.push_back(child->clone());
    }
}

void UnionOperatorPlan::print(std::ostream& os, int indent) const {
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << "UnionOperator" << (eliminate_duplicates ? "Distinct" : "") << "(\n";

    for (size_t i = 0; i < children.size(); ++i) {
        children[i]->print(os, indent + 2);
        if (i < children.size() - 1) {
            os << ",\n";
        }
    }

    os << "\n";
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << ")";
}

std::set<VarId> UnionOperatorPlan::get_vars() const {
    std::set<VarId> result;
    for (const auto& child : children) {
        auto child_vars = child->get_vars();
        result.insert(child_vars.begin(), child_vars.end());
    }
    return result;
}

void UnionOperatorPlan::set_input_vars(const std::set<VarId>& input_vars) {
    for (auto& child : children) {
        child->set_input_vars(input_vars);
    }
}

std::unique_ptr<BindingIter> UnionOperatorPlan::get_binding_iter() const {
    std::vector<std::unique_ptr<TwoColumnBindingIter>> child_iters;
    child_iters.reserve(children.size());

    for (const auto& child : children) {
        auto child_iter = child->get_binding_iter();
        // Cast to TwoColumnBindingIter - this assumes the children are compatible
        auto* two_col_iter = dynamic_cast<TwoColumnBindingIter*>(child_iter.release());
        if (two_col_iter) {
            child_iters.emplace_back(two_col_iter);
        } else {
            // Handle error case - child is not a TwoColumnBindingIter
            throw std::runtime_error("UnionOperatorPlan: child iterator is not a TwoColumnBindingIter");
        }
    }

    if (get_has_constant_col1()) {
        return std::make_unique<CustomOps::UnionOperator>(
            std::move(child_iters),
            get_constant_col1_value(),
            get_col2_var(),
            eliminate_duplicates,
            lazy
        );
    } else if (get_has_constant_col2()) {
        return std::make_unique<CustomOps::UnionOperator>(
            std::move(child_iters),
            get_col1_var(),
            get_constant_col2_value(),
            eliminate_duplicates,
            lazy
        );
    } else {
        return std::make_unique<CustomOps::UnionOperator>(
            std::move(child_iters),
            get_col1_var(),
            get_col2_var(),
            eliminate_duplicates,
            lazy
        );
    }
}

void UnionOperatorPlan::set_union_mode(UnionMode mode) {
    union_mode = mode;
    switch (mode) {
        case UnionMode::UNION_ALL:
            eliminate_duplicates = false;
            break;
        case UnionMode::UNION_DISTINCT:
            eliminate_duplicates = true;
            break;
        case UnionMode::UNION_ORDERED:
            // For now, treat as UNION_ALL
            eliminate_duplicates = false;
            break;
    }
}

void UnionOperatorPlan::set_duplicate_elimination(bool enable) {
    eliminate_duplicates = enable;
    union_mode = enable ? UnionMode::UNION_DISTINCT : UnionMode::UNION_ALL;
}