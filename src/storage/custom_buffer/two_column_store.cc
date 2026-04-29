#include "storage/custom_buffer/two_column_store.h"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace CustomOps {

// TwoColumnStore implementation
TwoColumnStore::TwoColumnStore(): csr_adj(nullptr), csr_off(nullptr), num_vertices(0), num_edges(0) {
    // Initialization if needed
}

TwoColumnStore::~TwoColumnStore() {
    // Cleanup if needed
}

void TwoColumnStore::append(uint64_t s, uint64_t t) {
    auto it = id2contiguous.find(s);
    if (it == id2contiguous.end()) {
        size_t new_contiguous_id = id2contiguous.size();
        id2contiguous[s] = new_contiguous_id;
        contiguous2id.push_back(s);  // Maintain reverse mapping
        adjacency_lists.emplace_back();
        it = id2contiguous.find(s);
        num_vertices++;
    }
    adjacency_lists[it->second].emplace_back(t);
    num_edges++;
}

/**
 * @brief Get neighbors of a vertex without memory copying
 *
 * Returns a pointer to the neighbors array and the count of neighbors for the specified vertex.
 * This is a zero-copy operation that provides direct access to the adjacency data.
 * Works with both compacted (CSR) and non-compacted (adjacency_lists) formats.
 *
 * @param vid The vertex ID to get neighbors for
 * @return std::pair<const uint64_t*, std::size_t> A pair containing:
 *         - Pointer to the first neighbor in the adjacency array (nullptr if vertex not found)
 *         - Number of neighbors (0 if vertex not found or has no neighbors)
 *
 * @note The returned pointer is valid until the next call to compact() or clear()
 * @note Time complexity: O(1) average case for hash table lookup
 */
std::pair<const uint64_t*, std::size_t> TwoColumnStore::get_neighbors(uint64_t vid) const {
    auto it = id2contiguous.find(vid);
    if (it == id2contiguous.end()) {
        return {nullptr, 0}; // Vertex not found
    }

    std::size_t contiguous_id = it->second;

    if (contiguous_id >= num_vertices) {
        // Should not happen if id2contiguous is consistent with num_vertices
        return {nullptr, 0};
    }

    // Handle compacted case (CSR format)
    if (csr_adj && csr_off) {
        const std::size_t start_offset = csr_off.get()[contiguous_id];
        const std::size_t end_offset   = csr_off.get()[contiguous_id + 1];
        const std::size_t count        = end_offset - start_offset;

        return {csr_adj.get() + start_offset, count};
    }
    // Handle non-compacted case (adjacency_lists)
    else if (!adjacency_lists.empty() && contiguous_id < adjacency_lists.size()) {
        const std::vector<uint64_t>& neighbors = adjacency_lists[contiguous_id];
        if (neighbors.empty()) {
            return {nullptr, 0};
        }
        return {neighbors.data(), neighbors.size()};
    }

    return {nullptr, 0};
}

/**
 * @brief Sort neighbors of a vertex in ascending order
 * Works with both compacted (CSR) and non-compacted (adjacency_lists) formats.
 * @param vid Vertex ID whose neighbors to sort
 */
void TwoColumnStore::sort_neighbors(uint64_t vid) {
    auto it = id2contiguous.find(vid);
    if (it == id2contiguous.end()) {
        return; // Vertex not found
    }

    std::size_t contiguous_id = it->second;
    if (contiguous_id >= num_vertices) {
        return; // Invalid vertex
    }

    // Handle compacted case (CSR format)
    if (csr_adj && csr_off) {
        if (csr_sorted[contiguous_id]) {
            return; // Already sorted
        }

        std::size_t start_offset = csr_off.get()[contiguous_id];
        std::size_t end_offset = csr_off.get()[contiguous_id + 1];

        // Sort the neighbors in ascending order
        std::sort(csr_adj.get() + start_offset, csr_adj.get() + end_offset);
        csr_sorted[contiguous_id] = true;
    }
    // Handle non-compacted case (adjacency_lists)
    else if (!adjacency_lists.empty() && contiguous_id < adjacency_lists.size()) {
        // For non-compacted case, we don't have a sorted flag array, so we sort directly
        // The adjacency_lists will be sorted in-place
        std::vector<uint64_t>& neighbors = adjacency_lists[contiguous_id];
        std::sort(neighbors.begin(), neighbors.end());
    }
}

