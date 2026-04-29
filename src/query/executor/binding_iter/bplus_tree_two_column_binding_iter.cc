#include "query/executor/binding_iter/bplus_tree_two_column_binding_iter.h"
#include "query/executor/binding_iter_visitor.h"

template<std::size_t N>
BPlusTreeTwoColumnBindingIter<N>::BPlusTreeTwoColumnBindingIter(const BPlusTree<N>& btree,
                                                                ObjectId predicate,
                                                                VarId col1_var,
                                                                VarId col2_var,
                                                                IndexType type,
                                                                bool epsilon_)
    : TwoColumnBindingIter(col1_var, col2_var),
      btree(btree),
      bpt_iter(),
      fixed_predicate(predicate),
      index_type(type),
      current_record(nullptr) {
    epsilon = epsilon_;
}

template<std::size_t N>
BPlusTreeTwoColumnBindingIter<N>::BPlusTreeTwoColumnBindingIter(const BPlusTree<N>& btree,
                                                                ObjectId predicate,
                                                                ObjectId constant_col1_value,
                                                                VarId col2_var,
                                                                IndexType type,
                                                                bool epsilon_)
    : TwoColumnBindingIter(constant_col1_value, col2_var),  // Use parent class constructor for constant col1
      btree(btree),
      bpt_iter(),
      fixed_predicate(predicate),
      index_type(type),
      current_record(nullptr) {
    epsilon = epsilon_;
}
template<std::size_t N>
BPlusTreeTwoColumnBindingIter<N>::BPlusTreeTwoColumnBindingIter(const BPlusTree<N>& btree,
                                                                ObjectId predicate,
                                                                VarId _col1_var,
                                                                ObjectId _constant_col2_value,
                                                                IndexType type,
                                                                bool epsilon_)
    : TwoColumnBindingIter(_col1_var, _constant_col2_value),
      btree(btree),
      bpt_iter(),
      fixed_predicate(predicate),
      index_type(type),
      current_record(nullptr) {
    epsilon = epsilon_;
}

template<std::size_t N>
void BPlusTreeTwoColumnBindingIter<N>::reset_iteration_state() {
    // Create search range for the fixed predicate
    Record<N> min_record;
    Record<N> max_record;

    if constexpr (N == 3) {
        if (has_constant_col1) {
            // If we have a constant col1 value, constrain the search to that specific value
            // PSO index: want all (p, s, o) where p = fixed_predicate and s = constant_col1
            // POS index: want all (p, o, s) where p = fixed_predicate and o = constant_col1
            min_record[0] = fixed_predicate.id;
            min_record[1] = constant_col1.id;
            min_record[2] = 0;  // Start from first object

            max_record[0] = fixed_predicate.id;
            max_record[1] = constant_col1.id;
            max_record[2] = UINT64_MAX;  // End at last object
        } else if (has_constant_col2) {
            min_record[0] = fixed_predicate.id;
            min_record[1] = 0;
            min_record[2] = constant_col2.id;

            max_record[0] = fixed_predicate.id;
            max_record[1] = UINT64_MAX;
            max_record[2] = constant_col2.id;
        } else {
            // No constant col1 - iterate over all values for the fixed predicate
            // PSO index: predicate is in position 1, we want all (p, s, o) where p = fixed_predicate
            // POS index: predicate is in position 1, we want all (p, o, s) where p = fixed_predicate
            min_record[0] = fixed_predicate.id;
            min_record[1] = 0;  // Start from first subject
            min_record[2] = 0;  // Start from first object

            max_record[0] = fixed_predicate.id;
            max_record[1] = UINT64_MAX;  // End at last subject
            max_record[2] = UINT64_MAX;  // End at last subject
        }
    }

    // Get iterator for the range
    bool *interruption_requested = new bool(false);
    bpt_iter = btree.get_range(interruption_requested, min_record, max_record);
    current_record = nullptr;
}

template<std::size_t N>
bool BPlusTreeTwoColumnBindingIter<N>::advance_to_next_edge() {
    current_record = bpt_iter.next();
    if (current_record == nullptr) {
        return false;
    }

    return true;
}

