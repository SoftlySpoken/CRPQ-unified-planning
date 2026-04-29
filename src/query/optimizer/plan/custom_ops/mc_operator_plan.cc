#include "mc_operator_plan.h"

#include "query/executor/binding_iter/custom_ops/mc_operator.h"

MCOperatorPlan::MCOperatorPlan(
    std::unique_ptr<Plan> _child_A,
    std::unique_ptr<Plan> _child_B,
    bool _left_epsilon,
    bool _right_epsilon
) :
    child_A      (std::move(_child_A)),
    child_B      (std::move(_child_B)),
    left_epsilon (_left_epsilon),
    right_epsilon(_right_epsilon)
{
    // MC (Merge Closure) involves concatenation + recursive expansions, so cost is high
    estimated_cost = (child_A->estimate_cost() + child_B->estimate_cost()) * 15.0;  // [TODO: refine]
    estimated_output_size = child_A->estimate_output_size() * child_B->estimate_output_size() * 8.0;    // [TODO: refine]
}

void MCOperatorPlan::print(std::ostream& os, int indent) const {
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << "MCOperator(left_eps=" << (left_epsilon ? "true" : "false")
       << ", right_eps=" << (right_epsilon ? "true" : "false") << ",\n";

    child_A->print(os, indent + 2);
    os << ",\n";
    child_B->print(os, indent + 2);

    os << "\n";
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << ")";
}

std::set<VarId> MCOperatorPlan::get_vars() const {
    auto result = child_A->get_vars();
    auto child_B_vars = child_B->get_vars();
    // MC vars should actually be at the two sides of A & B, but let it stay this way for now
    result.insert(child_B_vars.begin(), child_B_vars.end());
    return result;
}

void MCOperatorPlan::set_input_vars(const std::set<VarId>& input_vars) {
    child_A->set_input_vars(input_vars);
    child_B->set_input_vars(input_vars);
}

std::unique_ptr<BindingIter> MCOperatorPlan::get_binding_iter() const {
    // Need to cast children to TwoColumnBindingIter for MC operator
    auto child_A_iter = child_A->get_binding_iter();
    auto child_B_iter = child_B->get_binding_iter();

    auto two_column_A = dynamic_cast<TwoColumnBindingIter*>(child_A_iter.release());
    auto two_column_B = dynamic_cast<TwoColumnBindingIter*>(child_B_iter.release());

    if (!two_column_A || !two_column_B) {
        throw std::runtime_error("MCOperator requires TwoColumnBindingIter children");
    }

    return std::make_unique<CustomOps::MCOperator>(
        std::unique_ptr<TwoColumnBindingIter>(two_column_A),
        std::unique_ptr<TwoColumnBindingIter>(two_column_B),
        left_epsilon,
        right_epsilon
    );
}