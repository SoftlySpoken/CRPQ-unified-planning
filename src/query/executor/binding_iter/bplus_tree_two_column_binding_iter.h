#pragma once

#include "query/executor/binding_iter/two_column_binding_iter.h"
#include "storage/index/bplus_tree/bplus_tree.h"
#include "storage/index/record.h"
#include "query/var_id.h"
#include "graph_models/object_id.h"

// BindingIter for reading two-column data from B+ trees
// Supports RDF indexes like PSO and POS where we iterate over subject-object pairs
// given a fixed predicate
template<std::size_t N>
class BPlusTreeTwoColumnBindingIter : public TwoColumnBindingIter {
public:
    // RDF index types
    enum IndexType { PSO, POS };

private:
    const BPlusTree<N>& btree;
    BptIter<N> bpt_iter;
    ObjectId fixed_predicate;
    IndexType index_type;
    mutable std::vector<uint64_t> neighbors_buffer;

    // Iteration state
    bool iterator_valid;
    const Record<N>* current_record;

protected:
    // Implement abstract methods from TwoColumnBindingIter
    bool advance_to_next_edge() override;
    void reset_iteration_state() override;
    
    public:
    // For PSO index: col1_var=s, col2_var=o
    // For POS index: col1_var=o, col2_var=s
    BPlusTreeTwoColumnBindingIter(const BPlusTree<N>& btree,
        ObjectId predicate,
        VarId col1_var,
        VarId col2_var,
        IndexType type,
        bool epsilon_=false);

    // Constructor with constant col1 value - iterator will only return edges with this col1 value
    // This is equivalent to calling seek_to_vertex after initialization, but provides a uniform interface
    BPlusTreeTwoColumnBindingIter(const BPlusTree<N>& btree,
        ObjectId predicate,
        ObjectId constant_col1_value,
        VarId col2_var,
        IndexType type,
        bool epsilon_=false);
    // Constructor with constant col2 value - iterator will only return edges with this col2 value
    BPlusTreeTwoColumnBindingIter(const BPlusTree<N>& btree,
        ObjectId predicate,
        VarId col1_var,
        ObjectId constant_col2_value,
        IndexType type,
        bool epsilon_=false);
    void get_current_edge(std::pair<ObjectId, ObjectId> &pr) override;
    bool seek_to_vertex(uint64_t vertex) override;
    std::pair<const uint64_t*, size_t> get_neighbors(uint64_t vid) const override;

    // Getter methods for printer access
    ObjectId get_fixed_predicate() const { return fixed_predicate; }
    IndexType get_index_type() const { return index_type; }
    const char* get_index_type_name() const {
        return index_type == PSO ? "PSO" : "POS";
    }

    void accept_visitor(BindingIterVisitor& visitor) override;
};