template<std::size_t N>
void BPlusTreeTwoColumnBindingIter<N>::get_current_edge(std::pair<ObjectId, ObjectId> &pr) {
    if (!current_record) {
        pr.first = ObjectId::get_null();
        pr.second = ObjectId::get_null();
        return;
    }

    if constexpr (N == 3) {
        pr.first = ObjectId((*current_record)[1]);
        pr.second = ObjectId((*current_record)[2]);
        if (has_constant_col2) {
            while (pr.second != constant_col2) {
                if (!advance_to_next_edge()) {
                    pr.first = ObjectId::get_null();
                    pr.second = ObjectId::get_null();
                    return;
                }
                pr.first = ObjectId((*current_record)[1]);
                pr.second = ObjectId((*current_record)[2]);
            }
        }
        return;
    }

    pr.first = ObjectId::get_null();
    pr.second = ObjectId::get_null();
}

template<std::size_t N>
bool BPlusTreeTwoColumnBindingIter<N>::seek_to_vertex(uint64_t vertex) {
    // Create search range for the specified vertex
    Record<N> min_record;
    Record<N> max_record;

    if constexpr (N == 3) {
        min_record[0] = fixed_predicate.id;
        min_record[1] = vertex;
        min_record[2] = 0;

        max_record[0] = fixed_predicate.id;
        max_record[1] = vertex;
        max_record[2] = UINT64_MAX;
    }

    // Get iterator for the range
    bool *interruption_requested = new bool(false);
    bpt_iter = btree.get_range(interruption_requested, min_record, max_record);
    current_record = nullptr;

    // Try to advance to the first edge
    return true;
}

template<std::size_t N>
std::pair<const uint64_t*, size_t> BPlusTreeTwoColumnBindingIter<N>::get_neighbors(uint64_t vid) const {
    // Create a static thread-local buffer to store neighbors
    // This allows us to return a pointer to the data
    this->neighbors_buffer.clear();

    if constexpr (N == 3) {
        // Reuse the same logic as seek_to_vertex to create search range
        Record<N> min_record;
        Record<N> max_record;

        // PSO index: seek to edges with specific subject (vid is subject)
        // Want all (p, s, o) where p = fixed_predicate and s = vid
        // POS index: seek to edges with specific object (vid is object)
        // Want all (p, o, s) where p = fixed_predicate and o = vid
        min_record[0] = fixed_predicate.id;
        min_record[1] = static_cast<uint64_t>(vid);
        
        max_record[0] = fixed_predicate.id;
        max_record[1] = static_cast<uint64_t>(vid);
        if (has_constant_col2) {
            min_record[2] = constant_col2.id;
            max_record[2] = constant_col2.id;
        } else {
            min_record[2] = 0;  // Start from first object
            max_record[2] = UINT64_MAX;  // End at last object
        }

        // Get iterator for the range
        bool *interruption_requested = new bool(false);
        auto iter = btree.get_range(interruption_requested, min_record, max_record);

        // Collect all neighbors
        const Record<N>* record;
        while ((record = iter.next()) != nullptr) {
            // In PSO, the object (position 2) is the neighbor
            // In POS, the subject (position 2) is the neighbor
            uint64_t cur_neighbor = (uint64_t)((*record)[2]);
            if (!has_constant_col2 || cur_neighbor == constant_col2.id) {
                this->neighbors_buffer.emplace_back(cur_neighbor);
            }
        }
    }

    if (this->neighbors_buffer.empty()) {
        return std::make_pair(nullptr, 0);
    }

    return std::make_pair(this->neighbors_buffer.data(), neighbors_buffer.size());
}

template<std::size_t N>
void BPlusTreeTwoColumnBindingIter<N>::accept_visitor(BindingIterVisitor& visitor) {
    visitor.visit(*this);
}

// Explicit template instantiations for common sizes
template class BPlusTreeTwoColumnBindingIter<3>;
template class BPlusTreeTwoColumnBindingIter<4>;