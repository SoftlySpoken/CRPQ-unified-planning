#include "query/executor/binding_iter/custom_ops/kc_operator.h"
#include <iostream>
#include <chrono>

namespace CustomOps {

KCOperator::KCOperator(std::unique_ptr<TwoColumnBindingIter> child,
                       VarId col1_var,
                       VarId col2_var,
                       bool _epsilon,
                       bool _lazy)
    : TwoColumnStoreBindingIter(nullptr, col1_var, col2_var),
      child(std::move(child)),
      fix_point_reached(false),
      initialization_complete(false),
      iterations_count(0),
      global_compute_fix_point_called(false){
    lazy = _lazy;
    epsilon = _epsilon;
}

KCOperator::KCOperator(std::unique_ptr<TwoColumnBindingIter> child,
                       ObjectId constant_col1_value,
                       VarId col2_var,
                       bool _epsilon,
                       bool _lazy)
    : TwoColumnStoreBindingIter(nullptr, constant_col1_value, col2_var),
      child(std::move(child)),
      fix_point_reached(false),
      initialization_complete(false),
      iterations_count(0),
      global_compute_fix_point_called(false){
    lazy = _lazy;
    epsilon = _epsilon;
}
KCOperator::KCOperator(std::unique_ptr<TwoColumnBindingIter> child,
    VarId col1_var,
                       ObjectId constant_col2_value,
                       bool _epsilon,
                       bool _lazy)
    : TwoColumnStoreBindingIter(nullptr, col1_var, constant_col2_value),
      child(std::move(child)),
      fix_point_reached(false),
      initialization_complete(false),
      iterations_count(0),
      global_compute_fix_point_called(false){
    lazy = _lazy;
    epsilon = _epsilon;
}

void KCOperator::_begin(Binding& parent_binding) {
    if (!this->get_store()) {
        // Initialize this->store as empty TwoColumnStore
        set_store(std::make_unique<TwoColumnStore>());

        // Reset state
        seen_edges.clear();
        fix_point_reached = false;
        initialization_complete = false;
        iterations_count = 0;
        global_compute_fix_point_called = false;
        vertex_specific_compute_fix_point_called.clear();

        // Conditionally compute the fix-point based on lazy flag
        if (!lazy) {
            // Eager computation: compute fix-point immediately as before
            if (has_constant_col1) {
                compute_fix_point(constant_col1);
            } else {
                compute_fix_point();
            }

            std::cout << "this->store size = " << this->get_store()->size() << std::endl;

            // Compact the store for efficient iteration
            this->get_store()->compact();
        } else {
            // Lazy computation: defer fix-point computation until get_neighbors calls
            std::cout << "Lazy mode enabled: fix-point computation deferred until get_neighbors calls" << std::endl;
        }
    }

    // Call parent's _begin to initialize iteration over this->store
    TwoColumnStoreBindingIter::_begin(parent_binding);
    initialization_complete = true;
}

void KCOperator::compute_fix_point(ObjectId starting_vertex) {
    // Track the compute_fix_point invocation
    if (starting_vertex.is_null()) {
        global_compute_fix_point_called = true;
    } else {
        vertex_specific_compute_fix_point_called.insert(starting_vertex.id);
    }

    // Initialize with the original child results
    TwoColumnStore current_store;

    // Collect all initial edges from child iterator
    if (this->has_constant_col1 && child->get_has_constant_col1() \
    && this->constant_col1 != child->get_constant_col1()) {
        std::cout << "KCOperator: this and child constant_col1 do not match" << std::endl;
        return;
    }
    auto start_time = std::chrono::high_resolution_clock::now();
    int max_var_id = child->get_col2_var().id;
    if (!child->get_has_constant_col1()) {
        int col1_var_id = child->get_col1_var().id;
        if ((max_var_id < col1_var_id))
            max_var_id = col1_var_id;
    }
    Binding temp_binding(max_var_id + 1);
    child->begin(temp_binding);
    
    uint64_t edge_count = 0;
    // if (has_constant_col1) {
    //     child->seek_to_vertex(constant_col1.id);
    // }
    if (!starting_vertex.is_null()) {
        child->seek_to_vertex(starting_vertex.id);
    }
    while (child->next()) {
        uint64_t source = child->get_has_constant_col1() ? child->get_constant_col1().id : temp_binding[child->get_col1_var()].id;
        if (!starting_vertex.is_null() && source != starting_vertex.id)
            break;
        uint64_t target = temp_binding[child->get_col2_var()].id;
        if (is_edge_new(source, target)) {
            current_store.append(source, target);
            if (!has_constant_col2 || target == constant_col2.id) {
                this->get_store()->append(source, target);
            }
        }
        edge_count++;
    }
    auto current_time = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
    // std::cout << "Collect all initial edges from child iterator, elapsed time: "
    //             << elapsed.count() << " ms, edge_count = " << edge_count << std::endl;

    iterations_count = 1;

    // Iterate until fix-point is reached
    while (!current_store.empty()) {
        TwoColumnStore next_store;

        // Perform self-join iteration: join current_store with this->store
        // where target of first relation = source of second relation
        perform_self_join_iteration(current_store, next_store, temp_binding, starting_vertex);

        // If no new edges were generated, we've reached the fix-point
        if (next_store.empty()) {
            fix_point_reached = true;
            break;
        }

        // Add new edges to this->store
        for (auto it = next_store.begin(); it != next_store.end(); ++it) {
            auto edge = *it;
            if (!has_constant_col2 || edge.second == constant_col2.id) {
                this->get_store()->append(edge.first, edge.second);
            }
        }

        // Move to next iteration
        current_store = std::move(next_store);
        iterations_count++;

        // Safety check to prevent infinite loops (configurable limit)
        if (iterations_count > 1000) {
            std::cout << "Possible infinite loop!" << std::endl;
            fix_point_reached = true;
            break;
        }
    }
}


void KCOperator::perform_self_join_iteration(TwoColumnStore& current_store, TwoColumnStore& next_store, Binding &_temp_binding, ObjectId starting_vertex) {
    // Start timer before the for loop
    auto start_time = std::chrono::high_resolution_clock::now();
    size_t edges_appended = 0;

    // For each edge (s,t) in current_store
    for (auto current_it = current_store.begin(); current_it != current_store.end(); ++current_it) {
        auto current_edge = *current_it;
        uint64_t current_source = current_edge.first;
        uint64_t current_target = current_edge.second;

        // if (has_constant_col1 && current_source != constant_col1.id)
        //     continue;

        // Find all edges in the child iterator where source = current_target
        // This implements the "target=source" join condition
        child->seek_to_vertex(current_target);
        while (child->next()) {
            uint64_t source_child = child->get_col1_id(_temp_binding);
            uint64_t target_child = _temp_binding[child->get_col2_var()].id;

            // Check if target_A == source_B (join condition)
            if (current_target == source_child) {
                if (is_edge_new(current_source, target_child)) {
                    next_store.append(current_source, target_child);
                    edges_appended++;
                }

                // Log timing information every 100 edges
                if (edges_appended % 10000 == 0) {
                    auto current_time = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - start_time);
                    // std::cout << "Appended " << edges_appended << " edges, elapsed time: "
                    //           << elapsed.count() << " ms" << std::endl;
                }
            } else
                break;
        }
    }

