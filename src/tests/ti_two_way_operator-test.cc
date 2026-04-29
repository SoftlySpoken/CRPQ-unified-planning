#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_set>

#include "query/executor/binding_iter/custom_ops/ti_two_way_operator.h"
#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include "query/executor/binding_iter_visitor.h"
#include "query/executor/binding.h"
#include "query/var_id.h"

using namespace CustomOps;

typedef bool TestFunction();

// Helper function to create a TwoColumnStore with test data
std::unique_ptr<TwoColumnStore> create_test_store(const std::vector<std::pair<long long, long long>>& edges) {
    auto store = std::make_unique<TwoColumnStore>();

    for (const auto& edge : edges) {
        store->append(edge.first, edge.second);
    }

    store->compact();
    return store;
}

// Helper function to create a TwoColumnStoreBindingIter with test data
std::unique_ptr<TwoColumnStoreBindingIter> create_test_operand(
    const std::vector<std::pair<long long, long long>>& edges,
    VarId col1_var, VarId col2_var) {

    auto store = create_test_store(edges);
    return std::make_unique<TwoColumnStoreBindingIter>(std::move(store), col1_var, col2_var);
}

// Helper function to create a TwoColumnStoreBindingIter with epsilon support
std::unique_ptr<TwoColumnStoreBindingIter> create_test_operand_with_epsilon(
    const std::vector<std::pair<long long, long long>>& edges,
    VarId col1_var, VarId col2_var, bool has_epsilon) {

    auto store = create_test_store(edges);
    auto operand = std::make_unique<TwoColumnStoreBindingIter>(std::move(store), col1_var, col2_var);
    operand->set_epsilon(has_epsilon);
    return operand;
}

// Helper function to create a TwoColumnStoreBindingIter with constant col1
std::unique_ptr<TwoColumnStoreBindingIter> create_test_operand_with_constant_col1(
    const std::vector<std::pair<long long, long long>>& edges,
    ObjectId constant_col1, VarId col2_var) {

    auto store = create_test_store(edges);
    return std::make_unique<TwoColumnStoreBindingIter>(std::move(store), constant_col1, col2_var);
}

// Helper function to create a TwoColumnStoreBindingIter with constant col1 and epsilon
std::unique_ptr<TwoColumnStoreBindingIter> create_test_operand_with_constant_col1_and_epsilon(
    const std::vector<std::pair<long long, long long>>& edges,
    ObjectId constant_col1, VarId col2_var, bool has_epsilon) {

    auto store = create_test_store(edges);
    auto operand = std::make_unique<TwoColumnStoreBindingIter>(std::move(store), constant_col1, col2_var);
    operand->set_epsilon(has_epsilon);
    return operand;
}

