#pragma once

#include "query/executor/binding_iter/two_column_binding_iter.h"
#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include "storage/custom_buffer/two_column_store.h"
#include <memory>
#include <unordered_set>

namespace CustomOps {

// KC (Kleene Closure) Operator
// Performs fix-point computation on a TwoColumnStore by doing self-join
// with condition "target=source" (col2_var's result = col1_var's result)
// until no new results are produced
class KCOperator : public TwoColumnStoreBindingIter {
private:
    std::unique_ptr<TwoColumnBindingIter> child;


    // Hash set for efficient duplicate detection (source,target) pairs
    // Custom hash function for uint64_t pairs
    struct PairHash {
        std::size_t operator()(const std::pair<uint64_t, uint64_t>& p) const {
            // Use a simple but effective hash combination
            return std::hash<uint64_t>()(p.first) ^ (std::hash<uint64_t>()(p.second) << 1);
        }
    };
    std::unordered_set<std::pair<uint64_t, uint64_t>, PairHash> seen_edges;

    // Fix-point computation state
    bool fix_point_reached;
    bool initialization_complete;
    size_t iterations_count;

    // Lazy computation flag - if true, fix point computation is deferred until get_neighbors calls
    // bool lazy;

    // Tracking for compute_fix_point invocations
    mutable bool global_compute_fix_point_called;
    mutable std::unordered_set<uint64_t> vertex_specific_compute_fix_point_called;

public:
    KCOperator(std::unique_ptr<TwoColumnBindingIter> child,
               VarId col1_var,
               VarId col2_var,
               bool _epsilon,
               bool _lazy = false);

    // Constructor with constant col1 value - performs Kleene closure with fixed source
    KCOperator(std::unique_ptr<TwoColumnBindingIter> child,
               ObjectId constant_col1_value,
               VarId col2_var,
               bool _epsilon,
               bool _lazy = false);
    // Constructor with constant col2 value - performs Kleene closure with fixed target
    KCOperator(std::unique_ptr<TwoColumnBindingIter> child,
        VarId col1_var,
        ObjectId constant_col2_value,
               bool _epsilon,
               bool _lazy = false);

    ~KCOperator() = default;

protected:
    void _begin(Binding& parent_binding) override;

private:
    // Helper methods for fix-point computation
    void compute_fix_point(ObjectId starting_vertex = ObjectId::get_null());
    void perform_self_join_iteration(TwoColumnStore& current_store, TwoColumnStore& next_store, Binding &_temp_binding, ObjectId starting_vertex = ObjectId::get_null());
    bool is_edge_new(uint64_t source, uint64_t target);

public:
    // Override get_neighbors to invoke compute_fix_point when needed
    std::pair<const uint64_t*, size_t> get_neighbors(uint64_t vid) const override;

    // Getters for debugging/statistics
    size_t get_iterations_count() const { return iterations_count; }
    size_t get_total_edges() { return get_store() ? get_store()->size() : 0; }
    bool has_reached_fix_point() const { return fix_point_reached; }

    // void set_lazy(bool _lazy) { lazy = _lazy; }

    TwoColumnBindingIter *get_child() { return child.get(); }

    void accept_visitor(BindingIterVisitor& visitor) override;
};

} // namespace CustomOps