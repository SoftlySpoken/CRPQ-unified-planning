#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_set>

#include "query/executor/binding_iter/custom_ops/mc_operator.h"
#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include "storage/custom_buffer/two_column_store.h"
#include "query/executor/binding_iter_visitor.h"
#include "query/executor/binding.h"
#include "query/var_id.h"

using namespace CustomOps;

typedef bool TestFunction();

// Mock TwoColumnStoreBindingIter for testing
class MockTwoColumnStoreBindingIter : public TwoColumnStoreBindingIter {
public:
    MockTwoColumnStoreBindingIter(const std::vector<std::pair<uint64_t, uint64_t>>& edges,
                                  VarId col1_var, VarId col2_var)
        : TwoColumnStoreBindingIter(create_test_store(edges), col1_var, col2_var) {
        std::cout << "MockTwoColumnStoreBindingIter store size = " << this->get_store()->size() << std::endl;
    }

private:
    static std::unique_ptr<TwoColumnStore> create_test_store(const std::vector<std::pair<uint64_t, uint64_t>>& edges) {
        auto store = std::make_unique<TwoColumnStore>();
        for (const auto& edge : edges) {
            store->append(edge.first, edge.second);
        }
        store->compact();
        return store;
    }
};

// Mock TwoColumnBindingIter with constant col1 for testing
class MockConstantCol1BindingIter : public TwoColumnBindingIter {
private:
    std::vector<uint64_t> col2_values;
    size_t current_index;

public:
    MockConstantCol1BindingIter(uint64_t constant_col1_value, VarId col2_var,
                                const std::vector<uint64_t>& col2_values)
        : TwoColumnBindingIter(ObjectId(constant_col1_value), col2_var),
          col2_values(col2_values), current_index(0) {
        std::cout << "MockConstantCol1BindingIter with constant col1=" << constant_col1_value
                  << ", col2_values size=" << col2_values.size() << std::endl;
    }

protected:
    bool advance_to_next_edge() override {
        if (current_index >= col2_values.size()) {
            return false;
        }
        current_index++;
        return true;
    }

    void reset_iteration_state() override {
        current_index = 0;
    }

public:
    ~MockConstantCol1BindingIter() {}
    void get_current_edge(std::pair<ObjectId, ObjectId>& pr) override {
        if (current_index == 0 || current_index > col2_values.size()) {
            pr.first = ObjectId::get_null();
            pr.second = ObjectId::get_null();
            return;
        }
        pr.first = constant_col1;
        pr.second = ObjectId(col2_values[current_index - 1]);
    }

    bool seek_to_vertex(uint64_t vertex) override {
        // For constant col1, we can only seek to that constant value
        if (vertex == constant_col1.id) {
            current_index = 0;
            return !col2_values.empty();
        }
        return false;
    }

    std::pair<const uint64_t*, size_t> get_neighbors(uint64_t vid) const override {
        // Simple implementation for testing
        if (vid == constant_col1.id && !col2_values.empty()) {
            return {col2_values.data(), col2_values.size()};
        }
        return {nullptr, 0};
    }

    void accept_visitor(BindingIterVisitor& visitor) override {
        // Simple implementation for testing - just return without doing anything
        // In a real implementation, this would call visitor.visit(*this)
        (void)visitor; // Suppress unused parameter warning
    }
};

bool test_basic_mc_concatenation() {
    std::cout << "Testing basic MC concatenation..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // Create test data:
    // A: 1->2, 2->3
    // B: 2->4, 3->5
    // Expected concatenation A/B: 1->4 (from 1->2->4), 2->5 (from 2->3->5)
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {2, 3}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {2, 4}, {3, 5}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // Expected: 1->4, 2->5 from initial concatenation, plus expansions
    std::vector<std::pair<uint64_t, uint64_t>> expected_minimal = {
        {1, 4}, {2, 5}
    };

    // Sort for comparison
    std::sort(actual_edges.begin(), actual_edges.end());

    // Check that we at least have the basic concatenation results
    bool has_basic_results = true;
    for (const auto& expected : expected_minimal) {
        if (std::find(actual_edges.begin(), actual_edges.end(), expected) == actual_edges.end()) {
            std::cerr << "Missing expected edge: (" << expected.first << "," << expected.second << ")" << std::endl;
            has_basic_results = false;
        }
    }

    if (!has_basic_results) {
        return true;
    }

    std::cout << "Basic MC concatenation test passed!" << std::endl;
    return false;
}