/**
 * @brief Compacts the adjacency lists into CSR (Compressed Sparse Row) format.
 *
 * This method converts the `adjacency_lists` (which store neighbors in `std::vector<std::vector<int>>`)
 * into a more memory-efficient CSR format using `csr_adj` and `csr_off`.
 * All original vertex IDs are mapped to contiguous integers using `id2contiguous`.
 *
 * After compaction, the `adjacency_lists` are cleared to free memory.
 *
 */
void TwoColumnStore::compact() {
    if (adjacency_lists.empty()) {
        return;
    }

    // Allocate memory for CSR format
    // csr_off needs num_vertices + 1 entries (one for each vertex plus end marker)
    csr_off = std::shared_ptr<std::size_t>(new std::size_t[num_vertices + 1], std::default_delete<std::size_t[]>());
    // csr_sorted num_vertices entries
    csr_sorted.assign(num_vertices, false);
    // csr_adj needs num_edges entries (one for each edge)
    csr_adj = std::shared_ptr<uint64_t>(new uint64_t[num_edges], std::default_delete<uint64_t[]>());

    std::size_t current_offset = 0;

    // Convert adjacency lists to CSR format
    for (std::size_t vertex_idx = 0; vertex_idx < num_vertices; vertex_idx++) {
        csr_off.get()[vertex_idx] = current_offset;

        // Copy all neighbors of this vertex to the CSR adjacency array using memcpy
        const std::vector<uint64_t>& neighbors = adjacency_lists[vertex_idx];
        if (!neighbors.empty()) {
            std::memcpy(csr_adj.get() + current_offset,
            neighbors.data(),
            neighbors.size() * sizeof(uint64_t));
        }
        
        // std::cout << "HERE ";
        // for (std::size_t it = 0; it < adjacency_lists[vertex_idx].size(); it++)
        //     std::cout << *(csr_adj.get() + current_offset + it) << " ";
        // std::cout << std::endl;

        current_offset += adjacency_lists[vertex_idx].size();
    }

    // Set the final offset (end marker)
    csr_off.get()[num_vertices] = num_edges;

    // Clear the adjacency lists to free memory
    adjacency_lists.clear();
    adjacency_lists.shrink_to_fit();
}

void TwoColumnStore::clear() {
    id2contiguous.clear();
    contiguous2id.clear();  // Clear reverse mapping
    csr_adj.reset();
    csr_off.reset();
    csr_sorted.clear();
    adjacency_lists.clear();
    num_vertices = 0;
    num_edges = 0;
}

std::size_t TwoColumnStore::size() const {
    return num_edges;
}

bool TwoColumnStore::empty() const {
    return id2contiguous.empty();
}


TwoColumnStore::Iterator::Iterator(const TwoColumnStore* s, std::size_t v_idx, std::size_t e_idx)
: store(s), vertex_idx(v_idx), edge_in_vertex(e_idx) {
    // Compute global position
    global_position = store->num_edges;  // Default error value
    if (vertex_idx >= store->num_vertices)
        return;
    if (store->csr_adj && store->csr_off) {
        size_t cur_v_off = store->csr_off.get()[vertex_idx];
        if (store->csr_off.get()[vertex_idx + 1] > cur_v_off + edge_in_vertex)
            global_position = cur_v_off + edge_in_vertex;
    } else if (!store->adjacency_lists.empty()) {
        if (store->adjacency_lists[vertex_idx].size() > edge_in_vertex) {
            size_t cur_v_off = 0, cur_v = 0;
            while (cur_v < vertex_idx) {
                cur_v_off += store->adjacency_lists[cur_v].size();
                cur_v++;
            }
            global_position = cur_v_off + edge_in_vertex;
        }
    }
    // O(1) direct access instead of O(V) linear search
    if (vertex_idx < store->contiguous2id.size()) {
        vertex_id = store->contiguous2id[vertex_idx];
    }
}

