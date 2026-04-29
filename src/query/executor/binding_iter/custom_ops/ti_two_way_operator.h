#pragma once

#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include <memory>
#include <unordered_set>

namespace CustomOps {

// TI Two-Way Operator
// A simplified version of TIOperator for the case when edge_path_operands.size() == 1
// Inherits from TwoColumnStoreBindingIter to provide a two-column iterator interface
// For each row in the leftmost operand, iterates through neighbors of the common vertex
// and binds them to the col2_var of the edge/path operand
class TITwoWayOperator : public TwoColumnStoreBindingIter {
private:
    // Leftmost operand: two-column store binding iterator
    std::unique_ptr<TwoColumnBindingIter> leftmost_operand;

    // Edge/path operand: two-column store binding iterator
    std::unique_ptr<TwoColumnBindingIter> edge_path_operand;

    // The common variable between leftmost operand and the edge/path operand
    VarId common_var;

    // Result store for all computed results
    std::unique_ptr<TwoColumnStore> result_store;

    // Current state (kept for compatibility but may not be needed)
    Binding* parent_binding_ptr;
    bool has_current_leftmost_row;
    bool has_current_neighbor;

    // Helper methods for computing all results
    void compute_all_results();
    void compute_join_results();
    void compute_epsilon_results();

    void compute_results_for_vertex(uint64_t vertex) const;

    // Tracking for lazy computation
    mutable std::unordered_set<uint64_t> computed_vertices;
    mutable bool global_computation_done;

    int max_var_id;
    bool ep_has_const_col2;
    ObjectId ep_const_col2;
    void init_max_var_id();

protected:
    void _begin(Binding& parent_binding) override;

public:
    // Constructor takes leftmost operand, edge/path operand, variable names, and common variable
    TITwoWayOperator(std::unique_ptr<TwoColumnBindingIter> _leftmost_operand,
                     std::unique_ptr<TwoColumnBindingIter> _edge_path_operand,
                    VarId col1_var,
                    VarId col2_var,
                    bool epsilon_=false, bool _lazy=false);
    TITwoWayOperator(std::unique_ptr<TwoColumnBindingIter> _leftmost_operand,
                     std::unique_ptr<TwoColumnBindingIter> _edge_path_operand,
                    ObjectId constant_col1_value,
                    VarId col2_var,
                    bool epsilon_=false, bool _lazy=false);
    TITwoWayOperator(std::unique_ptr<TwoColumnBindingIter> _leftmost_operand,
                     std::unique_ptr<TwoColumnBindingIter> _edge_path_operand,
                     VarId col1_var,
                    ObjectId constant_col2_value,
                    bool epsilon_=false, bool _lazy=false);

    ~TITwoWayOperator() = default;

    void assign_nulls() override;
    void accept_visitor(BindingIterVisitor& visitor) override;

    // Get the common variable
    VarId get_common_var() const { return common_var; }

    // Additional getter methods for detailed printing
    TwoColumnBindingIter* get_leftmost_operand() { return leftmost_operand.get(); }
    TwoColumnBindingIter* get_edge_path_operand() { return edge_path_operand.get(); }
    bool get_has_current_leftmost_row() const { return has_current_leftmost_row; }
    bool get_has_current_neighbor() const { return has_current_neighbor; }
    size_t get_result_store_size() const { return result_store ? result_store->size() : 0; }

    // Override for lazy computation
    std::pair<const uint64_t*, size_t> get_neighbors(uint64_t vid) const override;
    bool seek_to_vertex(uint64_t vertex) override;
};

} // namespace CustomOps