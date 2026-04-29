#pragma once

#include "query/executor/binding_iter/two_column_binding_iter.h"
#include "storage/custom_buffer/two_column_store.h"
#include "query/var_id.h"
#include "graph_models/object_id.h"

namespace CustomOps {

// BindingIter wrapper for TwoColumnStore
// Converts each edge (s,t) in the store to a Binding with two variables.
// Designed to iterate over the contents of an already-populated TwoColumnStore,
// not to accumulate new data into it. It's a read-only iterator that converts each
// edge (source, target) in the store into a binding with two variables
class TwoColumnStoreBindingIter : public TwoColumnBindingIter {
private:
    std::unique_ptr<TwoColumnStore> store;

    // Current iteration state
    TwoColumnStore::VertexIterator vertex_iter;
    TwoColumnStore::VertexIterator vertex_end;
    const uint64_t* current_neighbors;
    size_t neighbor_count;
    size_t neighbor_index;
    uint64_t current_vertex;
    mutable std::vector<uint64_t> neighbors_buffer;

protected:
    // Implement abstract methods from TwoColumnBindingIter
    bool advance_to_next_edge() override;
    void reset_iteration_state() override;
    
    public:
    TwoColumnStoreBindingIter(std::unique_ptr<TwoColumnStore> store, VarId col1_var, VarId col2_var);

    // Constructor with constant col1 value - iterator will only return edges with this col1 value
    TwoColumnStoreBindingIter(std::unique_ptr<TwoColumnStore> store, ObjectId constant_col1_value, VarId col2_var);
    // Constructor with constant col2 value - iterator will only return edges with this col2 value
    TwoColumnStoreBindingIter(std::unique_ptr<TwoColumnStore> store, VarId col1_var, ObjectId constant_col2_value);

    ~TwoColumnStoreBindingIter() = default;
    void get_current_edge(std::pair<ObjectId, ObjectId> &pr) override;
    bool seek_to_vertex(uint64_t vertex) override;
    std::pair<const uint64_t*, size_t> get_neighbors(uint64_t vid) const override;

    void accept_visitor(BindingIterVisitor& visitor) override;

    // Store-specific accessors
    TwoColumnStore* get_store() { return store.get(); }
    void set_store(std::unique_ptr<TwoColumnStore> new_store) { store = std::move(new_store); }
};

} // namespace CustomOps