bool test_mc_empty_input_A() {
    std::cout << "Testing MC operator with empty input A..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {};
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {1, 2}, {2, 3}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    if (mc_op.next()) {
        std::cerr << "Expected no results for empty input A" << std::endl;
        return true;
    }

    std::cout << "MC empty input A test passed!" << std::endl;
    return false;
}

bool test_mc_empty_input_B() {
    std::cout << "Testing MC operator with empty input B..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {2, 3}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {};

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    if (mc_op.next()) {
        std::cerr << "Expected no results for empty input B" << std::endl;
        return true;
    }

    std::cout << "MC empty input B test passed!" << std::endl;
    return false;
}

bool test_mc_no_join_match() {
    std::cout << "Testing MC operator with no join matches..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // A: 1->2, 3->4
    // B: 5->6, 7->8
    // No target from A matches source from B
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {3, 4}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {5, 6}, {7, 8}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    if (mc_op.next()) {
        std::cerr << "Expected no results when no join matches exist" << std::endl;
        return true;
    }

    std::cout << "MC no join match test passed!" << std::endl;
    return false;
}

bool test_mc_left_expansion() {
    std::cout << "Testing MC left expansion..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // A: 1->2, 2->3, 4->5  (chain 1->2->3 and isolated edge 4->5)
    // B: 3->6  (connects to end of chain)
    // Initial concatenation: 2->6 (from 2->3->6)
    // Left expansion should add: 1->6 (from 1->2->3->6)
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {2, 3}, {4, 5}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {3, 6}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // Should include both 2->6 (initial) and 1->6 (left expansion)
    std::vector<std::pair<uint64_t, uint64_t>> expected_edges = {
        {2, 6}, {1, 6}
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() < expected_edges.size()) {
        std::cerr << "Expected at least " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (const auto& expected : expected_edges) {
        if (std::find(actual_edges.begin(), actual_edges.end(), expected) == actual_edges.end()) {
            std::cerr << "Missing expected edge: (" << expected.first << "," << expected.second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "MC left expansion test passed!" << std::endl;
    return false;
}

bool test_mc_right_expansion() {
    std::cout << "Testing MC right expansion..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // A: 1->2
    // B: 2->3, 3->4, 5->6  (chain 2->3->4 and isolated edge 5->6)
    // Initial concatenation: 1->3 (from 1->2->3)
    // Right expansion should add: 1->4 (from 1->2->3->4)
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {2, 3}, {3, 4}, {5, 6}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // Should include both 1->3 (initial) and 1->4 (right expansion)
    std::vector<std::pair<uint64_t, uint64_t>> expected_edges = {
        {1, 3}, {1, 4}
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() < expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (const auto& expected : expected_edges) {
        if (std::find(actual_edges.begin(), actual_edges.end(), expected) == actual_edges.end()) {
            std::cerr << "Missing expected edge: (" << expected.first << "," << expected.second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "MC right expansion test passed!" << std::endl;
    return false;
}

bool test_mc_complex_expansion() {
    std::cout << "Testing MC complex expansion..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // A: 1->2, 2->3, 4->1  (complex structure with paths and cycles)
    // B: 3->5, 5->6
    // Should produce various combinations through both left and right expansion
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {2, 3}, {4, 1}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {3, 5}, {5, 6}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // Expected minimal results:
    // Initial: 2->5 (from 2->3->5)
    // Left expansion: 1->5 (from 1->2->3->5), 4->5 (from 4->1->2->3->5)
    // Right expansion: 2->6, 1->6, 4->6 (from ->5->6)
    std::vector<std::pair<uint64_t, uint64_t>> expected_minimal = {
        {2, 5}, {1, 5}, {4, 5}, {2, 6}, {1, 6}, {4, 6}
    };

    if (actual_edges.size() < expected_minimal.size()) {
        std::cerr << "Expected at least " << expected_minimal.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (const auto& expected : expected_minimal) {
        if (std::find(actual_edges.begin(), actual_edges.end(), expected) == actual_edges.end()) {
            std::cerr << "Missing expected edge: (" << expected.first << "," << expected.second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "MC complex expansion test passed!" << std::endl;
    return false;
}

bool test_mc_reset_functionality() {
    std::cout << "Testing MC reset functionality..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {2, 3}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {3, 4}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> first_run;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        first_run.push_back({source, target});
    }

    mc_op.reset();
    mc_op.begin(binding);
    std::vector<std::pair<uint64_t, uint64_t>> second_run;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        second_run.push_back({source, target});
    }

    std::sort(first_run.begin(), first_run.end());
    std::sort(second_run.begin(), second_run.end());

    if (first_run != second_run) {
        std::cerr << "Reset didn't produce identical results" << std::endl;
        std::cerr << "First run size: " << first_run.size() << ", Second run size: " << second_run.size() << std::endl;
        return true;
    }

    std::cout << "MC reset functionality test passed!" << std::endl;
    return false;
}

bool test_mc_statistics() {
    std::cout << "Testing MC operator statistics..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {2, 3}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {3, 4}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    // Consume all results
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cerr << source << " " << target << std::endl;
    }

    // Check statistics
    size_t final_size = mc_op.get_current_phase_size();
    if (final_size != 2) {
        std::cerr << "Expected result count = 2, actual result count = " << final_size << std::endl;
        return true;
    }

    std::cout << "Final phase size: " << final_size << std::endl;
    std::cout << "MC statistics test passed!" << std::endl;
    return false;
}

bool test_mc_single_element_sets() {
    std::cout << "Testing MC operator with single element sets..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // A: 1->2
    // B: 2->3
    // Should produce: 1->3
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {2, 3}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    if (actual_edges.size() != 1) {
        std::cerr << "Expected exactly 1 edge, got " << actual_edges.size() << std::endl;
        return true;
    }

    if (actual_edges[0] != std::make_pair((uint64_t)1, (uint64_t)3)) {
        std::cerr << "Expected edge (1,3), got (" << actual_edges[0].first << "," << actual_edges[0].second << ")" << std::endl;
        return true;
    }

    std::cout << "MC single element sets test passed!" << std::endl;
    return false;
}

bool test_mc_left_epsilon_true_right_epsilon_false() {
    std::cout << "Testing MC operator with left_epsilon=true, right_epsilon=false..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // A: 1->3, 3->5
    // B: 5->6, 7->8
    // With left_epsilon=true, right_epsilon=false, should return all edges from B
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 3}, {3, 5}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {5, 6}, {7, 8}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), true, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // Should contain all edges from B: {5, 6}, {7, 8}
    std::vector<std::pair<uint64_t, uint64_t>> expected_edges = {
        {5, 6}, {7, 8}, {3, 6}, {1, 6}
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected_edges.size(); ++i) {
        if (actual_edges[i] != expected_edges[i]) {
            std::cerr << "Mismatch at index " << i << ": expected (" << expected_edges[i].first
                      << "," << expected_edges[i].second << "), got (" << actual_edges[i].first
                      << "," << actual_edges[i].second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "MC left_epsilon=true, right_epsilon=false test passed!" << std::endl;
    return false;
}

bool test_mc_left_epsilon_false_right_epsilon_true() {
    std::cout << "Testing MC operator with left_epsilon=false, right_epsilon=true..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // A: 1->2, 3->4
    // B: 5->6, 7->8
    // With left_epsilon=false, right_epsilon=true, should return all edges from A
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {3, 4}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {4, 6}, {6, 8}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, true);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // Should contain all edges from A: {1, 2}, {3, 4}
    std::vector<std::pair<uint64_t, uint64_t>> expected_edges = {
        {1, 2}, {3, 4}, {3, 6}, {3, 8}
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected_edges.size(); ++i) {
        if (actual_edges[i] != expected_edges[i]) {
            std::cerr << "Mismatch at index " << i << ": expected (" << expected_edges[i].first
                      << "," << expected_edges[i].second << "), got (" << actual_edges[i].first
                      << "," << actual_edges[i].second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "MC left_epsilon=false, right_epsilon=true test passed!" << std::endl;
    return false;
}

bool test_mc_left_epsilon_true_right_epsilon_true() {
    std::cout << "Testing MC operator with left_epsilon=true, right_epsilon=true..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // A: 1->2, 3->4
    // B: 5->6, 7->8
    // With left_epsilon=true, right_epsilon=true, should perform normal concatenation (no special epsilon behavior)
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {2, 4}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {2, 10}, {4, 11}  // Make these join with A for testing normal concatenation
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), true, true);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // Should perform normal concatenation: 1->2->10 = 1->10, 3->4->11 = 3->11
    std::vector<std::pair<uint64_t, uint64_t>> expected_edges = {
        {1, 10}, {2, 11}, {1, 2}, {2, 4}, {2, 10}, {4, 11}, {1, 4}
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() < expected_edges.size()) {
        std::cerr << "Expected at least " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    // Check that we have at least the basic concatenation results
    for (const auto& expected : expected_edges) {
        if (std::find(actual_edges.begin(), actual_edges.end(), expected) == actual_edges.end()) {
            std::cerr << "Missing expected edge: (" << expected.first << "," << expected.second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "MC left_epsilon=true, right_epsilon=true test passed!" << std::endl;
    return false;
}

bool test_mc_left_epsilon_false_right_epsilon_false() {
    std::cout << "Testing MC operator with left_epsilon=false, right_epsilon=false (standard behavior)..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // A: 1->2, 3->4
    // B: 2->10, 4->11
    // With left_epsilon=false, right_epsilon=false, should perform normal concatenation
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {1, 2}, {3, 4}
    };
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {2, 10}, {4, 11}
    };

    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, source_var, target_var);
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, source_var, target_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        uint64_t source = binding[source_var].id;
        uint64_t target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // Should perform normal concatenation: 1->2->10 = 1->10, 3->4->11 = 3->11
    std::vector<std::pair<uint64_t, uint64_t>> expected_minimal = {
        {1, 10}, {3, 11}
    };

    std::sort(actual_edges.begin(), actual_edges.end());

    if (actual_edges.size() < expected_minimal.size()) {
        std::cerr << "Expected at least " << expected_minimal.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    // Check that we have at least the basic concatenation results
    for (const auto& expected : expected_minimal) {
        if (std::find(actual_edges.begin(), actual_edges.end(), expected) == actual_edges.end()) {
            std::cerr << "Missing expected edge: (" << expected.first << "," << expected.second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "MC left_epsilon=false, right_epsilon=false test passed!" << std::endl;
    return false;
}

bool test_mc_child_a_constant_col1() {
    std::cout << "Testing MC operator with child_A having constant col1..." << std::endl;

    VarId target_var(1);  // Only col2 is variable, col1 is constant

    // Child A: constant col1=10, with col2 values: 20, 30
    // This represents edges: 10->20, 10->30
    std::vector<uint64_t> child_A_col2_values = {20, 30};
    auto child_A = std::make_unique<MockConstantCol1BindingIter>(10, target_var, child_A_col2_values);

    // Child B: 20->40, 30->50
    // This should join with child_A on the values 20 and 30
    std::vector<std::pair<uint64_t, uint64_t>> edges_B = {
        {20, 40}, {30, 50}
    };
    VarId source_var(0);  // For child_B, both columns are variables
    auto child_B = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_B, target_var, source_var);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        // Since child_A has constant col1=10, the result should be 10->col2_from_B
        // We need to check what variables are bound
        uint64_t col1_value = 10;  // This is the constant from child_A
        uint64_t col2_value = binding[source_var].id;
        std::cout << col1_value << " " << col2_value << std::endl;
        actual_edges.push_back({col1_value, col2_value});
    }

    // Expected concatenation:
    // child_A: 10->20, 10->30
    // child_B: 20->40, 30->50
    // MC result: 10->40 (from 10->20->40), 10->50 (from 10->30->50)
    std::vector<std::pair<uint64_t, uint64_t>> expected_edges = {
        {10, 40}, {10, 50}
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    // Check that we have at least the basic concatenation results
    for (const auto& expected : expected_edges) {
        if (std::find(actual_edges.begin(), actual_edges.end(), expected) == actual_edges.end()) {
            std::cerr << "Missing expected edge: (" << expected.first << "," << expected.second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "MC child_A constant col1 test passed!" << std::endl;
    return false;
}

bool test_mc_child_b_constant_col1() {
    std::cout << "Testing MC operator with child_B having constant col1..." << std::endl;

    VarId x(0), y(1), z(2);

    // Child A: 10->20, 10->30
    // This should produce intermediate targets 20 and 30
    std::vector<std::pair<uint64_t, uint64_t>> edges_A = {
        {10, 20}, {10, 30}
    };
    auto child_A = std::make_unique<MockTwoColumnStoreBindingIter>(
        edges_A, x, y);

    // Child B: constant col1=20, with col2 values: 40, 50
    // This represents edges: 20->40, 20->50
    // Only the edge 20->40 and 20->50 should match with child_A's output (20)
    std::vector<uint64_t> child_B_col2_values = {40, 50};
    auto child_B = std::make_unique<MockConstantCol1BindingIter>(20, z, child_B_col2_values);

    MCOperator mc_op(std::move(child_A), std::move(child_B), false, false);

    Binding binding(10);
    mc_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual_edges;
    while (mc_op.next()) {
        // The result should be source_from_A -> col2_from_B
        uint64_t col1_value = binding[x].id;
        uint64_t col2_value = binding[z].id;
        std::cout << col1_value << " " << col2_value << std::endl;
        actual_edges.push_back({col1_value, col2_value});
    }

    // Expected concatenation:
    // child_A: 10->20, 10->30
    // child_B: 20->40, 20->50 (only 20 matches from child_A)
    // MC result: 10->40 (from 10->20->40), 10->50 (from 10->20->50)
    // Note: 10->30 doesn't join with anything from child_B since child_B only has col1=20
    std::vector<std::pair<uint64_t, uint64_t>> expected_edges = {
        {10, 40}, {10, 50}
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    // Check that we have at least the basic concatenation results
    for (const auto& expected : expected_edges) {
        if (std::find(actual_edges.begin(), actual_edges.end(), expected) == actual_edges.end()) {
            std::cerr << "Missing expected edge: (" << expected.first << "," << expected.second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "MC child_B constant col1 test passed!" << std::endl;
    return false;
}

int main() {
    std::vector<TestFunction*> tests;

    tests.push_back(&test_basic_mc_concatenation);
    tests.push_back(&test_mc_empty_input_A);
    tests.push_back(&test_mc_empty_input_B);
    tests.push_back(&test_mc_no_join_match);
    tests.push_back(&test_mc_left_expansion);
    tests.push_back(&test_mc_right_expansion);
    tests.push_back(&test_mc_complex_expansion);
    tests.push_back(&test_mc_reset_functionality);
    tests.push_back(&test_mc_statistics);
    tests.push_back(&test_mc_single_element_sets);

    // Epsilon behavior tests
    tests.push_back(&test_mc_left_epsilon_true_right_epsilon_false);
    tests.push_back(&test_mc_left_epsilon_false_right_epsilon_true);
    tests.push_back(&test_mc_left_epsilon_true_right_epsilon_true);
    tests.push_back(&test_mc_left_epsilon_false_right_epsilon_false);

    // Constant column tests
    tests.push_back(&test_mc_child_a_constant_col1);
    tests.push_back(&test_mc_child_b_constant_col1);

    auto error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
    }

    if (!error) {
        std::cout << "\nAll MCOperator tests passed!" << std::endl;
    } else {
        std::cout << "\nSome MCOperator tests failed!" << std::endl;
    }

    return error;
}