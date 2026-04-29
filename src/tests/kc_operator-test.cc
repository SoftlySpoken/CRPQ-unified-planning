#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_set>

#include "query/executor/binding_iter/custom_ops/kc_operator.h"
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
    MockTwoColumnStoreBindingIter(const std::vector<std::pair<long long, long long>>& edges,
                                  VarId col1_var, VarId col2_var)
        : TwoColumnStoreBindingIter(create_test_store(edges), col1_var, col2_var) {
        std::cout << "MockTwoColumnStoreBindingIter store size = " << this->get_store()->size() << std::endl;
    }

    // Constructor with constant col1 value
    MockTwoColumnStoreBindingIter(const std::vector<std::pair<long long, long long>>& edges,
                                  ObjectId constant_col1_value, VarId col2_var)
        : TwoColumnStoreBindingIter(create_test_store(edges), constant_col1_value, col2_var) {
        std::cout << "MockTwoColumnStoreBindingIter (constant col1) store size = " << this->get_store()->size() << std::endl;
    }

private:
    static std::unique_ptr<TwoColumnStore> create_test_store(const std::vector<std::pair<long long, long long>>& edges) {
        auto store = std::make_unique<TwoColumnStore>();
        for (const auto& edge : edges) {
            store->append(edge.first, edge.second);
        }
        store->compact();
        return store;
    }
};

bool test_basic_kc_transitive_closure() {
    std::cout << "Testing basic KC transitive closure..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // Create test data: 1->2, 2->3
    // Expected result: 1->2, 2->3, 1->3 (transitive closure)
    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), source_var, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (kc_op.next()) {
        long long source = binding[source_var].id;
        long long target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // Expected edges: original + transitive closure
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 2}, {2, 3}, {1, 3}
    };

    // Sort both vectors for comparison
    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected_edges.size(); ++i) {
        if (actual_edges[i] != expected_edges[i]) {
            std::cerr << "Mismatch at position " << i << ": got ("
                      << actual_edges[i].first << "," << actual_edges[i].second
                      << "), expected (" << expected_edges[i].first << ","
                      << expected_edges[i].second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "Basic KC transitive closure test passed!" << std::endl;
    return false;
}

bool test_kc_cycle_handling() {
    std::cout << "Testing KC operator with cycles..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // Create test data with a cycle: 1->2, 2->3, 3->1
    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}, {3, 1}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), source_var, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (kc_op.next()) {
        long long source = binding[source_var].id;
        long long target = binding[target_var].id;
        std::cout << source << " " << target << std::endl;
        actual_edges.push_back({source, target});
    }

    // With a cycle, we should get all possible connections
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 2}, {2, 3}, {3, 1}, // original
        {1, 3}, {2, 1}, {3, 2}, {1, 1}, {2, 2}, {3, 3}  // transitive closure
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected_edges.size(); ++i) {
        if (actual_edges[i] != expected_edges[i]) {
            std::cerr << "Mismatch at position " << i << ": got ("
                      << actual_edges[i].first << "," << actual_edges[i].second
                      << "), expected (" << expected_edges[i].first << ","
                      << expected_edges[i].second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "KC cycle handling test passed!" << std::endl;
    return false;
}

bool test_kc_empty_input() {
    std::cout << "Testing KC operator with empty input..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    std::vector<std::pair<long long, long long>> input_edges = {};

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), source_var, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    if (kc_op.next()) {
        std::cerr << "Expected no results for empty input" << std::endl;
        return true;
    }

    std::cout << "KC empty input test passed!" << std::endl;
    return false;
}

bool test_kc_single_edge() {
    std::cout << "Testing KC operator with single edge..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), source_var, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (kc_op.next()) {
        long long source = binding[source_var].id;
        long long target = binding[target_var].id;
        actual_edges.push_back({source, target});
    }

    // Should only have the original edge
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 2}
    };

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    if (actual_edges[0] != expected_edges[0]) {
        std::cerr << "Edge mismatch: got (" << actual_edges[0].first << ","
                  << actual_edges[0].second << "), expected ("
                  << expected_edges[0].first << "," << expected_edges[0].second << ")" << std::endl;
        return true;
    }

    std::cout << "KC single edge test passed!" << std::endl;
    return false;
}

