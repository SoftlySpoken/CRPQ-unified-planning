#include "ti_two_way_operator.h"
#include <iostream>
#include <algorithm>

namespace CustomOps {

TITwoWayOperator::TITwoWayOperator(std::unique_ptr<TwoColumnBindingIter> _leftmost_operand,
                                   std::unique_ptr<TwoColumnBindingIter> _edge_path_operand,
                                VarId col1_var,
                                VarId col2_var,
                                bool epsilon_, bool _lazy)
    : TwoColumnStoreBindingIter(nullptr, col1_var, col2_var)
    , leftmost_operand(std::move(_leftmost_operand))
    , edge_path_operand(std::move(_edge_path_operand))
    , common_var(this->leftmost_operand->get_col2_var())
    , result_store(std::make_unique<TwoColumnStore>())
    , parent_binding_ptr(nullptr)
    , has_current_leftmost_row(false)
    , has_current_neighbor(false) {
    this->lazy = _lazy;
    epsilon = epsilon_ || leftmost_operand->get_epsilon() && edge_path_operand->get_epsilon();
    global_computation_done = false;
    this->init_max_var_id();
    // if (leftmost_operand->get_has_constant_col1()) {
    //     this->has_constant_col1 = true;
    //     this->constant_col1 = leftmost_operand->get_constant_col1();
    // }
}
TITwoWayOperator::TITwoWayOperator(std::unique_ptr<TwoColumnBindingIter> _leftmost_operand,
                                   std::unique_ptr<TwoColumnBindingIter> _edge_path_operand,
                                ObjectId constant_col1_value,
                                VarId col2_var,
                                bool epsilon_, bool _lazy)
    : TwoColumnStoreBindingIter(nullptr, constant_col1_value, col2_var)
    , leftmost_operand(std::move(_leftmost_operand))
    , edge_path_operand(std::move(_edge_path_operand))
    , common_var(this->leftmost_operand->get_col2_var())
    , result_store(std::make_unique<TwoColumnStore>())
    , parent_binding_ptr(nullptr)
    , has_current_leftmost_row(false)
    , has_current_neighbor(false) {
    this->lazy = _lazy;
    epsilon = epsilon_ || leftmost_operand->get_epsilon() && edge_path_operand->get_epsilon();
    global_computation_done = false;
    this->init_max_var_id();
    // if (leftmost_operand->get_has_constant_col1()) {
    //     this->has_constant_col1 = true;
    //     this->constant_col1 = leftmost_operand->get_constant_col1();
    // }
}
TITwoWayOperator::TITwoWayOperator(std::unique_ptr<TwoColumnBindingIter> _leftmost_operand,
                                   std::unique_ptr<TwoColumnBindingIter> _edge_path_operand,
                                   VarId _col1_var,
                                ObjectId _constant_col2_value,
                                bool epsilon_, bool _lazy)
    : TwoColumnStoreBindingIter(nullptr, _col1_var, _constant_col2_value)
    , leftmost_operand(std::move(_leftmost_operand))
    , edge_path_operand(std::move(_edge_path_operand))
    , common_var(this->leftmost_operand->get_col2_var())
    , result_store(std::make_unique<TwoColumnStore>())
    , parent_binding_ptr(nullptr)
    , has_current_leftmost_row(false)
    , has_current_neighbor(false) {
    this->lazy = _lazy;
    epsilon = epsilon_ || leftmost_operand->get_epsilon() && edge_path_operand->get_epsilon();
    global_computation_done = false;
    this->init_max_var_id();
    // if (leftmost_operand->get_has_constant_col1()) {
    //     this->has_constant_col1 = true;
    //     this->constant_col1 = leftmost_operand->get_constant_col1();
    // }
}

void TITwoWayOperator::init_max_var_id() {
    // Get binding size for temporary binding
    int col1_var_id = -1;
    if (!leftmost_operand->get_has_constant_col1()) {
        col1_var_id = leftmost_operand->get_col1_var().id;
    }
    int col2_var_id = leftmost_operand->get_col2_var().id;
    int edge_col2_var_id = -1;
    (this->ep_has_const_col2) = edge_path_operand->get_has_constant_col2();
    if (!ep_has_const_col2) {
        edge_col2_var_id = edge_path_operand->get_col2_var().id;
    } else {
        (this->ep_const_col2) = edge_path_operand->get_constant_col2();
    }

    (this->max_var_id) = std::max({col1_var_id, col2_var_id, edge_col2_var_id});
}

void TITwoWayOperator::_begin(Binding& parent_binding) {
    if (!this->get_store()) {
        // Reset state
        result_store->clear();
        parent_binding_ptr = &parent_binding;

        // Reset lazy computation state
        computed_vertices.clear();
        global_computation_done = false;

        // Compute all results and store them
        if (!lazy) {
            compute_all_results();
            std::cout << "TITwoWayOperator result_store size = " << result_store->size() << std::endl;
            set_store(std::move(result_store));
        } else {
            set_store(std::make_unique<TwoColumnStore>());
        }
    }

    // Call parent's _begin to initialize iteration over the result store
    TwoColumnStoreBindingIter::_begin(parent_binding);
}

void TITwoWayOperator::compute_all_results() {
    // First compute the main join results
    compute_join_results();

    // Then handle epsilon cases
    if (leftmost_operand->get_epsilon() || edge_path_operand->get_epsilon()) {
        compute_epsilon_results();
    }

    // Compact the result store for efficient iteration
    result_store->compact();
}

void TITwoWayOperator::compute_join_results() {
    Binding temp_binding(max_var_id + 1);

    // Initialize leftmost operand
    leftmost_operand->begin(temp_binding);
    edge_path_operand->begin(temp_binding);
    // long long edge_path_constant_col1_id = -1;
    // if (edge_path_operand->get_has_constant_col1())
    //     edge_path_constant_col1_id = edge_path_operand->get_constant_col1().id;

    // For each row in leftmost operand
    while (leftmost_operand->next()) {
        // Get the common vertex value
        ObjectId common_vertex_obj = temp_binding[common_var];
        if (common_vertex_obj.is_null()) {
            continue;
        }

        long long common_vertex = static_cast<long long>(common_vertex_obj.id);
        // if (edge_path_constant_col1_id != -1 && edge_path_constant_col1_id != common_vertex)
        //     continue;

        // Get neighbors from edge_path_operand
        auto [neighbors_ptr, neighbor_count] = edge_path_operand->get_neighbors(common_vertex);

        // For each neighbor, create a result row
        for (size_t i = 0; i < neighbor_count; ++i) {
            long long neighbor = neighbors_ptr[i];
            if (ep_has_const_col2 && neighbor != ep_const_col2.id) {
                continue;
            }

            // Get the leftmost operand's col1 value (the start vertex)
            ObjectId start_vertex_obj;
            if (leftmost_operand->get_has_constant_col1())
                start_vertex_obj = leftmost_operand->get_constant_col1();
            else
                start_vertex_obj = temp_binding[leftmost_operand->get_col1_var()];
            if (!start_vertex_obj.is_null()) {
                long long start_vertex = static_cast<long long>(start_vertex_obj.id);
                result_store->append(start_vertex, neighbor);
            }
        }
    }
}

void TITwoWayOperator::compute_epsilon_results() {
    Binding temp_binding(max_var_id + 1);

    // Handle left operand epsilon: when leftmost_operand has epsilon,
    // produce cartesian product with edge_path_operand
    if (leftmost_operand->get_epsilon()) {
        edge_path_operand->reset();
        edge_path_operand->begin(temp_binding);
        while (edge_path_operand->next()) {
            ObjectId col1_val = temp_binding[edge_path_operand->get_col1_var()];
            ObjectId col2_val = ep_has_const_col2 ? ep_const_col2 : temp_binding[edge_path_operand->get_col2_var()];

            if (!col1_val.is_null() && (!this->has_constant_col1 || col1_val == this->constant_col1) && !col2_val.is_null()) {
                result_store->append(static_cast<long long>(col1_val.id),
                                   static_cast<long long>(col2_val.id));
            }
        }
    }

    // Handle right operand epsilon: when edge_path_operand has epsilon,
    // produce cartesian product with leftmost_operand
    if (edge_path_operand->get_epsilon()) {
        leftmost_operand->reset();
        leftmost_operand->begin(temp_binding);
        while (leftmost_operand->next()) {
            ObjectId col1_val;
            if (leftmost_operand->get_has_constant_col1())
                col1_val = leftmost_operand->get_constant_col1();
            else
                col1_val = temp_binding[leftmost_operand->get_col1_var()];
            ObjectId col2_val = temp_binding[leftmost_operand->get_col2_var()];
            if (edge_path_operand->get_has_constant_col2()
            && col2_val != edge_path_operand->get_constant_col2()) {
                continue;
            }

            if (!col1_val.is_null() && !col2_val.is_null()) {
                result_store->append(static_cast<long long>(col1_val.id),
                                   static_cast<long long>(col2_val.id));
            }
        }
    }
}

void TITwoWayOperator::assign_nulls() {
    leftmost_operand->assign_nulls();
    edge_path_operand->assign_nulls();
}

void TITwoWayOperator::accept_visitor(BindingIterVisitor& visitor) {
    visitor.visit(*this);
}

std::pair<const uint64_t*, size_t> TITwoWayOperator::get_neighbors(uint64_t vid) const {
    if (!lazy || global_computation_done) {
        // Eager mode or global computation already done: use existing materialized results
        return TwoColumnStoreBindingIter::get_neighbors(vid);
    }

    // Lazy mode: check if computation is needed for this vertex
    if (computed_vertices.find(vid) == computed_vertices.end()) {
        // Need to compute union results for this vertex
        compute_results_for_vertex(vid);
    }

    // Return the neighbors after computation
    return TwoColumnStoreBindingIter::get_neighbors(vid);

}

bool TITwoWayOperator::seek_to_vertex(uint64_t vertex) {
    if (!lazy || global_computation_done) {
        // Eager mode or global computation already done: use existing materialized results
        return TwoColumnStoreBindingIter::seek_to_vertex(vertex);
    }

    // Lazy mode: check if computation is needed for this vertex
    if (computed_vertices.find(vertex) == computed_vertices.end()) {
        // Need to compute union results for this vertex
        compute_results_for_vertex(vertex);
    }
    return TwoColumnStoreBindingIter::seek_to_vertex(vertex);
}

void TITwoWayOperator::compute_results_for_vertex(uint64_t vertex) const {
    // Mark vertex as being computed
    computed_vertices.insert(vertex);
    
    if (this->get_has_constant_col1() && this->get_constant_col1().id != vertex) {
        return;
    }

    Binding temp_binding(max_var_id + 1);
    leftmost_operand->begin(temp_binding);
    edge_path_operand->begin(temp_binding);
    // Normal join
    leftmost_operand->seek_to_vertex(vertex);
    while (leftmost_operand->next()) {
        if (!this->get_has_constant_col1() && temp_binding[this->get_col1_var()].id != vertex) {
            break;
        }
        ObjectId common_vertex_obj = temp_binding[common_var];
        long long common_vertex = static_cast<long long>(common_vertex_obj.id);
        if (edge_path_operand->get_epsilon()) {
            if (!ep_has_const_col2 || common_vertex == ep_const_col2.id) {
                const_cast<TITwoWayOperator*>(this)->get_store()->append(vertex, common_vertex);
            }
        }
        auto [neighbors_ptr, neighbor_count] = edge_path_operand->get_neighbors(common_vertex);
        for (size_t i = 0; i < neighbor_count; ++i) {
            long long neighbor = neighbors_ptr[i];
            if (ep_has_const_col2 && neighbor != ep_const_col2.id) {
                continue;
            }
            const_cast<TITwoWayOperator*>(this)->get_store()->append(vertex, neighbor);
        }
    }

    // Treat epsilon
    if (leftmost_operand->get_epsilon()) {
        edge_path_operand->seek_to_vertex(vertex);
        while (edge_path_operand->next()) {
            if (!this->get_has_constant_col1() && temp_binding[this->get_col1_var()].id != vertex) {
                break;
            }
            long long neighbor = temp_binding[edge_path_operand->get_col2_var()].id;
            if (ep_has_const_col2 && neighbor != ep_const_col2.id) {
                continue;
            }
            const_cast<TITwoWayOperator*>(this)->get_store()->append(vertex, neighbor);
        }
    }
}

} // namespace CustomOps