    // Final timing report
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    // std::cout << "Self-join iteration completed. Total edges appended: " << edges_appended
    //           << ", total time: " << total_elapsed.count() << " ms" << std::endl;
}

bool KCOperator::is_edge_new(uint64_t source, uint64_t target) {
    auto edge_pair = std::make_pair(source, target);
    if (seen_edges.find(edge_pair) == seen_edges.end()) {
        seen_edges.insert(edge_pair);
        return true;
    }
    return false;
}

std::pair<const uint64_t*, size_t> KCOperator::get_neighbors(uint64_t vid) const {
    // In eager mode (lazy=false), the fix-point was computed in _begin,
    // so we can directly return the neighbors
    if (!lazy) {
        return TwoColumnStoreBindingIter::get_neighbors(vid);
    }

    // In lazy mode (lazy=true), we need to check if computation is needed

    // Check if global compute_fix_point has been called - if so, use existing results
    if (global_compute_fix_point_called) {
        return TwoColumnStoreBindingIter::get_neighbors(vid);
    }

    // Check if compute_fix_point has already been called for this specific vertex
    if (vertex_specific_compute_fix_point_called.find(vid) != vertex_specific_compute_fix_point_called.end()) {
        return TwoColumnStoreBindingIter::get_neighbors(vid);
    }

    // Need to invoke compute_fix_point for this vertex
    // Since this is a const method, we need to cast away const to modify state
    const_cast<KCOperator*>(this)->compute_fix_point(ObjectId(vid));

    // Return the neighbors after computation
    return TwoColumnStoreBindingIter::get_neighbors(vid);
}

void KCOperator::accept_visitor(BindingIterVisitor& visitor) {
    visitor.visit(*this);
}

} // namespace CustomOps