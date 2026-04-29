#include "query/executor/binding_iter/two_column_binding_iter.h"

TwoColumnBindingIter::TwoColumnBindingIter(VarId col1_var, VarId col2_var)
    : col1_var(col1_var),
      col2_var(col2_var),
      constant_col1(ObjectId::get_null()),
      constant_col2(ObjectId::get_null()),
      has_constant_col1(false),
      has_constant_col2(false),
      parent_binding_ptr(nullptr),
      in_epsilon_handling(false),
      epsilon_src(UINT64_MAX),
      lazy(false) {
}

TwoColumnBindingIter::TwoColumnBindingIter(ObjectId constant_col1_value, VarId col2_var)
    : col1_var(VarId(UINT32_MAX)),  // col1 is constant, so no variable binding needed
      col2_var(col2_var),
      constant_col1(constant_col1_value),
      constant_col2(ObjectId::get_null()),
      has_constant_col1(true),
      has_constant_col2(false),
      parent_binding_ptr(nullptr),
      in_epsilon_handling(false),
      epsilon_src(UINT64_MAX),
      lazy(false) {
}
TwoColumnBindingIter::TwoColumnBindingIter(VarId _col1_var, ObjectId _constant_col2_value)
    : col1_var(_col1_var),
      col2_var(VarId(UINT32_MAX)),
      constant_col1(ObjectId::get_null()),
      constant_col2(_constant_col2_value),
      has_constant_col1(false),
      has_constant_col2(true),
      parent_binding_ptr(nullptr),
      in_epsilon_handling(false),
      epsilon_src(UINT64_MAX),
      lazy(false) {
}

void TwoColumnBindingIter::_begin(Binding& parent_binding) {
    parent_binding_ptr = &parent_binding;
    reset_iteration_state();
}

bool TwoColumnBindingIter::_epsilon_next() {
    if (!epsilon)
        return false;
    if (!in_epsilon_handling) {
        in_epsilon_handling = true;
        if (has_constant_col1) {
            parent_binding_ptr->add(col2_var, constant_col1);
            return true;
        } else if (has_constant_col2) {
            parent_binding_ptr->add(col1_var, constant_col2);
            return true;
        } else {
            reset_iteration_state();
        }
    }
    if (has_constant_col1 || has_constant_col2)
        return false;
    std::pair<ObjectId, ObjectId> edge;
    while (advance_to_next_edge()) {
        get_current_edge(edge);
        if (edge.first != epsilon_src) {
            epsilon_src = edge.first;
            if (!has_constant_col1)
                parent_binding_ptr->add(col1_var, edge.first);
            parent_binding_ptr->add(col2_var, edge.first);
            return true;
        }
    }
    return false;
}

bool TwoColumnBindingIter::_next() {
    if (!advance_to_next_edge()) {
        return false;
    }

    // Get current edge (col1, col2) from derived class
    std::pair<ObjectId, ObjectId> edge;
    get_current_edge(edge);
    if (edge.first == ObjectId::get_null() || edge.second == ObjectId::get_null()) {
        return false;
    }

    // Assign to binding - only bind variables, not constants
    if (!has_constant_col1) {
        parent_binding_ptr->add(col1_var, edge.first);
    }
    if (!has_constant_col2) {
        parent_binding_ptr->add(col2_var, edge.second);
    }

    return true;
}

void TwoColumnBindingIter::_reset() {
    reset_iteration_state();
}

void TwoColumnBindingIter::assign_nulls() {
    if (parent_binding_ptr) {
        if (!has_constant_col1) {
            parent_binding_ptr->add(col1_var, ObjectId::get_null());
        }
        parent_binding_ptr->add(col2_var, ObjectId::get_null());
    }
}