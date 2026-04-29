#include "kc_operator_plan.h"

#include "query/executor/binding_iter/custom_ops/kc_operator.h"

// Constructor with variable col1
KCOperatorPlan::KCOperatorPlan(
    std::unique_ptr<Plan> _child,
    VarId _col1_var,
    VarId _col2_var,
    bool _epsilon,
    bool _lazy
) :
    TwoColumnPlan(_col1_var, _col2_var),
    child     (std::move(_child))
{
    // KC (Kleene Closure) involves fix-point computation, so cost is higher
    lazy = _lazy;
    epsilon = _epsilon;
    estimated_cost = child->estimate_cost() * 10.0; // Rough estimate for fix-point iterations. [TODO: refine]
    estimated_output_size = child->estimate_output_size() * 5.0; // Can expand significantly    [TODO: refine]
}

// Constructor with constant col1
KCOperatorPlan::KCOperatorPlan(
    std::unique_ptr<Plan> _child,
    ObjectId _constant_col1_value,
    VarId _col2_var,
    bool _epsilon,
    bool _lazy
) :
    TwoColumnPlan(_constant_col1_value, _col2_var),
    child     (std::move(_child))
{
    // KC (Kleene Closure) involves fix-point computation, so cost is higher
    lazy = _lazy;
    epsilon = _epsilon;
    estimated_cost = child->estimate_cost() * 10.0; // Rough estimate for fix-point iterations. [TODO: refine]
    estimated_output_size = child->estimate_output_size() * 5.0; // Can expand significantly    [TODO: refine]
}
// Constructor with constant col2
KCOperatorPlan::KCOperatorPlan(
    std::unique_ptr<Plan> _child,
    VarId _col1_var,
    ObjectId _constant_col2_value,
    bool _epsilon,
    bool _lazy
) :
    TwoColumnPlan(_col1_var, _constant_col2_value),
    child     (std::move(_child))
{
    // KC (Kleene Closure) involves fix-point computation, so cost is higher
    lazy = _lazy;
    epsilon = _epsilon;
    estimated_cost = child->estimate_cost() * 10.0; // Rough estimate for fix-point iterations. [TODO: refine]
    estimated_output_size = child->estimate_output_size() * 5.0; // Can expand significantly    [TODO: refine]
}

void KCOperatorPlan::print(std::ostream& os, int indent) const {
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    if (get_has_constant_col1()) {
        os << "KCOperator(col1=" << get_constant_col1_value().id << " (constant), col2=" << get_col2_var().id
           << ", epsilon=" << (epsilon ? "true" : "false") << ", lazy=" << (lazy ? "true" : "false") << ",\n";
    } else {
        os << "KCOperator(col1=" << get_col1_var().id << ", col2=" << get_col2_var().id
           << ", epsilon=" << (epsilon ? "true" : "false") << ", lazy=" << (lazy ? "true" : "false") << ",\n";
    }
    child->print(os, indent + 2);
    os << "\n";
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << ")";
}

std::set<VarId> KCOperatorPlan::get_vars() const {
    return get_two_column_vars();
}

void KCOperatorPlan::set_input_vars(const std::set<VarId>& input_vars) {
    child->set_input_vars(input_vars);
}

std::unique_ptr<BindingIter> KCOperatorPlan::get_binding_iter() const {
    // Need to cast child to TwoColumnBindingIter for KC operator
    auto child_iter = child->get_binding_iter();
    auto two_column_iter = dynamic_cast<TwoColumnBindingIter*>(child_iter.release());

    if (!two_column_iter) {
        throw std::runtime_error("KCOperator requires TwoColumnBindingIter child");
    }

    if (get_has_constant_col1()) {
        return std::make_unique<CustomOps::KCOperator>(
            std::unique_ptr<TwoColumnBindingIter>(two_column_iter),
            get_constant_col1_value(),
            get_col2_var(),
            epsilon,
            lazy
        );
    } else if (get_has_constant_col2()) {
        return std::make_unique<CustomOps::KCOperator>(
            std::unique_ptr<TwoColumnBindingIter>(two_column_iter),
            get_col1_var(),
            get_constant_col2_value(),
            epsilon,
            lazy
        );
    } else {
        return std::make_unique<CustomOps::KCOperator>(
            std::unique_ptr<TwoColumnBindingIter>(two_column_iter),
            get_col1_var(),
            get_col2_var(),
            epsilon,
            lazy
        );
    }
}