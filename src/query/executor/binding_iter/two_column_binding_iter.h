#pragma once

#include "query/executor/binding_iter.h"
#include "query/var_id.h"
#include "graph_models/object_id.h"

// Abstract base class for iterators that produce two-column bindings
// Provides common functionality for iterating over pairs of ObjectIds
// and binding them to two variables in the query execution context.
class TwoColumnBindingIter : public BindingIter {
private:
    ObjectId epsilon_src;
    bool in_epsilon_handling;
protected:
    VarId col1_var;  // Variable ID for first column
    VarId col2_var;  // Variable ID for second column
    // Optional constant col1 value - if set, iterator will only return edges with this col1 value
    ObjectId constant_col1;
    // Optional constant col2 value - if set, iterator will only return edges with this col2 value
    ObjectId constant_col2;
    bool has_constant_col1;
    bool has_constant_col2;
    Binding* parent_binding_ptr;
    bool lazy;
    bool epsilon = false;

    // Template method pattern - derived classes implement these
    virtual bool advance_to_next_edge() = 0;
    virtual void reset_iteration_state() = 0;
    // Common BindingIter implementations
    void _begin(Binding& parent_binding) override;
    bool _next() override;
    bool _epsilon_next() override;
    void _reset() override;

public:
    TwoColumnBindingIter(VarId col1_var, VarId col2_var);

    // Constructor with constant col1 value - iterator will only return edges with this col1 value
    TwoColumnBindingIter(ObjectId constant_col1_value, VarId col2_var);
    // Constructor with constant col2 value - iterator will only return edges with this col2 value
    TwoColumnBindingIter(VarId col1_var, ObjectId constant_col2_value);

    virtual ~TwoColumnBindingIter() = default;
    virtual void get_current_edge(std::pair<ObjectId, ObjectId> &pr) = 0;
    // Seek to the first edge with the given source vertex
    // Returns true if such edges exist, false otherwise
    virtual bool seek_to_vertex(uint64_t vertex) = 0;
    virtual std::pair<const uint64_t*, size_t> get_neighbors(uint64_t vid) const = 0;

    void assign_nulls() override;

    // Accessors (same interface as TwoColumnStoreBindingIter)
    VarId get_col1_var() const { return col1_var; }
    VarId get_col2_var() const { return col2_var; }

    bool get_has_constant_col1() const { return has_constant_col1; }
    ObjectId get_constant_col1() const { return constant_col1; }
    bool get_has_constant_col2() const { return has_constant_col2; }
    ObjectId get_constant_col2() const { return constant_col2; }

    // Helper method to get col1 ID from either constant or binding
    // This method consolidates the common pattern used in custom operators
    inline uint64_t get_col1_id(const Binding& binding) const {
        return has_constant_col1 ? constant_col1.id : binding[col1_var].id;
    }

    void set_lazy(bool _lazy) { lazy = _lazy; }
    bool is_lazy() const { return lazy; }

    void set_epsilon(bool epsilon_) { epsilon = epsilon_; }
    bool get_epsilon() const override { return epsilon; }
};