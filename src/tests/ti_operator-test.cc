#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_set>

#include "query/executor/binding_iter/custom_ops/ti_operator.h"
#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include "query/executor/binding_iter_visitor.h"
#include "query/executor/binding.h"
#include "query/var_id.h"

using namespace CustomOps;

typedef bool TestFunction();

// Helper function to compare actual and expected results with sorting
template<typename T>
bool compare_results(const std::vector<T>& actual, const std::vector<T>& expected, const std::string& test_name) {
    // Create sorted copies for comparison
    std::vector<T> sorted_actual = actual;
    std::vector<T> sorted_expected = expected;

    std::sort(sorted_actual.begin(), sorted_actual.end());
    std::sort(sorted_expected.begin(), sorted_expected.end());

    if (sorted_actual.size() != sorted_expected.size()) {
        std::cerr << test_name << ": Expected " << sorted_expected.size()
                  << " results, got " << sorted_actual.size() << std::endl;
        return true;  // true indicates failure
    }

    for (size_t i = 0; i < sorted_expected.size(); ++i) {
        if (sorted_actual[i] != sorted_expected[i]) {
            std::cerr << test_name << ": Mismatch at position " << i << " after sorting" << std::endl;
            return true;  // true indicates failure
        }
    }

    return false;  // false indicates success
}
class MockBindingIter : public BindingIter {
private:
    std::vector<std::vector<uint64_t>> results;
    size_t current_result;
    Binding* parent_binding;
    std::vector<VarId> vars;

public:
    MockBindingIter(const std::vector<std::vector<uint64_t>>& data, const std::vector<VarId>& variables)
        : results(data), current_result(0), parent_binding(nullptr), vars(variables) {}

    void _begin(Binding& binding) override {
        parent_binding = &binding;
        current_result = 0;
    }

    bool _next() override {
        if (current_result >= results.size()) {
            return false;
        }

        // Set values in binding
        for (size_t i = 0; i < vars.size() && i < results[current_result].size(); ++i) {
            auto obj_id = ObjectId(results[current_result][i]);
            parent_binding->add(vars[i], obj_id);
        }

        current_result++;
        return true;
    }

    void _reset() override {
        current_result = 0;
    }

    void assign_nulls() override {
        if (parent_binding) {
            for (const auto& var : vars) {
                (*parent_binding)[var] = ObjectId::get_null();
            }
        }
    }

    void accept_visitor(BindingIterVisitor& visitor) override {
        // Mock implementation
    }
};

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
std::unique_ptr<TwoColumnStoreBindingIter> create_test_edge_operand(
    const std::vector<std::pair<long long, long long>>& edges,
    VarId col1_var, VarId col2_var) {

    auto store = create_test_store(edges);
    return std::make_unique<TwoColumnStoreBindingIter>(std::move(store), col1_var, col2_var);
}

bool test_basic_ti_intersection() {
    std::cout << "Testing basic TI intersection functionality..." << std::endl;

    // Variables: x (0), y (1), z (2)
    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: [(x=1, y=10), (x=2, y=20)]
    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}, {2, 20}}, leftmost_vars);

    // Edge operands:
    // Edge 1: x -> z
    // Edge 2: y -> z
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {{1, 100}, {1, 101}, {2, 200}, {3, 300}}, var_x, var_z));
    edge_operands.push_back(create_test_edge_operand(
        {{10, 100}, {10, 102}, {20, 200}, {20, 400}}, var_y, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> results;
    while (ti_op.next()) {
        results.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
        std::cout << binding[var_x].id << " " << binding[var_y].id << " " << binding[var_z].id << std::endl;
    }

    // Expected results: intersection of neighbors for each leftmost row
    // For x=1, y=10: neighbors of 1 in both edges intersect at {100}
    // For x=2, y=20: neighbors of 2 in both edges intersect at {200}
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100},
        {2, 20, 200}
    };

    if (compare_results(results, expected, "test_basic_ti_intersection")) {
        return true;
    }

    std::cout << "Basic TI intersection test passed!" << std::endl;
    return false;
}

