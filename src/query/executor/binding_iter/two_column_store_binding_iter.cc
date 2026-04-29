#include "query/executor/binding_iter/two_column_store_binding_iter.h"

namespace CustomOps {

TwoColumnStoreBindingIter::TwoColumnStoreBindingIter(std::unique_ptr<TwoColumnStore> _store,
                                                     VarId col1_var,
                                                     VarId col2_var)
    : TwoColumnBindingIter(col1_var, col2_var),
      store(std::move(_store)),
      vertex_iter(this->store ? this->store->vertices_end() : TwoColumnStore::VertexIterator(std::unordered_map<uint64_t, size_t>::const_iterator{})),
      vertex_end(this->store ? this->store->vertices_end() : TwoColumnStore::VertexIterator(std::unordered_map<uint64_t, size_t>::const_iterator{})),
      current_neighbors(nullptr),
      neighbor_count(0),
      neighbor_index(0),
      current_vertex(-1) {
}

TwoColumnStoreBindingIter::TwoColumnStoreBindingIter(std::unique_ptr<TwoColumnStore> _store,
                                                     ObjectId constant_col1_value,
                                                     VarId col2_var)
    : TwoColumnBindingIter(constant_col1_value, col2_var),
      store(std::move(_store)),
      vertex_iter(this->store ? this->store->vertices_end() : TwoColumnStore::VertexIterator(std::unordered_map<uint64_t, size_t>::const_iterator{})),
      vertex_end(this->store ? this->store->vertices_end() : TwoColumnStore::VertexIterator(std::unordered_map<uint64_t, size_t>::const_iterator{})),
      current_neighbors(nullptr),
      neighbor_count(0),
      neighbor_index(0),
      current_vertex(-1) {
}
TwoColumnStoreBindingIter::TwoColumnStoreBindingIter(std::unique_ptr<TwoColumnStore> _store,
    VarId col1_var,
    ObjectId constant_col2_value)
    : TwoColumnBindingIter(col1_var, constant_col2_value),
      store(std::move(_store)),
      vertex_iter(this->store ? this->store->vertices_end() : TwoColumnStore::VertexIterator(std::unordered_map<uint64_t, size_t>::const_iterator{})),
      vertex_end(this->store ? this->store->vertices_end() : TwoColumnStore::VertexIterator(std::unordered_map<uint64_t, size_t>::const_iterator{})),
      current_neighbors(nullptr),
      neighbor_count(0),
      neighbor_index(0),
      current_vertex(-1) {
}


bool TwoColumnStoreBindingIter::advance_to_next_edge() {
    if (!store || store->empty()) {
        return false;
    }

    // Variable col1 case - iterate over all vertices
    // If we have more neighbors for current vertex
    if (neighbor_index < neighbor_count) {
        return true;
    }
    // If we have a constant col1, we only iterate over that specific vertex
    if (has_constant_col1)
        return false;

    // Move to next vertex that has neighbors
    while (vertex_iter != vertex_end) {
        current_vertex = vertex_iter.vertex_id();
        auto neighbors_pair = store->get_neighbors(current_vertex);
        current_neighbors = neighbors_pair.first;
        neighbor_count = neighbors_pair.second;
        neighbor_index = 0;

        ++vertex_iter;

        if (neighbor_count > 0) {
            return true;
        }
    }

    return false;
}

void TwoColumnStoreBindingIter::get_current_edge(std::pair<ObjectId, ObjectId> &pr) {
    // Get current edge (source, target)
    ObjectId source_obj;

    if (has_constant_col1) {
        // Use the constant col1 value
        source_obj = constant_col1;
    } else {
        // Use the current vertex
        source_obj = ObjectId(current_vertex);
    }

    uint64_t target = current_neighbors[neighbor_index];
    ObjectId target_obj = ObjectId(target);
    if (has_constant_col2) {
        while (constant_col2 != target_obj) {
            neighbor_index++;
            if (neighbor_index >= neighbor_count) {
                if (!advance_to_next_edge()) {
                    pr.first = ObjectId::get_null();
                    pr.second = ObjectId::get_null();
                    return;
                }
            }
            source_obj = ObjectId(current_vertex);
            target_obj = ObjectId(current_neighbors[neighbor_index]);
        }
    }

    neighbor_index++;
    pr.first = source_obj;
    pr.second = target_obj;
}

void TwoColumnStoreBindingIter::reset_iteration_state() {
    neighbor_index = 0;
    neighbor_count = 0;
    current_neighbors = nullptr;

    if (has_constant_col1) {
        // For constant col1, set up to iterate over the specific vertex
        uint64_t constant_vertex = static_cast<uint64_t>(constant_col1.id);
        current_vertex = constant_vertex;

        if (store) {
            auto neighbors_pair = store->get_neighbors(constant_vertex);
            current_neighbors = neighbors_pair.first;
            neighbor_count = neighbors_pair.second;
        }
    } else {
        // For variable col1, reset to iterate over all vertices
        current_vertex = -1;
        if (store) {
            vertex_iter = store->vertices_begin();
            vertex_end = store->vertices_end();
        }
    }
}

bool TwoColumnStoreBindingIter::seek_to_vertex(uint64_t vertex) {
    if (!store || store->empty()) {
        return false;
    }

    // Try to get neighbors for the specified vertex
    auto neighbors_pair = store->get_neighbors(vertex);
    current_neighbors = neighbors_pair.first;
    neighbor_count = neighbors_pair.second;

    if (neighbor_count == 0) {
        return false;  // No edges for this vertex
    }

    // Set up state to iterate over this vertex's neighbors
    current_vertex = vertex;
    neighbor_index = 0;

    // Update vertex_iter to point to the found vertex so that future calls to
    // advance_to_next_edge() will continue correctly from the next vertex
    vertex_iter = store->vertices_after(vertex);

    return true;
}

std::pair<const uint64_t*, size_t> TwoColumnStoreBindingIter::get_neighbors(uint64_t vid) const {
    if (has_constant_col2) {
        neighbors_buffer.clear();
        const auto &pr = this->store->get_neighbors(vid);
        for (size_t i = 0; i < pr.second; i++) {
            if (pr.first[i] == constant_col2.id) {
                neighbors_buffer.emplace_back(constant_col2.id);
            }
        }
        return std::make_pair<const uint64_t*, size_t>(this->neighbors_buffer.data(), this->neighbors_buffer.size());
    } else {
        return this->store->get_neighbors(vid);
    }
}


void TwoColumnStoreBindingIter::accept_visitor(BindingIterVisitor& visitor) {
    visitor.visit(*this);
}

} // namespace CustomOps