bool test_kc_disconnected_components() {
    std::cout << "Testing KC operator with disconnected components..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    // Two disconnected chains: 1->2->3 and 4->5
    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}, {4, 5}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), source_var, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (kc_op.next()) {
        long long source = binding[source_var].id;
        long long target = binding[target_var].id;
        actual_edges.push_back({source, target});
    }

    // Expected: original edges + transitive closure within each component
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 2}, {2, 3}, {4, 5}, // original
        {1, 3}  // transitive closure of first component
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " edges, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected_edges.size(); ++i) {
        if (actual_edges[i] != expected_edges[i]) {
            std::cerr << "Mismatch at position " << i << ": got ("
                      << actual_edges[i].first << "," << actual_edges[i].second
                      << "), expected (" << expected_edges[i].first << ","
                      << expected_edges[i].second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "KC disconnected components test passed!" << std::endl;
    return false;
}

bool test_kc_statistics() {
    std::cout << "Testing KC operator statistics..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), source_var, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    // Consume all results
    while (kc_op.next()) {}

    // Check statistics
    if (kc_op.get_iterations_count() == 0) {
        std::cerr << "Expected positive iteration count" << std::endl;
        return true;
    }

    if (kc_op.get_total_edges() != 3) { // 1->2, 2->3, 1->3
        std::cerr << "Expected 3 total edges, got " << kc_op.get_total_edges() << std::endl;
        return true;
    }

    if (!kc_op.has_reached_fix_point()) {
        std::cerr << "Expected fix-point to be reached" << std::endl;
        return true;
    }

    std::cout << "KC statistics test passed!" << std::endl;
    return false;
}

bool test_kc_reset_functionality() {
    std::cout << "Testing KC reset functionality..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);

    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), source_var, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    std::vector<std::pair<long long, long long>> first_run;
    while (kc_op.next()) {
        long long source = binding[source_var].id;
        long long target = binding[target_var].id;
        first_run.push_back({source, target});
    }

    kc_op.reset();
    std::vector<std::pair<long long, long long>> second_run;
    while (kc_op.next()) {
        long long source = binding[source_var].id;
        long long target = binding[target_var].id;
        second_run.push_back({source, target});
    }

    if (first_run != second_run) {
        std::cerr << "Reset didn't produce identical results" << std::endl;
        return true;
    }

    std::cout << "KC reset functionality test passed!" << std::endl;
    return false;
}

bool test_kc_constant_col1_basic() {
    std::cout << "Testing KC operator with constant col1 (basic)..." << std::endl;

    VarId source_var(0), target_var(1);
    ObjectId constant_source(1); // Only paths starting from vertex 1

    // Create test data: 1->2, 2->3, 4->5, 1->4
    // With constant col1=1, we should only get paths starting from vertex 1
    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}, {4, 5}, {1, 4}, {5, 6}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), constant_source, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (kc_op.next()) {
        // Only target_var should be bound since col1 is constant
        long long target = binding[target_var].id;
        std::cout << "1 -> " << target << std::endl;
        actual_edges.emplace_back(1, target);
    }

    // Expected targets reachable from vertex 1: 2, 3, 4, 5
    // (1->2, 1->2->3, 1->4, 1->4->5)
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 2}, {1, 3}, {1, 5}, {1, 4}, {1, 6}
    };

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " targets, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected_edges.size(); ++i) {
        if (actual_edges[i] != expected_edges[i]) {
            std::cerr << "Mismatch at position " << i << ": got " << actual_edges[i].first << " " << actual_edges[i].second
                      << ", expected " << expected_edges[i].first << " " << expected_edges[i].second << std::endl;
            return true;
        }
    }

    std::cout << "KC constant col1 basic test passed!" << std::endl;
    return false;
}

bool test_kc_constant_col1_cycle() {
    std::cout << "Testing KC operator with constant col1 and cycles..." << std::endl;

    VarId source_var(0), target_var(1);
    ObjectId constant_source(1); // Only paths starting from vertex 1

    // Create test data with cycle: 1->2, 2->3, 3->1, 4->5
    // With constant col1=1, we should get all reachable vertices from 1 (which is all in the cycle)
    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}, {3, 1}, {4, 5}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), constant_source, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (kc_op.next()) {
        long long target = binding[target_var].id;
        std::cout << "1 -> " << target << std::endl;
        actual_edges.emplace_back(1, target);
    }

    // Expected targets reachable from vertex 1: 1, 2, 3 (cycle makes all reachable)
    // Note: 4, 5 are not reachable from 1
    std::vector<std::pair<long long, long long>> expected_edges = {{1, 1}, {1, 2}, {1, 3}};

    std::sort(actual_edges.begin(), actual_edges.end());
    std::sort(expected_edges.begin(), expected_edges.end());

    if (actual_edges.size() != expected_edges.size()) {
        std::cerr << "Expected " << expected_edges.size() << " targets, got " << actual_edges.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected_edges.size(); ++i) {
        if (actual_edges[i] != expected_edges[i]) {
            std::cerr << "Mismatch at position " << i << ": got " << actual_edges[i].first << " " << actual_edges[i].second
                      << ", expected " << expected_edges[i].first << " " << expected_edges[i].second << std::endl;
            return true;
        }
    }

    std::cout << "KC constant col1 cycle test passed!" << std::endl;
    return false;
}

