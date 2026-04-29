#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <unordered_map>
#include <cstring>
#include <optional>

namespace CustomOps {

// Special two-column intermediate result memory layout
// Optimized for storing and accessing two-column results efficiently
class TwoColumnStore {
private:
    // Hash table from vertex ID to contiguous integers
    std::unordered_map<uint64_t, size_t> id2contiguous;
    // Reverse mapping from contiguous integers to vertex IDs (for O(1) iterator access)
    std::vector<uint64_t> contiguous2id;
    std::shared_ptr<uint64_t> csr_adj;    // Adjacency list of CSR
    std::shared_ptr<size_t> csr_off;    // Offsets of CSR
    std::vector<bool> csr_sorted;   // Denotes whether each vertex's neighbor lists are sorted
    std::vector<std::vector<uint64_t>> adjacency_lists;  // Adjacency lists, to be compacted into csr_adj
    size_t num_vertices;
    size_t num_edges;

public:
    TwoColumnStore();
    ~TwoColumnStore();

    // Basic operations
    void append(uint64_t s, uint64_t t);
    std::pair<const uint64_t*, size_t> get_neighbors(uint64_t vid) const;
    void sort_neighbors(uint64_t vid);
    void compact();
    void clear();
    size_t size() const;
    bool empty() const;

    // Iterator interface
    class Iterator {
    private:
        const TwoColumnStore* store;
        size_t vertex_idx;      // Current vertex index
        uint64_t vertex_id;    // Current vertex ID
        size_t edge_in_vertex;  // Edge index within current vertex
        size_t global_position; // Global edge position

    public:
        Iterator(const TwoColumnStore* s, size_t v_idx, size_t e_idx);

        std::pair<uint64_t, uint64_t> operator*() const;
        Iterator& operator++();
        bool operator!=(const Iterator& other) const;
        bool operator==(const Iterator& other) const;
    };

    Iterator begin() const;
    Iterator end() const { return Iterator(this, num_vertices, 0); }

    // Iterator for unique vertices (column 1 values)
    class VertexIterator {
    private:
        std::unordered_map<uint64_t, size_t>::const_iterator it;

    public:
        VertexIterator(std::unordered_map<uint64_t, size_t>::const_iterator iter) : it(iter) {}

        uint64_t operator*() const { return it->first; }
        uint64_t vertex_id() const { return it->first; }
        VertexIterator& operator++() { ++it; return *this; }
        bool operator!=(const VertexIterator& other) const { return it != other.it; }
    };

    VertexIterator vertices_begin() const { return VertexIterator(id2contiguous.begin()); }
    VertexIterator vertices_end() const { return VertexIterator(id2contiguous.end()); }
    VertexIterator vertices_after(uint64_t v) const {
        auto v_iter = id2contiguous.find(v);
        if (v_iter == id2contiguous.end())
            return VertexIterator(id2contiguous.end());
        else {
            ++v_iter;
            return VertexIterator(v_iter);
        }
    }

    // Statistics
    struct Statistics {
        size_t total_rows;
        size_t unique_col1_values;
        size_t unique_col2_values;
        double col1_selectivity;
        double col2_selectivity;
        size_t memory_bytes;
    };

    Statistics get_statistics() const;

    // Serialization for spilling to disk
    void serialize_to_file(const std::string& filename);
    void deserialize_from_file(const std::string& filename);
};

} // namespace CustomOps