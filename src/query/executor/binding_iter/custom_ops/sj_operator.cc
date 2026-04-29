#include "query/executor/binding_iter/custom_ops/sj_operator.h"
#include "query/executor/binding_iter/custom_ops/ti_operator.h"
#include "query/executor/binding_iter/two_column_binding_iter.h"
#include <iostream>
#include <algorithm>

namespace CustomOps {

SJOperator::SJOperator(std::unique_ptr<BindingIter> left,
                       std::unique_ptr<BindingIter> right,
                       std::vector<VarId> _left_vars,
                       std::vector<VarId> _right_vars)
    : left_child(std::move(left)),
      right_child(std::move(right)),
      left_vars(std::move(_left_vars)),
      right_vars(std::move(_right_vars)),
      left_exhausted(false),
      hash_table_built(false),
      parent_binding_ptr(nullptr),
      in_enumeration_state(false),
      left_epsilon_added(false),
      right_epsilon_added(false),
      hash_bindings_iter_initialized(false) {
    // epsilon = left_child->epsilon && right_child->epsilon;
    // Take intersection of left_vars & right_vars
    for (const auto& var : left_vars) {
        if (std::find(right_vars.begin(), right_vars.end(), var) != right_vars.end()) {
            join_vars.push_back(var);
        }
    }
}

void SJOperator::_begin(Binding& parent_binding) {
    parent_binding_ptr = &parent_binding;


    // Build hash table from right child if not already built
    if (!hash_table_built) {
        build_hash_table();
        hash_table_built = true;
    }

    // Initialize left child
    left_child->begin(parent_binding);
    left_exhausted = false;
    in_enumeration_state = false;
    hash_table_iter = hash_table.end();
}

bool SJOperator::_next() {
    // If we are currently enumerating matches for a left binding
    if (in_enumeration_state) {
        if (current_matches_iter != current_matches_end) {
            // Copy the right binding values to parent binding
            for (const auto &vid : right_vars)
                parent_binding_ptr->add(vid, (**current_matches_iter)[vid]);
            ++current_matches_iter;
            return true;
        } else {
            // Finished enumerating current matches, try next left binding
            in_enumeration_state = false;
        }
    }

    // Get next left binding and find matches in hash table
    while (!left_exhausted) {
        if (left_child->next()) {
            // Extract join key from left binding
            HashKey join_key = extract_key(*parent_binding_ptr, join_vars);

            // Look for matches in hash table
            auto it = hash_table.find(join_key);
            if (it != hash_table.end() && !it->second.empty()) {
                // Found matches, enter enumeration state
                current_matches_iter = it->second.begin();
                current_matches_end = it->second.end();
                in_enumeration_state = true;

                // Return first match
                for (const auto &vid : right_vars)
                    parent_binding_ptr->add(vid, (**current_matches_iter)[vid]);
                ++current_matches_iter;
                return true;
            }
            // No matches for this left binding, continue to next
        } else {
            left_exhausted = true;
        }
    }

    return false;
}

void SJOperator::_reset() {
    left_exhausted = false;
    in_enumeration_state = false;
    left_epsilon_added = false;
    right_epsilon_added = false;
    hash_bindings_iter_initialized = false;
    left_child->reset();
}

void SJOperator::assign_nulls() {
    left_child->assign_nulls();
    right_child->assign_nulls();
}

void SJOperator::accept_visitor(BindingIterVisitor& visitor) {
    visitor.visit(*this);
}

void SJOperator::build_hash_table() {
    // Create a binding for the right child to operate on
    Binding right_binding(parent_binding_ptr->size);
    right_binding.add_all(*parent_binding_ptr);

    // Initialize right child
    right_child->begin(right_binding);

    // Build hash table by iterating through all right bindings
    while (right_child->next()) {
        // Extract join key from right binding
        HashKey join_key = extract_key(right_binding, join_vars);

        // Create a new binding and copy the right binding data
        auto new_binding = std::make_unique<Binding>(right_binding.size);
        new_binding->add_all(right_binding);
        hash_table[join_key].push_back(std::move(new_binding));
    }
}

SJOperator::HashKey SJOperator::extract_key(const Binding& binding, const std::vector<VarId>& vars) {
    HashKey key;
    key.values.reserve(vars.size());

    for (const auto& var : vars) {
        key.values.push_back(binding[var]);
    }

    return key;
}

} // namespace CustomOps