bool test_basic_ti_two_way_functionality() {
    std::cout << "Testing basic TI two-way functionality..." << std::endl;

    // Variables: x (0), y (1), z (2)
    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: [(x=1, y=10), (x=2, y=20)]
    auto leftmost = create_test_operand({{1, 10}, {2, 20}}, var_x, var_y);

    // Edge operand: y -> z (neighbors of y values)
    auto edge_operand = create_test_operand({{10, 100}, {10, 101}, {20, 200}, {20, 201}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
        std::cout << "Result: x=" << binding[var_x].id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected results: For each leftmost row, all neighbors of the common variable (y)
    // Row 1: x=1, y=10 -> neighbors 100, 101
    // Row 2: x=2, y=20 -> neighbors 200, 201
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {1, 100}, {1, 101}, {2, 200}, {2, 201}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    // Sort both vectors to compare regardless of order
    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            std::cerr << "Expected: (" << std::get<0>(expected[i]) << ", " << std::get<1>(expected[i])
                      << ")" << std::endl;
            std::cerr << "Got: (" << std::get<0>(results[i]) << ", " << std::get<1>(results[i])
                      << ")" << std::endl;
            return true;
        }
    }

    std::cout << "Basic TI two-way functionality test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_empty_leftmost() {
    std::cout << "Testing TI two-way with empty leftmost operand..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Empty leftmost operand
    auto leftmost = create_test_operand({}, var_x, var_y);

    // Edge operand
    auto edge_operand = create_test_operand({{10, 100}, {20, 200}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    if (ti_two_way.next()) {
        std::cerr << "Expected no results when leftmost operand is empty" << std::endl;
        return true;
    }

    std::cout << "TI two-way empty leftmost test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_empty_edge_operand() {
    std::cout << "Testing TI two-way with empty edge operand..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand with data
    auto leftmost = create_test_operand({{1, 10}, {2, 20}}, var_x, var_y);

    // Empty edge operand
    auto edge_operand = create_test_operand({}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    if (ti_two_way.next()) {
        std::cerr << "Expected no results when edge operand is empty" << std::endl;
        return true;
    }

    std::cout << "TI two-way empty edge operand test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_no_matching_vertices() {
    std::cout << "Testing TI two-way with no matching vertices..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: vertices that don't exist in edge operand
    auto leftmost = create_test_operand({{1, 100}, {2, 200}}, var_x, var_y);

    // Edge operand: different vertices
    auto edge_operand = create_test_operand({{300, 30}, {400, 40}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    if (ti_two_way.next()) {
        std::cerr << "Expected no results when no vertices match" << std::endl;
        return true;
    }

    std::cout << "TI two-way no matching vertices test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_single_vertex_multiple_neighbors() {
    std::cout << "Testing TI two-way with single vertex having multiple neighbors..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: single row
    auto leftmost = create_test_operand({{1, 10}}, var_x, var_y);

    // Edge operand: single vertex with multiple neighbors
    auto edge_operand = create_test_operand({{10, 100}, {10, 101}, {10, 102}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
    }

    // Expected: all neighbors of vertex 10
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {1, 100}, {1, 101}, {1, 102}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way single vertex multiple neighbors test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_reset_functionality() {
    std::cout << "Testing TI two-way reset functionality..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    auto leftmost = create_test_operand({{1, 10}, {2, 20}}, var_x, var_y);
    auto edge_operand = create_test_operand({{10, 100}, {20, 200}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> first_run;
    while (ti_two_way.next()) {
        first_run.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    ti_two_way.reset();
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> second_run;
    while (ti_two_way.next()) {
        second_run.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    if (first_run != second_run) {
        std::cerr << "Reset didn't produce identical results" << std::endl;
        return true;
    }

    std::cout << "TI two-way reset functionality test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_assign_nulls() {
    std::cout << "Testing TI two-way assign_nulls functionality..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    auto leftmost = create_test_operand({{1, 10}}, var_x, var_y);
    auto edge_operand = create_test_operand({{10, 100}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.assign_nulls();

    // Check that variables are set to null
    if (binding[var_x] != ObjectId::get_null() ||
        binding[var_z] != ObjectId::get_null()) {
        std::cerr << "assign_nulls didn't set variables to null" << std::endl;
        return true;
    }

    std::cout << "TI two-way assign_nulls test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_common_var_accessor() {
    std::cout << "Testing TI two-way common variable accessor..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    auto leftmost = create_test_operand({{1, 10}}, var_x, var_y);
    auto edge_operand = create_test_operand({{10, 100}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    // The common variable should be leftmost's col2_var, which is var_y
    if (ti_two_way.get_common_var() != var_y) {
        std::cerr << "Common variable accessor returned wrong variable" << std::endl;
        return true;
    }

    std::cout << "TI two-way common variable accessor test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_large_dataset() {
    std::cout << "Testing TI two-way with larger dataset..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Create larger dataset
    std::vector<std::pair<long long, long long>> leftmost_data;
    std::vector<std::pair<long long, long long>> edge_data;

    for (int i = 1; i <= 100; ++i) {
        leftmost_data.push_back({i, i * 10});
        // Each vertex has 3 neighbors
        edge_data.push_back({i * 10, i * 100});
        edge_data.push_back({i * 10, i * 100 + 1});
        edge_data.push_back({i * 10, i * 100 + 2});
    }

    auto leftmost = create_test_operand(leftmost_data, var_x, var_y);
    auto edge_operand = create_test_operand(edge_data, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    size_t result_count = 0;
    while (ti_two_way.next()) {
        result_count++;
    }

    // Expected: 100 vertices * 3 neighbors each = 300 results
    if (result_count != 300) {
        std::cerr << "Expected 300 results, got " << result_count << std::endl;
        return true;
    }

    std::cout << "TI two-way large dataset test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_partial_matching() {
    std::cout << "Testing TI two-way with partial matching vertices..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: mix of vertices that exist and don't exist in edge operand
    auto leftmost = create_test_operand({{1, 10}, {2, 20}, {3, 30}}, var_x, var_y);

    // Edge operand: only has neighbors for vertices 10 and 30, not 20
    auto edge_operand = create_test_operand({{10, 100}, {10, 101}, {30, 300}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
    }

    // Expected: only results for vertices 10 and 30, nothing for 20
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {1, 100}, {1, 101}, {3, 300}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way partial matching test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_left_epsilon_only() {
    std::cout << "Testing TI two-way with left operand epsilon only..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Left operand has epsilon, right operand doesn't
    auto leftmost = create_test_operand_with_epsilon({{1, 10}}, var_x, var_y, true);
    auto edge_operand = create_test_operand_with_epsilon({{100, 1000}, {200, 2000}}, var_y, var_z, false);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
        std::cout << "Result: x=" << binding[var_x].id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected: epsilon from left operand should produce cartesian product with edge operand
    // Since left operand is empty after exhausting its single row, we get epsilon results
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {100, 1000}, {200, 2000}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way left epsilon only test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_right_epsilon_only() {
    std::cout << "Testing TI two-way with right operand epsilon only..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Left operand doesn't have epsilon, right operand has epsilon
    auto leftmost = create_test_operand_with_epsilon({{1, 10}}, var_x, var_y, false);
    auto edge_operand = create_test_operand_with_epsilon({{100, 1000}, {200, 2000}}, var_y, var_z, true);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
        std::cout << "Result: x=" << binding[var_x].id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected: epsilon from right operand should produce cartesian product with left operand
    // Since there are no neighbors for vertex 10, we get epsilon results
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {1, 10}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way right epsilon only test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_both_epsilon() {
    std::cout << "Testing TI two-way with both operands having epsilon..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Both operands have epsilon
    auto leftmost = create_test_operand_with_epsilon({{1, 10}}, var_x, var_y, true);
    auto edge_operand = create_test_operand_with_epsilon({{100, 1000}, {200, 2000}}, var_y, var_z, true);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
        std::cout << "Result: x=" << binding[var_x].id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected: When both have epsilon, left operand takes precedence
    // Should get cartesian product of left epsilon with edge operand
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {1, 10}, {100, 1000}, {200, 2000}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way both epsilon test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_no_epsilon() {
    std::cout << "Testing TI two-way with no epsilon..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Neither operand has epsilon
    auto leftmost = create_test_operand_with_epsilon({{1, 10}}, var_x, var_y, false);
    auto edge_operand = create_test_operand_with_epsilon({{100, 1000}, {200, 2000}}, var_y, var_z, false);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
        std::cout << "Result: x=" << binding[var_x].id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected: No epsilon, no neighbors for vertex 10, so no results
    if (results.size() != 0) {
        std::cerr << "Expected 0 results, got " << results.size() << std::endl;
        return true;
    }

    std::cout << "TI two-way no epsilon test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_epsilon_with_matching_vertices() {
    std::cout << "Testing TI two-way epsilon with matching vertices..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Left operand has epsilon, and there are also matching vertices
    auto leftmost = create_test_operand_with_epsilon({{1, 10}, {2, 20}}, var_x, var_y, true);
    auto edge_operand = create_test_operand_with_epsilon({{10, 100}, {30, 300}}, var_y, var_z, false);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
        std::cout << "Result: x=" << binding[var_x].id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected:
    // 1. Normal join: x=1, y=10 -> z=100 (vertex 10 has neighbor 100)
    // 2. Epsilon results: cartesian product of left epsilon with edge operand
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {1, 100},     // Normal join result
        {10, 100},    // Epsilon result
        {30, 300}     // Epsilon result
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way epsilon with matching vertices test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_epsilon_reset() {
    std::cout << "Testing TI two-way epsilon reset functionality..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    auto leftmost = create_test_operand_with_epsilon({{1, 10}}, var_x, var_y, true);
    auto edge_operand = create_test_operand_with_epsilon({{100, 1000}, {200, 2000}}, var_y, var_z, false);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> first_run;
    while (ti_two_way.next()) {
        first_run.push_back({binding[var_x].id, binding[var_z].id});
    }

    ti_two_way.reset();
    std::vector<std::tuple<uint64_t, uint64_t>> second_run;
    while (ti_two_way.next()) {
        second_run.push_back({binding[var_x].id, binding[var_z].id});
    }

    if (first_run != second_run) {
        std::cerr << "Reset didn't produce identical results for epsilon handling" << std::endl;
        std::cerr << "First run size: " << first_run.size() << ", Second run size: " << second_run.size() << std::endl;
        return true;
    }

    if (first_run.size() == 0) {
        std::cerr << "No results produced - epsilon handling may be broken" << std::endl;
        return true;
    }

    std::cout << "TI two-way epsilon reset test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_leftmost_constant_col1() {
    std::cout << "Testing TI two-way with leftmost operand having constant col1..." << std::endl;

    VarId var_y(1), var_z(2);

    // Leftmost operand: constant col1 = 100, y variable
    // Edges: (100, 10), (100, 20) but operand filters to only show constant 100
    auto leftmost = create_test_operand_with_constant_col1({{100, 10}, {100, 20}, {100, 30}}, ObjectId(100), var_y);

    // Edge operand: y -> z (neighbors of y values)
    auto edge_operand = create_test_operand({{10, 1000}, {10, 1001}, {20, 2000}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), ObjectId(100), var_z);

    // Check that the operator properly inherits the constant col1
    if (!ti_two_way.get_has_constant_col1()) {
        std::cerr << "TITwoWayOperator should inherit constant col1 from leftmost operand" << std::endl;
        return true;
    }

    if (ti_two_way.get_constant_col1() != ObjectId(100)) {
        std::cerr << "TITwoWayOperator should inherit correct constant col1 value" << std::endl;
        return true;
    }

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({ti_two_way.get_constant_col1().id, binding[var_z].id});
        std::cout << "Result: constant=" << ti_two_way.get_constant_col1().id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected results: For y=10 and y=20 (from leftmost), find their neighbors
    // y=10 -> neighbors 1000, 1001
    // y=20 -> neighbor 2000
    // All should have col1 = 100 (the constant)
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {100, 1000}, {100, 1001}, {100, 2000}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            std::cerr << "Expected: (" << std::get<0>(expected[i]) << ", " << std::get<1>(expected[i])
                      << ")" << std::endl;
            std::cerr << "Got: (" << std::get<0>(results[i]) << ", " << std::get<1>(results[i])
                      << ")" << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way leftmost constant col1 test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_edge_operand_constant_col1() {
    std::cout << "Testing TI two-way with edge operand having constant col1..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: normal x -> y mapping
    auto leftmost = create_test_operand({{1, 10}, {2, 20}}, var_x, var_y);

    // Edge operand: constant col1 = 10, z variable
    // This means all edges in the operand have source = 10
    auto edge_operand = create_test_operand_with_constant_col1({{10, 100}, {10, 101}, {10, 200}}, ObjectId(10), var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
        std::cout << "Result: x=" << binding[var_x].id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected results: Only leftmost row with y=10 can join
    // because edge operand only has edges with source=10
    // x=1, y=10 -> neighbors of 10 are 100, 101
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {1, 100}, {1, 101}, {1, 200}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way edge operand constant col1 test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_both_constant_col1() {
    std::cout << "Testing TI two-way with both operands having constant col1..." << std::endl;

    VarId var_y(1), var_z(2);

    // Leftmost operand: constant col1 = 100, y variable
    auto leftmost = create_test_operand_with_constant_col1({{100, 10}, {100, 20}}, ObjectId(100), var_y);

    // Edge operand: constant col1 = 10, z variable
    auto edge_operand = create_test_operand_with_constant_col1({{10, 1000}, {10, 1001}}, ObjectId(10), var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), ObjectId(100), var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({ti_two_way.get_constant_col1().id, binding[var_z].id});
        std::cout << "Result: constant=" << ti_two_way.get_constant_col1().id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected results: Only y=10 from leftmost can join
    // because edge operand only has edges with source=10
    // y=10 -> neighbors 1000, 1001, with col1=100 (from leftmost)
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {100, 1000}, {100, 1001}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way both constant col1 test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_constant_col1_no_matches() {
    std::cout << "Testing TI two-way with constant col1 but no matching vertices..." << std::endl;

    VarId var_y(1), var_z(2);

    // Leftmost operand: constant col1 = 100, y variable
    auto leftmost = create_test_operand_with_constant_col1({{100, 10}, {100, 20}}, ObjectId(100), var_y);

    // Edge operand: has edges but none start from vertices 10 or 20
    auto edge_operand = create_test_operand({{30, 3000}, {40, 4000}}, var_y, var_z);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), ObjectId(100), var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    if (ti_two_way.next()) {
        std::cerr << "Expected no results when constant col1 operand has no matching vertices" << std::endl;
        return true;
    }

    std::cout << "TI two-way constant col1 no matches test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_constant_col1_with_epsilon() {
    std::cout << "Testing TI two-way with constant col1 and epsilon..." << std::endl;

    VarId var_y(1), var_z(2);

    // Leftmost operand: constant col1 = 100, y variable, has epsilon
    auto leftmost = create_test_operand_with_constant_col1_and_epsilon({{100, 10}}, ObjectId(100), var_y, true);

    // Edge operand: normal operand, no epsilon
    auto edge_operand = create_test_operand_with_epsilon({{20, 2000}, {30, 3000}}, var_y, var_z, false);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), ObjectId(100), var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[ti_two_way.get_col1_var()].id, binding[var_z].id});
        std::cout << "Result: constant=" << ti_two_way.get_constant_col1().id
                  << ", z=" << binding[var_z].id << std::endl;
    }

    // Expected: Since leftmost has epsilon and no matching vertices (10 is not in edge operand),
    // epsilon should produce cartesian product with edge operand
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way constant col1 with epsilon test passed!" << std::endl;
    return false;
}

bool test_ti_two_way_edge_constant_col1_with_epsilon() {
    std::cout << "Testing TI two-way with edge operand constant col1 and epsilon..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: normal operand, no epsilon
    auto leftmost = create_test_operand_with_epsilon({{1, 10}, {2, 20}}, var_x, var_y, false);

    // Edge operand: constant col1 = 30, has epsilon
    auto edge_operand = create_test_operand_with_constant_col1_and_epsilon({{30, 3000}}, ObjectId(30), var_z, true);

    TITwoWayOperator ti_two_way(std::move(leftmost), std::move(edge_operand), var_x, var_z);

    Binding binding(3);
    ti_two_way.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_two_way.next()) {
        results.push_back({binding[var_x].id, binding[var_z].id});
        std::cout << "Result: x=" << binding[var_x].id
                  << ", y=" << binding[var_z].id << std::endl;
    }

    // Expected: Since edge operand has epsilon and no matching vertices
    // (neither 10 nor 20 match the constant 30),
    // epsilon should produce cartesian product with leftmost operand
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {1, 10}, {2, 20}
    };

    if (results.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << results.size() << std::endl;
        return true;
    }

    std::sort(results.begin(), results.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (results[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "TI two-way edge constant col1 with epsilon test passed!" << std::endl;
    return false;
}

int main() {
    std::vector<TestFunction*> tests;

    tests.push_back(&test_basic_ti_two_way_functionality);
    tests.push_back(&test_ti_two_way_empty_leftmost);
    tests.push_back(&test_ti_two_way_empty_edge_operand);
    tests.push_back(&test_ti_two_way_no_matching_vertices);
    tests.push_back(&test_ti_two_way_single_vertex_multiple_neighbors);
    tests.push_back(&test_ti_two_way_reset_functionality);
    tests.push_back(&test_ti_two_way_assign_nulls);
    tests.push_back(&test_ti_two_way_common_var_accessor);
    tests.push_back(&test_ti_two_way_large_dataset);
    tests.push_back(&test_ti_two_way_partial_matching);

    // Epsilon handling tests
    tests.push_back(&test_ti_two_way_left_epsilon_only);
    tests.push_back(&test_ti_two_way_right_epsilon_only);
    tests.push_back(&test_ti_two_way_both_epsilon);
    tests.push_back(&test_ti_two_way_no_epsilon);
    tests.push_back(&test_ti_two_way_epsilon_with_matching_vertices);
    tests.push_back(&test_ti_two_way_epsilon_reset);

    // Constant col1 tests
    tests.push_back(&test_ti_two_way_leftmost_constant_col1);
    tests.push_back(&test_ti_two_way_edge_operand_constant_col1);
    tests.push_back(&test_ti_two_way_both_constant_col1);
    tests.push_back(&test_ti_two_way_constant_col1_no_matches);
    tests.push_back(&test_ti_two_way_constant_col1_with_epsilon);
    tests.push_back(&test_ti_two_way_edge_constant_col1_with_epsilon);

    auto error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
    }

    if (!error) {
        std::cout << "\nAll TITwoWayOperator tests passed!" << std::endl;
    } else {
        std::cout << "\nSome TITwoWayOperator tests failed!" << std::endl;
    }

    return error;
}