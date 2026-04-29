#include "query/executor/binding_iter/custom_ops/union_operator.h"
#include <functional>

namespace CustomOps {

bool UnionOperator::DuplicateDetector::check_and_add(uint64_t id1, uint64_t id2) {
    // Create a pair for collision-free duplicate detection
    std::pair<uint64_t, uint64_t> binding_pair(id1, id2);

    if (seen_pairs.find(binding_pair) != seen_pairs.end()) {
        return false; // Duplicate found
    }

    seen_pairs.insert(binding_pair);
    return true; // New binding
}

UnionOperator::UnionOperator(std::vector<std::unique_ptr<TwoColumnBindingIter>> children,
                            VarId col1_var,
                            VarId col2_var,
                             bool eliminate_duplicates,
                             bool _lazy)
    : TwoColumnStoreBindingIter(nullptr, col1_var, col2_var),
      children(std::move(children)),
      global_computation_done(false) {
    lazy = _lazy;
    union_config.eliminate_duplicates = eliminate_duplicates;
    union_config.use_hash_based_dedup = false;

    // estimated_cardinality = 0;
    // for (const auto& child : this->children) {
    //     if (auto custom_child = dynamic_cast<TwoColumnBindingIter*>(child.get())) {
    //         estimated_cardinality += custom_child->get_estimated_cardinality();
    //     }
    // }
}
UnionOperator::UnionOperator(std::vector<std::unique_ptr<TwoColumnBindingIter>> children,
                            ObjectId constant_col1_value,
                            VarId col2_var,
                             bool eliminate_duplicates,
                             bool _lazy)
    : TwoColumnStoreBindingIter(nullptr, constant_col1_value, col2_var),
      children(std::move(children)),
      global_computation_done(false) {
    lazy = _lazy;
    union_config.eliminate_duplicates = eliminate_duplicates;
    union_config.use_hash_based_dedup = false;

    // estimated_cardinality = 0;
    // for (const auto& child : this->children) {
    //     if (auto custom_child = dynamic_cast<TwoColumnBindingIter*>(child.get())) {
    //         estimated_cardinality += custom_child->get_estimated_cardinality();
    //     }
    // }
}
UnionOperator::UnionOperator(std::vector<std::unique_ptr<TwoColumnBindingIter>> children,
    VarId _col1_var,
                            ObjectId _constant_col2_value,
                             bool eliminate_duplicates,
                             bool _lazy)
    : TwoColumnStoreBindingIter(nullptr, _col1_var, _constant_col2_value),
      children(std::move(children)),
      global_computation_done(false) {
    lazy = _lazy;
    union_config.eliminate_duplicates = eliminate_duplicates;
    union_config.use_hash_based_dedup = false;

    // estimated_cardinality = 0;
    // for (const auto& child : this->children) {
    //     if (auto custom_child = dynamic_cast<TwoColumnBindingIter*>(child.get())) {
    //         estimated_cardinality += custom_child->get_estimated_cardinality();
    //     }
    // }
}

void UnionOperator::_begin(Binding& parent_binding) {
    // Initialize store if not already done
    if (!this->get_store()) {
        set_store(std::make_unique<TwoColumnStore>());

        // Reset lazy computation state
        computed_vertices.clear();
        global_computation_done = false;

        // Clear duplicate detector if needed
        if (union_config.eliminate_duplicates) {
            dup_detector.seen_pairs.clear();
        }

        // Conditionally compute union based on lazy flag
        if (!lazy) {
            // Eager computation: materialize all results immediately (current behavior)
            compute_full_union();
            this->get_store()->compact();
        }
        // Compact the store for efficient iteration
        this->get_store()->compact();
    }

    // Call parent's _begin to initialize iteration over the materialized store
    TwoColumnStoreBindingIter::_begin(parent_binding);
}

void UnionOperator::assign_nulls() {
    // Union operator doesn't introduce new variables,
    // and since we've materialized all results, we delegate to the parent
    // which handles the materialized store iteration
    TwoColumnBindingIter::assign_nulls();
}

void UnionOperator::accept_visitor(BindingIterVisitor& visitor) {
    visitor.visit(*this);
}

void UnionOperator::set_duplicate_elimination(bool enable) {
    union_config.eliminate_duplicates = enable;
    if (enable) {
        dup_detector.seen_pairs.clear();
    }
}

void UnionOperator::add_child(std::unique_ptr<TwoColumnBindingIter> child) {
    children.push_back(std::move(child));

    // Update estimated cardinality
    // if (auto custom_child = dynamic_cast<CustomOperatorBase*>(children.back().get())) {
    //     estimated_cardinality += custom_child->get_estimated_cardinality();
    // }
}

void UnionOperator::set_union_mode(UnionMode mode) {
    switch (mode) {
        case UnionMode::UNION_ALL:
            union_config.eliminate_duplicates = false;
            break;
        case UnionMode::UNION_DISTINCT:
            union_config.eliminate_duplicates = true;
            break;
    }
}

size_t UnionOperator::get_duplicates_eliminated() const {
    if (union_config.eliminate_duplicates) {
        // Return the number of unique pairs seen
        // Note: This represents total unique pairs, not necessarily duplicates eliminated
        // To get actual duplicates eliminated, we'd need to track both seen and total counts
        return dup_detector.seen_pairs.size();
    }
    return 0;
}

std::pair<const uint64_t*, size_t> UnionOperator::get_neighbors(uint64_t vid) const {
    if (!lazy || global_computation_done) {
        // Eager mode or global computation already done: use existing materialized results
        return TwoColumnStoreBindingIter::get_neighbors(vid);
    }

    // Lazy mode: check if computation is needed for this vertex
    if (computed_vertices.find(vid) == computed_vertices.end()) {
        // Need to compute union results for this vertex
        compute_union_for_vertex(vid);
    }

    // Return the neighbors after computation
    return TwoColumnStoreBindingIter::get_neighbors(vid);
}

bool UnionOperator::seek_to_vertex(uint64_t vertex) {
    if (!lazy || global_computation_done) {
        // Eager mode or global computation already done: use existing materialized results
        return TwoColumnStoreBindingIter::seek_to_vertex(vertex);
    }

    // Lazy mode: check if computation is needed for this vertex
    if (computed_vertices.find(vertex) == computed_vertices.end()) {
        // Need to compute union results for this vertex
        compute_union_for_vertex(vertex);
    }
    return TwoColumnStoreBindingIter::seek_to_vertex(vertex);
}

void UnionOperator::compute_union_for_vertex(uint64_t vertex) const {
    // Mark vertex as being computed
    computed_vertices.insert(vertex);
    if (has_constant_col1 && vertex != constant_col1.id) {
        return;
    }

    // Need to access the parent binding for child iteration
    // We'll create a temporary binding with the right size
    // First, find the maximum variable id we'll need
    int max_var_id = -1;
    if (!has_constant_col1) {
        max_var_id = std::max(max_var_id, int(col1_var.id));
    }
    if (!has_constant_col2) {
        max_var_id = std::max(max_var_id, int(col2_var.id));
    }

    // Determine max variable ID across all children
    for (const auto& child : children) {
        if (!child->get_has_constant_col1()) {
            int child_col1_id = child->get_col1_var().id;
            max_var_id = std::max(max_var_id, child_col1_id);
        }
        if (!child->get_has_constant_col2()) {
            int child_col2_id = child->get_col2_var().id;
            max_var_id = std::max(max_var_id, child_col2_id);
        }
    }

    Binding temp_binding(max_var_id + 1);

    // Collect results from all children for this specific vertex
    for (auto& child : children) {
        // If we have constant col1, only process if it matches the requested vertex
        if ((has_constant_col1 && child->get_has_constant_col1() && constant_col1 != child->get_constant_col1())
        || (has_constant_col2 && child->get_has_constant_col2() && constant_col2 != child->get_constant_col2())) {
            continue;
        }
        child->begin(temp_binding);

        if (child->is_lazy())
            child->get_neighbors(vertex);
        child->seek_to_vertex(vertex);

        // For vertex-specific computation, we need to seek to the vertex if possible
        // or filter during iteration
        bool has_next = false;
        while (child->next()) {
            uint64_t col1_id, col2_id;

            if (child->get_has_constant_col1()) {
                col1_id = child->get_constant_col1().id;
                col2_id = temp_binding[child->get_col2_var()].id;
            } else if (child->get_has_constant_col2()) {
                col1_id = temp_binding[child->get_col1_var()].id;
                col2_id = child->get_constant_col2().id;
            } else {
                col1_id = temp_binding[child->get_col1_var()].id;
                col2_id = temp_binding[child->get_col2_var()].id;

                // Skip if this edge doesn't originate from requested vertex
                if (col1_id != vertex || (has_constant_col1 && col1_id != constant_col1.id)
                || (has_constant_col2 && col2_id != constant_col2.id)) {
                    continue;
                } else {
                    has_next = true;
                }
            }

            // Apply duplicate elimination if needed
            if (union_config.eliminate_duplicates) {
                if (!const_cast<DuplicateDetector&>(dup_detector).check_and_add(col1_id, col2_id)) {
                    continue; // Skip duplicate
                }
            }

            // Add to store using const_cast to modify from const method
            const_cast<UnionOperator*>(this)->get_store()->append(col1_id, col2_id);
        }
        if (child->get_epsilon() && has_next) {
            if (!child->get_has_constant_col2() || child->get_constant_col2().id == vertex) {
                if (union_config.eliminate_duplicates) {
                    if (!const_cast<DuplicateDetector&>(dup_detector).check_and_add(vertex, vertex)) {
                        continue; // Skip duplicate
                    }
                }
                const_cast<UnionOperator*>(this)->get_store()->append(vertex, vertex);
            }
        }
    }
}

void UnionOperator::compute_full_union() const {
    global_computation_done = true;

    // Need to access the parent binding for child iteration
    // We'll create a temporary binding with the right size
    int max_var_id = -1;
    if (!has_constant_col1) {
        max_var_id = std::max(max_var_id, int(col1_var.id));
    }
    if (!has_constant_col2) {
        max_var_id = std::max(max_var_id, int(col2_var.id));
    }

    // Determine max variable ID across all children
    for (const auto& child : children) {
        if (!child->get_has_constant_col1()) {
            int child_col1_id = child->get_col1_var().id;
            max_var_id = std::max(max_var_id, child_col1_id);
        }
        if (!child->get_has_constant_col2()) {
            int child_col2_id = child->get_col2_var().id;
            max_var_id = std::max(max_var_id, child_col2_id);
        }
    }

    Binding temp_binding(max_var_id + 1);

    // Materialize all results from all children (existing eager logic)
    for (auto& child : children) {
        if ((has_constant_col1 && child->get_has_constant_col1() && constant_col1 != child->get_constant_col1())
        || (has_constant_col2 && child->get_has_constant_col2() && constant_col2 != child->get_constant_col2())) {
            continue;
        }
        child->begin(temp_binding);

        uint64_t prev_col1_id = UINT64_MAX;
        while (child->next()) {
            // Extract the column values and add to store
            uint64_t col1_id, col2_id;
            if (child->get_has_constant_col1()) {
                col1_id = child->get_constant_col1().id;
                col2_id = temp_binding[child->get_col2_var()].id;
            } else if (child->get_has_constant_col2()) {
                col1_id = temp_binding[child->get_col1_var()].id;
                col2_id = child->get_constant_col2().id;
            } else {
                col1_id = temp_binding[child->get_col1_var()].id;
                col2_id = temp_binding[child->get_col2_var()].id;
            }
            if ((has_constant_col1 && col1_id != constant_col1.id) || (has_constant_col2 && col2_id != constant_col2.id)) {
                continue;
            }

            // Apply duplicate elimination if needed
            if (union_config.eliminate_duplicates) {
                if (!const_cast<DuplicateDetector&>(dup_detector).check_and_add(col1_id, col2_id)) {
                    continue; // Skip duplicate
                }
            }

            // Add to store using const_cast to modify from const method
            const_cast<UnionOperator*>(this)->get_store()->append(col1_id, col2_id);
            
            if (child->get_epsilon() && col1_id != prev_col1_id) {
                if (!child->get_has_constant_col2() || child->get_constant_col2().id == col1_id) {
                    if (union_config.eliminate_duplicates) {
                        if (!const_cast<DuplicateDetector&>(dup_detector).check_and_add(col1_id, col1_id)) {
                            continue; // Skip duplicate
                        }
                    }
                    const_cast<UnionOperator*>(this)->get_store()->append(col1_id, col1_id);
                    prev_col1_id = col1_id;
                }
            }
        }
        if (child->get_has_constant_col2()) {
            uint64_t child_const_col2_id = child->get_constant_col2().id;
            if (union_config.eliminate_duplicates) {
                if (!const_cast<DuplicateDetector&>(dup_detector).check_and_add(child_const_col2_id, child_const_col2_id)) {
                    continue; // Skip duplicate
                }
            }
            const_cast<UnionOperator*>(this)->get_store()->append(child_const_col2_id, child_const_col2_id);
        }
    }
}

} // namespace CustomOps