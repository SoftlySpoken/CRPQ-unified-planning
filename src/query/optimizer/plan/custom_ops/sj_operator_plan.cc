#include "sj_operator_plan.h"

#include "query/executor/binding_iter/custom_ops/sj_operator.h"

SJOperatorPlan::SJOperatorPlan(
    std::unique_ptr<Plan> _left_child,
    std::unique_ptr<Plan> _right_child,
    std::vector<VarId> _left_vars,
    std::vector<VarId> _right_vars
) :
    left_child       (std::move(_left_child)),
    right_child      (std::move(_right_child)),
    left_vars   (std::move(_left_vars)),
    right_vars  (std::move(_right_vars))
{
    // SJ (Subgraph Join) is similar to hash join
    estimated_cost = left_child->estimate_cost() + right_child->estimate_cost();
    estimated_cost *= 1.5; // Hash table build and probe overhead

    // Join selectivity estimation based on join variables
    double selectivity = 1.0 / (left_vars.size() + 1); // More join vars = more selective
    estimated_output_size = left_child->estimate_output_size() *
                           right_child->estimate_output_size() * selectivity;
}

SJOperatorPlan::SJOperatorPlan(const SJOperatorPlan& other) :
    Plan(other),
    left_child        (other.left_child->clone()),
    right_child       (other.right_child->clone()),
    left_vars    (other.left_vars),
    right_vars   (other.right_vars),
    estimated_cost    (other.estimated_cost),
    estimated_output_size (other.estimated_output_size)
{
}

void SJOperatorPlan::print(std::ostream& os, int indent) const {
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << "SJOperator(left_vars=[";
    for (size_t i = 0; i < left_vars.size(); ++i) {
        os << left_vars[i].id;
        if (i < left_vars.size() - 1) os << ",";
    }
    os << "], right_vars=[";
    for (size_t i = 0; i < right_vars.size(); ++i) {
        os << right_vars[i].id;
        if (i < right_vars.size() - 1) os << ",";
    }
    os << "],\n";

    left_child->print(os, indent + 2);
    os << ",\n";
    right_child->print(os, indent + 2);

    os << "\n";
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << ")";
}

std::set<VarId> SJOperatorPlan::get_vars() const {
    auto result = left_child->get_vars();
    auto right_vars = right_child->get_vars();
    result.insert(right_vars.begin(), right_vars.end());
    return result;
}

void SJOperatorPlan::set_input_vars(const std::set<VarId>& input_vars) {
    left_child->set_input_vars(input_vars);
    right_child->set_input_vars(input_vars);
}

std::unique_ptr<BindingIter> SJOperatorPlan::get_binding_iter() const {
    auto left_iter = left_child->get_binding_iter();
    auto right_iter = right_child->get_binding_iter();

    return std::make_unique<CustomOps::SJOperator>(
        std::move(left_iter),
        std::move(right_iter),
        left_vars,
        right_vars
    );
}