bool test_ti_no_intersection() {
    std::cout << "Testing TI with no intersection..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: [(x=1, y=10)]
    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}}, leftmost_vars);

    // Edge operands with no common neighbors (both map to var_z):
    // Edge 1: x -> z, edges: (1,100), (1,101)
    // Edge 2: y -> z, edges: (10,200), (10,201)
    // No intersection between {100,101} and {200,201}
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {{1, 100}, {1, 101}}, var_x, var_z));
    edge_operands.push_back(create_test_edge_operand(
        {{10, 200}, {10, 201}}, var_y, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    if (ti_op.next()) {
        std::cerr << "Expected no results when no intersection exists" << std::endl;
        return true;
    }

    std::cout << "TI no intersection test passed!" << std::endl;
    return false;
}

bool test_ti_empty_leftmost() {
    std::cout << "Testing TI with empty leftmost operand..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Empty leftmost operand
    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{}, leftmost_vars);

    // Edge operand
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {{1, 100}, {2, 200}}, var_x, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    if (ti_op.next()) {
        std::cerr << "Expected no results when leftmost operand is empty" << std::endl;
        return true;
    }

    std::cout << "TI empty leftmost test passed!" << std::endl;
    return false;
}

bool test_ti_single_edge_operand() {
    std::cout << "Testing TI with single edge operand..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: [(x=1, y=10), (x=2, y=20)]
    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}, {2, 20}}, leftmost_vars);

    // Single edge operand: x -> z
    // With single edge, "intersection" is just all neighbors
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {{1, 101}, {1, 100}, {2, 200}}, var_x, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> results;
    while (ti_op.next()) {
        results.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
        std::cout << binding[var_x].id << " " << binding[var_y].id << " " << binding[var_z].id << std::endl;
    }

    // With single edge operand, all neighbors should be returned
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 101}, {1, 10, 100}, {2, 20, 200}
    };

    if (compare_results(results, expected, "test_ti_single_edge_operand")) {
        return true;
    }

    std::cout << "TI single edge operand test passed!" << std::endl;
    return false;
}

bool test_ti_multiple_intersections() {
    std::cout << "Testing TI with multiple intersections..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: [(x=1, y=10)]
    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}}, leftmost_vars);

    // Edge operands with multiple common neighbors (both map to var_z):
    // Edge 1: x -> z, edges: (1,100), (1,200), (1,300)
    // Edge 2: y -> z, edges: (10,100), (10,200), (10,400)
    // Intersection: {100, 200}
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {{1, 100}, {1, 200}, {1, 300}}, var_x, var_z));
    edge_operands.push_back(create_test_edge_operand(
        {{10, 100}, {10, 200}, {10, 400}}, var_y, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> results;
    while (ti_op.next()) {
        results.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    // Expected intersections: {100, 200}
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100},
        {1, 10, 200}
    };

    if (compare_results(results, expected, "test_ti_multiple_intersections")) {
        return true;
    }

    std::cout << "TI multiple intersections test passed!" << std::endl;
    return false;
}

bool test_ti_reset_functionality() {
    std::cout << "Testing TI reset functionality..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}, {2, 20}}, leftmost_vars);

    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {{1, 100}, {2, 200}}, var_x, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> first_run;
    std::cout << "first run:" << std::endl;
    while (ti_op.next()) {
        first_run.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
        std::cout << binding[var_x].id << " " << binding[var_y].id << " " << binding[var_z].id << std::endl;
    }
    
    ti_op.reset();
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> second_run;
    std::cout << "second run:" << std::endl;
    while (ti_op.next()) {
        second_run.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
        std::cout << binding[var_x].id << " " << binding[var_y].id << " " << binding[var_z].id << std::endl;
    }

    if (first_run != second_run) {
        std::cerr << "Reset didn't produce identical results" << std::endl;
        return true;
    }

    std::cout << "TI reset functionality test passed!" << std::endl;
    return false;
}

bool test_ti_operator_type() {
    std::cout << "Testing TI operator type..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{}, leftmost_vars);

    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {}, var_x, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    if (ti_op.get_operator_type() != CustomOperatorBase::OperatorType::TI) {
        std::cerr << "Wrong operator type returned" << std::endl;
        return true;
    }

    std::cout << "TI operator type test passed!" << std::endl;
    return false;
}

