#pragma once

#include "query/executor/binding_iter/custom_ops/custom_operator_base.h"
#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include <memory>
#include <vector>
#include <unordered_set>

namespace CustomOps {

// TI (Traverse-Intersect) Operator
// The leftmost operand is a query subgraph, and all other operands are query edges or paths
// For each row in the leftmost operand result, neighbor lists of common vertices are sorted
// on demand, and multi-way set intersection is conducted.
class TIOperator : public CustomOperatorBase {
private:
    // Leftmost operand: query subgraph
    std::unique_ptr<BindingIter> leftmost_operand;
    const TwoColumnBindingIter *tcbit_leftmost;

    // Other operands: query edges or paths (two-column results)
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_path_operands;

    // Current state
    Binding* parent_binding_ptr;
    bool has_current_row;

    // Multi-way intersection state
    struct IntersectionState {
        std::vector<const uint64_t*> neighbor_lists;  // Sorted neighbor lists for intersection
        std::vector<std::vector<uint64_t>> copied_lists;    // Sorted neighbor lists that are copied out for epsilon
        std::vector<size_t> neighbor_nums;  // Length of sorted neighbor lists
        std::vector<size_t> current_positions;               // Current positions in each list
        bool intersection_complete;

        IntersectionState() : intersection_complete(false) {}
        void reset();
    } intersection_state;

    bool intersect_in_leftmost; // whether the intersect var is in leftmost operand
    bool left_epsilon_added;
    bool left_epsilon_terminated;
    std::unique_ptr<TwoColumnBindingIter> left_epsilon_biter;
    uint64_t cur_left_epsilon_src;
    bool leftmost_has_constant_col1;
    ObjectId leftmost_constant_col1;
    bool only_edge_path_has_constant_col2;
    ObjectId only_edge_path_constant_col2;

protected:
    void _begin(Binding& parent_binding) override;
    bool _next() override;
    void _reset() override;

public:
    // Constructor takes leftmost operand and edge/path operands
    TIOperator(std::unique_ptr<BindingIter> _leftmost_operand,
               std::vector<std::unique_ptr<TwoColumnBindingIter>> _edge_path_operands,
            bool _intersect_in_leftmost=false);

    ~TIOperator();

    void assign_nulls() override;
    void accept_visitor(BindingIterVisitor& visitor) override;

    OperatorType get_operator_type() const override {
        return OperatorType::TI;
    }

    // TI-specific methods

    // Find common vertices between leftmost operand result and edge/path operands
    // std::vector<int> find_common_vertices(const Binding& leftmost_binding);

    // Sort neighbor lists on demand for common vertices
    void sort_neighbor_lists_on_demand();
    void sort_neighbor_lists_on_demand_left_epsilon();

    // Conduct multi-way set intersection on sorted neighbor lists
    bool conduct_multi_way_intersection();

    // Get next intersection result
    bool get_next_intersection_result();

    // Reset intersection state for new leftmost operand row
    void reset_intersection_for_new_row();
    void reset_intersection_for_new_row_left_epsilon();

    // Statistics and diagnostics
    size_t get_num_edge_path_operands() const { return edge_path_operands.size(); }

    // Additional getter methods for detailed printing
    bool has_intersection_in_leftmost() const { return intersect_in_leftmost; }
    bool is_intersection_complete() const { return intersection_state.intersection_complete; }
    size_t get_neighbor_lists_count() const { return intersection_state.neighbor_lists.size(); }
    bool has_leftmost_constant_col1() const { return leftmost_has_constant_col1; }
    ObjectId get_leftmost_constant_col1() const { return leftmost_constant_col1; }
    bool has_only_edge_path_constant_col2() const { return only_edge_path_has_constant_col2; }
    ObjectId get_only_edge_path_constant_col2() const { return only_edge_path_constant_col2; }
    BindingIter *get_leftmost_operand() { return leftmost_operand.get(); }
    size_t get_num_edge_path_operands() { return edge_path_operands.size(); }
    BindingIter *get_edge_path_operand(size_t i) {
        if (i < edge_path_operands.size()) return edge_path_operands[i].get();
        return nullptr;
    }
};

} // namespace CustomOps