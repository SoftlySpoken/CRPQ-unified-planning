#include "mc_operator.h"
#include "query/executor/binding_iter_visitor.h"
#include <algorithm>
#include <iostream>

namespace CustomOps {

MCOperator::MCOperator(std::unique_ptr<TwoColumnBindingIter> _child_A,
                       std::unique_ptr<TwoColumnBindingIter> _child_B,
                       bool _left_epsilon,
                       bool _right_epsilon)
    : TwoColumnStoreBindingIter(nullptr,
                                _child_A->get_col1_var(),
                                _child_B->get_col2_var())
    , child_A(std::move(_child_A))
    , child_B(std::move(_child_B))
    , current_phase(ProcessingPhase::INITIAL_CONCATENATION)
    , expansion_changed(false)
    , left_epsilon(_left_epsilon)
    , right_epsilon(_right_epsilon) {
    // Initialize result stores
    initial_results = std::make_unique<TwoColumnStore>();
    left_expanded = std::make_unique<TwoColumnStore>();
    final_results = std::make_unique<TwoColumnStore>();

    epsilon = _left_epsilon && _right_epsilon;

    if (child_A->get_has_constant_col1()) {
        this->has_constant_col1 = true;
        this->constant_col1 = child_A->get_constant_col1();
    }
}

void MCOperator::_begin(Binding& parent_binding) {
    if (!this->get_store()) {
        // Reset state for fresh computation
        current_phase = ProcessingPhase::INITIAL_CONCATENATION;
        expansion_changed = false;
        seen_pairs.clear();
    
        // Clear result stores
        initial_results->clear();
        left_expanded->clear();
        final_results->clear();
    
        // Initialize bindings with the same size as parent binding and copy data
        parent_binding_ptr = &parent_binding;
        binding_A = std::make_unique<Binding>(parent_binding.size);
        binding_B = std::make_unique<Binding>(parent_binding.size);
        binding_A->add_all(parent_binding);
        binding_B->add_all(parent_binding);
    
        // Begin children with their respective bindings
        child_A->begin(*binding_A);
        child_B->begin(*binding_B);
    
        // Perform all MC computation phases
        compute_initial_concatenation();
        current_phase = ProcessingPhase::LEFT_EXPANSION;
        expand_left_recursively();
        current_phase = ProcessingPhase::RIGHT_EXPANSION;
        expand_right_recursively();
    
        // Set the final results as our store
        set_store(std::make_unique<TwoColumnStore>(*final_results));
    }

    // Call parent's _begin to initialize iteration over our result store
    TwoColumnStoreBindingIter::_begin(parent_binding);
}


void MCOperator::_reset() {
    if (child_A) child_A->reset();
    if (child_B) child_B->reset();

    // Call parent's reset
    TwoColumnStoreBindingIter::_reset();

    current_phase = ProcessingPhase::INITIAL_CONCATENATION;
    expansion_changed = false;
    seen_pairs.clear();

    // Clear result stores
    if (initial_results)
        initial_results->clear();
    else
        initial_results = std::make_unique<TwoColumnStore>();
    if (left_expanded)
        left_expanded->clear();
    else
        left_expanded = std::make_unique<TwoColumnStore>();
    if (final_results)
        final_results->clear();
    else
        final_results = std::make_unique<TwoColumnStore>();
}

void MCOperator::accept_visitor(BindingIterVisitor& visitor) {
    visitor.visit(*this);
}

void MCOperator::compute_initial_concatenation() {
    // Phase 1: Compute A/B (concatenation)
    // For each edge (s1, t1) in A and each edge (s2, t2) in B where t1 == s2,
    // produce result edge (s1, t2)

    if (left_epsilon || right_epsilon) {
        if (left_epsilon) {
            // Iterate through all edges in child_B
            while (child_B->next()) {
                uint64_t source = child_B->get_col1_id(*binding_B);
                uint64_t target = (*binding_B)[child_B->get_col2_var()].id;
                initial_results->append(source, target);
                seen_pairs.insert({source, target});
            }
        }
        if (right_epsilon) {
            // Iterate through all edges in child_A
            while (child_A->next()) {
                uint64_t source = child_A->get_col1_id(*binding_A);
                uint64_t target = (*binding_A)[child_A->get_col2_var()].id;
                initial_results->append(source, target);
                seen_pairs.insert({source, target});
            }
        }
    } else {
        initial_results = std::make_unique<TwoColumnStore>();

        // For each edge in A
        while (child_A->next()) {
            uint64_t source_A = child_A->get_col1_id(*binding_A);
            uint64_t target_A = (*binding_A)[child_A->get_col2_var()].id;

            // For each edge in B
            child_B->reset();
            binding_B->add_all(*parent_binding_ptr);
            child_B->begin(*binding_B);
            child_B->seek_to_vertex(target_A);

            while (child_B->next()) {
                uint64_t source_B = child_B->get_col1_id(*binding_B);
                uint64_t target_B = (*binding_B)[child_B->get_col2_var()].id;

                // Check if target_A == source_B (join condition)
                if (target_A == source_B) {
                    initial_results->append(source_A, target_B);
                    seen_pairs.insert({source_A, target_B});
                } else
                    break;
            }
        }
    }
}

void MCOperator::expand_left_recursively() {
    // Phase 2: Expand left A's recursively to get A^+/B
    // Keep expanding by concatenating A with current results until no new pairs are found

    left_expanded = std::move(initial_results);

    bool changed = true;
    while (changed) {
        changed = false;

        // Create temporary store for new results in this iteration
        auto temp_results = std::make_unique<TwoColumnStore>();

        // For each edge in A
        child_A->reset();
        child_A->begin(*binding_A);

        while (child_A->next()) {
            uint64_t source_A = child_A->get_col1_id(*binding_A);
            uint64_t target_A = (*binding_A)[child_A->get_col2_var()].id;

            // Find all edges in left_expanded where source = target_A
            auto neighbors = left_expanded->get_neighbors(target_A);
            const uint64_t* neighbor_list = neighbors.first;
            size_t neighbor_count = neighbors.second;

            for (size_t i = 0; i < neighbor_count; ++i) {
                uint64_t target_current = neighbor_list[i];

                if (!is_pair_seen(source_A, target_current)) {
                    temp_results->append(source_A, target_current);
                    seen_pairs.insert({source_A, target_current});
                    changed = true;
                }
            }
        }

        // Add new results to left_expanded
        if (changed) {
            for (auto it = temp_results->begin(); it != temp_results->end(); ++it) {
                auto edge = *it;
                left_expanded->append(edge.first, edge.second);
            }
        }
    }
}

void MCOperator::expand_right_recursively() {
    // Phase 3: Expand right B's recursively to get A^+/B^+
    // Keep expanding by concatenating current results with B until no new pairs are found

    final_results = std::move(left_expanded);

    bool changed = true;
    while (changed) {
        changed = false;

        // Create temporary store for new results in this iteration
        auto temp_results = std::make_unique<TwoColumnStore>();

        // For each edge in current final_results
        for (auto it_current = final_results->begin(); it_current != final_results->end(); ++it_current) {
            auto current_edge = *it_current;
            uint64_t source_current = current_edge.first;
            uint64_t target_current = current_edge.second;

            // For each edge in B
            child_B->reset();
            child_B->begin(*binding_B);
            child_B->seek_to_vertex(target_current);

            while (child_B->next()) {
                uint64_t source_B = child_B->get_col1_id(*binding_B);
                uint64_t target_B = (*binding_B)[child_B->get_col2_var()].id;

                // Check if target_current == source_B (join condition)
                if (target_current == source_B) {
                    if (!is_pair_seen(source_current, target_B)) {
                        temp_results->append(source_current, target_B);
                        seen_pairs.insert({source_current, target_B});
                        changed = true;
                    }
                } else
                    break;
            }
        }

        // Add new results to final_results
        if (changed) {
            for (auto it = temp_results->begin(); it != temp_results->end(); ++it) {
                auto edge = *it;
                final_results->append(edge.first, edge.second);
            }
        }
    }
}

bool MCOperator::is_pair_seen(uint64_t first, uint64_t second) {
    return seen_pairs.find({first, second}) != seen_pairs.end();
}


size_t MCOperator::get_current_phase_size() const {
    switch (current_phase) {
        case ProcessingPhase::INITIAL_CONCATENATION:
            return initial_results ? initial_results->size() : 0;
        case ProcessingPhase::LEFT_EXPANSION:
            return left_expanded ? left_expanded->size() : 0;
        case ProcessingPhase::RIGHT_EXPANSION:
            return final_results ? final_results->size() : 0;
    }
    return 0;
}

} // namespace CustomOps