bool test_ti_assign_nulls() {
    std::cout << "Testing TI assign_nulls functionality..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}}, leftmost_vars);

    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {{1, 100}}, var_x, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.assign_nulls();

    // Check that leftmost variables are set to null
    if (binding[var_x] != ObjectId::get_null() ||
        binding[var_y] != ObjectId::get_null()) {
        std::cerr << "assign_nulls didn't set leftmost variables to null" << std::endl;
        return true;
    }

    std::cout << "TI assign_nulls test passed!" << std::endl;
    return false;
}

bool test_ti_prebound_intersection_variable_valid() {
    std::cout << "Testing TI with pre-bound intersection variable (valid case)..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: [(x=1, y=10, z=100)]  - z is pre-bound
    std::vector<VarId> leftmost_vars = {var_x, var_y, var_z};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10, 100}}, leftmost_vars);

    // Edge operands (both map to var_z):
    // Edge 1: x -> z, edges: (1,100), (1,101)  - 100 intersects with pre-bound z
    // Edge 2: y -> z, edges: (10,100), (10,102)  - 100 intersects
    // Intersection: {100}, which matches pre-bound z
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {{1, 100}, {1, 101}}, var_x, var_z));
    edge_operands.push_back(create_test_edge_operand(
        {{10, 100}, {10, 102}}, var_y, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> results;
    while (ti_op.next()) {
        results.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    // Expected: Only intersection value 100 should be valid since z is pre-bound to 100
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100}
    };

    if (compare_results(results, expected, "test_ti_prebound_intersection_variable_valid")) {
        return true;
    }

    std::cout << "TI pre-bound intersection variable (valid) test passed!" << std::endl;
    return false;
}

bool test_ti_prebound_intersection_variable_invalid() {
    std::cout << "Testing TI with pre-bound intersection variable (invalid case)..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: [(x=1, y=10, z=999)]  - z is pre-bound to 999
    std::vector<VarId> leftmost_vars = {var_x, var_y, var_z};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10, 999}}, leftmost_vars);

    // Edge operands (both map to var_z):
    // Edge 1: x -> z, edges: (1,100), (1,101)  - No 999 in intersection
    // Edge 2: y -> z, edges: (10,100), (10,102)  - Intersection is 100, but z is bound to 999
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand(
        {{1, 100}, {1, 101}}, var_x, var_z));
    edge_operands.push_back(create_test_edge_operand(
        {{10, 100}, {10, 102}}, var_y, var_z));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands), true);

    Binding binding(3);
    ti_op.begin(binding);

    // Should get no results since intersection (100) doesn't match pre-bound z (999)
    if (ti_op.next()) {
        std::cerr << "Expected no results when pre-bound variable conflicts with intersection" << std::endl;
        return true;
    }

    std::cout << "TI pre-bound intersection variable (invalid) test passed!" << std::endl;
    return false;
}

// Mock binding iterator with epsilon support for testing
class MockEpsilonBindingIter : public BindingIter {
private:
    std::vector<std::vector<uint64_t>> results;
    size_t current_result;
    Binding* parent_binding;
    std::vector<VarId> vars;
    bool epsilon = false;

public:
    MockEpsilonBindingIter(const std::vector<std::vector<uint64_t>>& data,
                          const std::vector<VarId>& variables, bool has_epsilon = true)
        : results(data), current_result(0), parent_binding(nullptr), vars(variables) {
        epsilon = has_epsilon;
    }

    void _begin(Binding& binding) override {
        parent_binding = &binding;
        current_result = 0;
    }

    bool _next() override {
        if (current_result >= results.size()) {
            return false;
        }

        // Set values in binding
        for (size_t i = 0; i < vars.size() && i < results[current_result].size(); ++i) {
            auto obj_id = ObjectId(results[current_result][i]);
            parent_binding->add(vars[i], obj_id);
        }

        current_result++;
        return true;
    }

    void _reset() override {
        current_result = 0;
    }

    void assign_nulls() override {
        if (parent_binding) {
            for (const auto& var : vars) {
                (*parent_binding)[var] = ObjectId::get_null();
            }
        }
    }

    void accept_visitor(BindingIterVisitor& visitor) override {
        // Mock implementation
    }