TwoColumnStore::Iterator TwoColumnStore::begin() const {
    if (num_edges == 0) {
        return end();
    }

    std::size_t vertex_idx = 0;

    // Handle compacted case (CSR format)
    if (csr_adj && csr_off) {
        // Find the first vertex with edges
        while (vertex_idx < num_vertices) {
            if (csr_off.get()[vertex_idx + 1] > csr_off.get()[vertex_idx]) {
                break;
            }
            vertex_idx++;
        }
    }
    // Handle uncompacted case (adjacency_lists)
    else if (!adjacency_lists.empty()) {
        // Find the first vertex with edges
        while (vertex_idx < adjacency_lists.size()) {
            if (!adjacency_lists[vertex_idx].empty()) {
                break;
            }
            vertex_idx++;
        }
    }

    return Iterator(this, vertex_idx, 0);
}

// Iterator implementation
std::pair<uint64_t, uint64_t> TwoColumnStore::Iterator::operator*() const {
    if (global_position >= store->num_edges) {
        return {0, 0};
    }

    // Handle compacted case (CSR format)
    if (store->csr_adj && store->csr_off) {
        if (vertex_idx >= store->num_vertices) {
            return {0, 0};
        }

        // Get the target vertex from the CSR adjacency array
        std::size_t csr_position = store->csr_off.get()[vertex_idx] + edge_in_vertex;
        uint64_t target_vertex = static_cast<uint64_t>(store->csr_adj.get()[csr_position]);

        return {vertex_id, target_vertex};
    }
    // Handle uncompacted case (adjacency_lists)
    else if (!store->adjacency_lists.empty()) {
        if (vertex_idx >= store->adjacency_lists.size() ||
            edge_in_vertex >= store->adjacency_lists[vertex_idx].size()) {
            return {0, 0};
        }

        uint64_t target_vertex = static_cast<uint64_t>(store->adjacency_lists[vertex_idx][edge_in_vertex]);
        return {vertex_id, target_vertex};
    }

    return {0, 0};
}

TwoColumnStore::Iterator& TwoColumnStore::Iterator::operator++() {
    global_position++;

    if (global_position >= store->num_edges) {
        return *this;
    }

    // Handle compacted case (CSR format)
    if (store->csr_adj && store->csr_off) {
        edge_in_vertex++;

        // Check if we need to move to the next vertex
        while (vertex_idx < store->num_vertices &&
               edge_in_vertex >= (store->csr_off.get()[vertex_idx + 1] - store->csr_off.get()[vertex_idx])) {
            vertex_idx++;
            edge_in_vertex = 0;
        }
    }
    // Handle uncompacted case (adjacency_lists)
    else if (!store->adjacency_lists.empty()) {
        edge_in_vertex++;

        // Check if we need to move to the next vertex
        while (vertex_idx < store->adjacency_lists.size() &&
               edge_in_vertex >= store->adjacency_lists[vertex_idx].size()) {
            vertex_idx++;
            edge_in_vertex = 0;
        }
    }
    // O(1) direct access instead of O(V) linear search
    if (vertex_idx < store->contiguous2id.size()) {
        vertex_id = store->contiguous2id[vertex_idx];
    }

    return *this;
}

bool TwoColumnStore::Iterator::operator!=(const Iterator& other) const {
    return store != other.store || global_position != other.global_position;
}

bool TwoColumnStore::Iterator::operator==(const Iterator& other) const {
    return store == other.store && global_position == other.global_position;
}

TwoColumnStore::Statistics TwoColumnStore::get_statistics() const {
    Statistics stats;

    // TODO: Calculate statistics
    stats.unique_col1_values = 0;
    stats.unique_col2_values = 0;
    stats.col1_selectivity = 0.0;
    stats.col2_selectivity = 0.0;

    return stats;
}

void TwoColumnStore::serialize_to_file(const std::string& filename) {
    // TODO: Implement serialization
}

void TwoColumnStore::deserialize_from_file(const std::string& filename) {
    // TODO: Implement deserialization
}

} // namespace CustomOps