bool test_kc_constant_col1_optimization() {
    std::cout << "Testing KC operator constant col1 optimization..." << std::endl;

    VarId source_var(0);
    VarId target_var(1);
    ObjectId constant_source(1);

    // Create test data: 1->2, 2->3, 4->5, 6->7, 1->6
    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}, {4, 5}, {6, 7}, {1, 6}
    };

    // Test 1: Full closure (no constant)
    auto child1 = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);
    KCOperator kc_op_full(std::move(child1), source_var, target_var, false);

    Binding binding1(10);
    kc_op_full.begin(binding1);

    std::vector<std::pair<long long, long long>> full_edges;
    while (kc_op_full.next()) {
        long long source = binding1[source_var].id;
        long long target = binding1[target_var].id;
        full_edges.push_back({source, target});
    }

    // Test 2: Constant col1 closure
    auto child2 = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);
    KCOperator kc_op_const(std::move(child2), constant_source, target_var, false);

    Binding binding2(10);
    kc_op_const.begin(binding2);

    std::vector<long long> const_targets;
    while (kc_op_const.next()) {
        long long target = binding2[target_var].id;
        const_targets.push_back(target);
    }

    // Filter full_edges to only those starting from vertex 1
    std::vector<long long> filtered_targets;
    for (const auto& edge : full_edges) {
        if (edge.first == 1) {
            filtered_targets.push_back(edge.second);
        }
    }

    std::sort(const_targets.begin(), const_targets.end());
    std::sort(filtered_targets.begin(), filtered_targets.end());

    // The constant col1 version should produce the same targets as filtering the full result
    if (const_targets != filtered_targets) {
        std::cerr << "Constant col1 optimization produces different results than expected" << std::endl;
        std::cerr << "Constant targets: ";
        for (auto t : const_targets) std::cerr << t << " ";
        std::cerr << "\nFiltered targets: ";
        for (auto t : filtered_targets) std::cerr << t << " ";
        std::cerr << std::endl;
        return true;
    }

    // The constant version should be more efficient (fewer iterations or edges processed)
    if (kc_op_const.get_total_edges() > kc_op_full.get_total_edges()) {
        std::cerr << "Expected constant col1 version to produce fewer edges" << std::endl;
        return true;
    }

    std::cout << "KC constant col1 optimization test passed!" << std::endl;
    return false;
}

bool test_kc_constant_col1_no_edges() {
    std::cout << "Testing KC operator with constant col1 (no matching edges)..." << std::endl;

    VarId source_var(0), target_var(1);
    ObjectId constant_source(10); // Vertex that doesn't exist in input

    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}, {4, 5}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, source_var, target_var);

    KCOperator kc_op(std::move(child), constant_source, target_var, false);

    Binding binding(10);
    kc_op.begin(binding);

    if (kc_op.next()) {
        std::cerr << "Expected no results for non-existent constant source" << std::endl;
        return true;
    }

    std::cout << "KC constant col1 no edges test passed!" << std::endl;
    return false;
}

int main() {
    std::vector<TestFunction*> tests;

    tests.push_back(&test_basic_kc_transitive_closure);
    tests.push_back(&test_kc_cycle_handling);
    tests.push_back(&test_kc_empty_input);
    tests.push_back(&test_kc_single_edge);
    tests.push_back(&test_kc_disconnected_components);
    tests.push_back(&test_kc_statistics);
    tests.push_back(&test_kc_reset_functionality);

    // constant_col1 test cases
    tests.push_back(&test_kc_constant_col1_basic);
    tests.push_back(&test_kc_constant_col1_cycle);
    tests.push_back(&test_kc_constant_col1_optimization);
    tests.push_back(&test_kc_constant_col1_no_edges);

    auto error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
    }

    if (!error) {
        std::cout << "\nAll KCOperator tests passed!" << std::endl;
    } else {
        std::cout << "\nSome KCOperator tests failed!" << std::endl;
    }

    return error;
}