    void set_epsilon(bool epsilon_) { epsilon = epsilon_; }
};

// Helper function to create a TwoColumnStoreBindingIter with epsilon support
std::unique_ptr<TwoColumnStoreBindingIter> create_test_edge_operand_epsilon(
    const std::vector<std::pair<long long, long long>>& edges,
    VarId col1_var, VarId col2_var, bool has_epsilon = true) {

    auto store = create_test_store(edges);
    auto iter = std::make_unique<TwoColumnStoreBindingIter>(std::move(store), col1_var, col2_var);
    iter->set_epsilon(has_epsilon);
    return iter;
}

bool test_ti_epsilon_functionality() {
    std::cout << "Testing TI epsilon functionality..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand with epsilon: [(x=1, y=10)]
    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockEpsilonBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}}, leftmost_vars, true);

    // Edge operands with epsilon enabled:
    // When epsilon is true, each vertex should include itself as a neighbor
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand_epsilon(
        {{1, 100}, {1, 101}}, var_x, var_z, true));
    edge_operands.push_back(create_test_edge_operand_epsilon(
        {{10, 100}, {10, 102}, {1, 101}}, var_y, var_z, true));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> results;
    while (ti_op.next()) {
        results.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
        std::cout << binding[var_x].id << " " << binding[var_y].id << " " << binding[var_z].id << std::endl;
    }

    // With epsilon, we expect:
    // - Regular intersection: {100}
    // - Self-loops: vertex 1 should appear in neighbors of x->z, vertex 10 in neighbors of y->z
    // So intersection should include 1, 10, and 100
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100},  // Regular intersection
        // Note: The actual epsilon behavior may include more results depending on implementation
        {1, 1, 101}, {1, 1, 1}
    };

    if (compare_results(results, expected, "test_ti_epsilon_functionality")) {
        return true;
    }

    std::cout << "TI epsilon functionality test passed!" << std::endl;
    return false;
}

bool test_ti_mixed_epsilon_operands() {
    std::cout << "Testing TI with mixed epsilon operands..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand with epsilon
    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockEpsilonBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}}, leftmost_vars, true);

    // Mixed edge operands: one with epsilon, one without
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand_epsilon(
        {{1, 100}, {1, 101}}, var_x, var_z, true));  // With epsilon
    edge_operands.push_back(create_test_edge_operand_epsilon(
        {{10, 100}, {10, 102}, {10, 1}}, var_y, var_z, false)); // Without epsilon

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    // TI operator should have epsilon = false because not all operands have epsilon
    if (ti_op.get_epsilon()) {
        std::cerr << "TI operator should not have epsilon when operands have mixed epsilon support" << std::endl;
        return true;
    }

    Binding binding(3);
    ti_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> results;
    while (ti_op.next()) {
        results.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    // Should behave like regular intersection since epsilon is disabled
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100},
        {1, 10, 1}
    };

    if (compare_results(results, expected, "test_ti_mixed_epsilon_operands")) {
        return true;
    }

    std::cout << "TI mixed epsilon operands test passed!" << std::endl;
    return false;
}

bool test_ti_left_epsilon_functionality() {
    std::cout << "Testing TI left epsilon functionality..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand with epsilon but no regular results (empty)
    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockEpsilonBindingIter>(
        std::vector<std::vector<uint64_t>>{}, leftmost_vars, true);

    // Edge operands with epsilon - this should trigger left epsilon logic
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand_epsilon(
        {{1, 100}, {2, 200}, {3, 300}}, var_x, var_z, true));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> results;
    int max_results = 10; // Prevent infinite loop in case of bugs
    while (ti_op.next() && results.size() < max_results) {
        results.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
        std::cout << "Left epsilon result: " << binding[var_x].id
                  << " " << binding[var_y].id << " " << binding[var_z].id << std::endl;
    }

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 1, 100}, {2, 2, 200}, {3, 3, 300}, {1, 1, 1}, {2, 2, 2}, {3, 3, 3}
    };

    if (compare_results(results, expected, "test_ti_left_epsilon_functionality")) {
        return true;
    }

    std::cout << "TI left epsilon functionality test passed!" << std::endl;
    return false;
}

bool test_ti_left_epsilon_right_no_epsilon() {
    std::cout << "Testing TI left epsilon right no epsilon functionality..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2);

    // Leftmost operand: two-column binding iter with constant col1=1, epsilon enabled
    // This mimics Q937857 (P279)* ?x2 pattern - constant first column with epsilon path
    auto store_leftmost = create_test_store({{1, 10}}); // Second column data for epsilon
    auto leftmost = std::make_unique<TwoColumnStoreBindingIter>(
        std::move(store_leftmost), ObjectId(1), var_y); // Constant col1=1, variable col2=var_y
    leftmost->set_epsilon(true);

    // Edge operands without epsilon - mimics P641 and P425 relations
    // Both edge operands should connect to the same result variable
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand_epsilon(
        {{1, 2}, {1, 3}}, var_y, var_z, false));  // ?x2 P641 ?y -> (1,2), (1,3)
    edge_operands.push_back(create_test_edge_operand_epsilon(
        {{1, 2}, {1, 4}}, var_y, var_z, false));  // ?x2 P425 ?y -> (1,2), (1,4)

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    Binding binding(3);
    ti_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t>> results;
    while (ti_op.next()) {
        results.push_back({binding[var_y].id, binding[var_z].id});
        std::cout << "Result: " << binding[var_y].id
                  << " " << binding[var_z].id << std::endl;
    }

    // Expected result: (1,1,2) - intersection of P641 and P425 from vertex 1 is {2}
    // var_x=1 (constant), var_y=1 (from epsilon), var_z=2 (intersection result)
    std::vector<std::tuple<uint64_t, uint64_t>> expected = {
        {1, 2}
    };

    if (compare_results(results, expected, "test_ti_left_epsilon_right_no_epsilon")) {
        return true;
    }

    std::cout << "TI left epsilon right no epsilon functionality test passed!" << std::endl;
    return false;
}

bool test_ti_get_num_edge_path_operands() {
    std::cout << "Testing TI get_num_edge_path_operands..." << std::endl;

    VarId var_x(0), var_y(1), var_z(2), var_w(3);

    std::vector<VarId> leftmost_vars = {var_x, var_y};
    auto leftmost = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{}, leftmost_vars);

    // Create multiple edge operands
    std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
    edge_operands.push_back(create_test_edge_operand({{1, 100}}, var_x, var_z));
    edge_operands.push_back(create_test_edge_operand({{10, 200}}, var_y, var_z));
    edge_operands.push_back(create_test_edge_operand({{1, 300}}, var_x, var_w));

    TIOperator ti_op(std::move(leftmost), std::move(edge_operands));

    if (ti_op.get_num_edge_path_operands() != 3) {
        std::cerr << "Expected 3 edge operands, got " << ti_op.get_num_edge_path_operands() << std::endl;
        return true;
    }

    std::cout << "TI get_num_edge_path_operands test passed!" << std::endl;
    return false;
}

int main() {
    std::vector<TestFunction*> tests;

    // Original tests
    tests.push_back(&test_basic_ti_intersection);
    tests.push_back(&test_ti_no_intersection);
    tests.push_back(&test_ti_empty_leftmost);
    tests.push_back(&test_ti_single_edge_operand);
    tests.push_back(&test_ti_multiple_intersections);
    tests.push_back(&test_ti_reset_functionality);
    tests.push_back(&test_ti_operator_type);
    tests.push_back(&test_ti_assign_nulls);
    tests.push_back(&test_ti_prebound_intersection_variable_valid);
    tests.push_back(&test_ti_prebound_intersection_variable_invalid);

    // New tests for epsilon functionality and enhanced features
    tests.push_back(&test_ti_epsilon_functionality);
    tests.push_back(&test_ti_mixed_epsilon_operands);
    tests.push_back(&test_ti_left_epsilon_functionality);
    tests.push_back(&test_ti_left_epsilon_right_no_epsilon);
    tests.push_back(&test_ti_get_num_edge_path_operands);

    auto error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
    }

    if (!error) {
        std::cout << "\nAll TIOperator tests passed!" << std::endl;
    } else {
        std::cout << "\nSome TIOperator tests failed!" << std::endl;
    }

    return error;
}