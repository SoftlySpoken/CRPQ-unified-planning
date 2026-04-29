#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_set>
#include <set>

#include "query/executor/binding_iter/custom_ops/sj_operator.h"
#include "query/executor/binding_iter_visitor.h"
#include "query/executor/binding.h"
#include "query/var_id.h"

using namespace CustomOps;

typedef bool TestFunction();

// Helper function for order-insensitive comparison of tuples
template<typename TupleType>
bool compare_results_order_insensitive(const std::vector<TupleType>& actual,
                                      const std::vector<TupleType>& expected) {
    if (actual.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << actual.size() << std::endl;
        return false;
    }

    std::set<TupleType> actual_set(actual.begin(), actual.end());
    std::set<TupleType> expected_set(expected.begin(), expected.end());

    if (actual_set != expected_set) {
        std::cerr << "Result sets don't match:" << std::endl;
        std::cerr << "Expected:" << std::endl;
        for (const auto& tuple : expected_set) {
            std::cerr << "  ";
            std::apply([](const auto&... args) { ((std::cerr << args << " "), ...); }, tuple);
            std::cerr << std::endl;
        }
        std::cerr << "Actual:" << std::endl;
        for (const auto& tuple : actual_set) {
            std::cerr << "  ";
            std::apply([](const auto&... args) { ((std::cerr << args << " "), ...); }, tuple);
            std::cerr << std::endl;
        }
        return false;
    }

    return true;
}

class MockBindingIter : public BindingIter {
private:
    std::vector<std::vector<uint64_t>> results;
    size_t current_result;
    Binding* parent_binding;
    std::vector<VarId> vars;
    bool epsilon = false;

public:
    MockBindingIter(const std::vector<std::vector<uint64_t>>& data, const std::vector<VarId>& variables, bool has_epsilon = false)
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

bool test_basic_sj_join() {
    std::cout << "Testing basic SJ join functionality..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}, {2, 20}, {3, 30}}, left_vars);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 10}, {200, 20}, {300, 40}}, right_vars);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(3);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100}, {2, 20, 200}
    };
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> actual;

    while (sj_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    if (actual.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << actual.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        std::cout << std::get<0>(actual[i]) << " " << std::get<1>(actual[i]) << " " << std::get<2>(actual[i]) << std::endl;
        std::cout << std::get<0>(expected[i]) << " " << std::get<1>(expected[i]) << " " << std::get<2>(expected[i]) << std::endl;
        if (actual[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "Basic SJ join test passed!" << std::endl;
    return false;
}

bool test_sj_no_matches() {
    std::cout << "Testing SJ join with no matches..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}, {2, 20}}, left_vars);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 30}, {200, 40}}, right_vars);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    if (sj_op.next()) {
        std::cerr << "Expected no results when no join matches exist" << std::endl;
        return true;
    }

    std::cout << "SJ join no matches test passed!" << std::endl;
    return false;
}

bool test_sj_empty_left_child() {
    std::cout << "Testing SJ join with empty left child..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{}, left_vars);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 10}, {200, 20}}, right_vars);


    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    if (sj_op.next()) {
        std::cerr << "Expected no results when left child is empty" << std::endl;
        return true;
    }

    std::cout << "SJ join empty left child test passed!" << std::endl;
    return false;
}

bool test_sj_empty_right_child() {
    std::cout << "Testing SJ join with empty right child..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}, {2, 20}}, left_vars);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{}, right_vars);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    if (sj_op.next()) {
        std::cerr << "Expected no results when right child is empty" << std::endl;
        return true;
    }

    std::cout << "SJ join empty right child test passed!" << std::endl;
    return false;
}

bool test_sj_multiple_matches() {
    std::cout << "Testing SJ join with multiple matches..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}, {2, 20}}, left_vars);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 10}, {101, 10}, {200, 20}}, right_vars);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100}, {1, 10, 101}, {2, 20, 200}
    };
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> actual;

    while (sj_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    if (actual.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << actual.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        std::cout << std::get<0>(actual[i]) << " " << std::get<1>(actual[i]) << " " << std::get<2>(actual[i]) << std::endl;
        std::cout << std::get<0>(expected[i]) << " " << std::get<1>(expected[i]) << " " << std::get<2>(expected[i]) << std::endl;
        if (actual[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "SJ join multiple matches test passed!" << std::endl;
    return false;
}

bool test_sj_multi_column_join() {
    std::cout << "Testing SJ join with multiple join columns..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);
    VarId var_w(3);

    std::vector<VarId> left_vars = {var_x, var_y, var_z};
    std::vector<VarId> right_vars = {var_w, var_y, var_z};

    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10, 100}, {2, 20, 200}, {3, 10, 200}}, left_vars);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1000, 10, 100}, {2000, 20, 300}, {3000, 10, 200}}, right_vars);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100, 1000}, {3, 10, 200, 3000}
    };
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> actual;

    while (sj_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id, binding[var_w].id});
    }

    if (actual.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " results, got " << actual.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "SJ join multi-column test passed!" << std::endl;
    return false;
}

bool test_sj_reset_functionality() {
    std::cout << "Testing SJ reset functionality..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}, {2, 20}}, left_vars);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 10}, {200, 20}}, right_vars);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> first_run;
    while (sj_op.next()) {
        first_run.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    sj_op.reset();
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> second_run;
    while (sj_op.next()) {
        second_run.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    if (first_run != second_run) {
        std::cerr << "Reset didn't produce identical results" << std::endl;
        return true;
    }

    std::cout << "SJ reset functionality test passed!" << std::endl;
    return false;
}

bool test_sj_assign_nulls() {
    std::cout << "Testing SJ assign_nulls functionality..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}}, left_vars);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 10}}, right_vars);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.assign_nulls();

    if (binding[var_x] != ObjectId::get_null() ||
        binding[var_y] != ObjectId::get_null() ||
        binding[var_z] != ObjectId::get_null()) {
        std::cerr << "assign_nulls didn't set variables to null" << std::endl;
        return true;
    }

    std::cout << "SJ assign_nulls test passed!" << std::endl;
    return false;
}

bool test_sj_operator_type() {
    std::cout << "Testing SJ operator type..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);

    std::vector<VarId> left_vars = {var_x};
    std::vector<VarId> right_vars = {var_y};

    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{}, left_vars);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{}, right_vars);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    if (sj_op.get_operator_type() != CustomOperatorBase::OperatorType::SJ) {
        std::cerr << "Wrong operator type returned" << std::endl;
        return true;
    }

    std::cout << "SJ operator type test passed!" << std::endl;
    return false;
}

bool test_sj_left_epsilon() {
    std::cout << "Testing SJ with left epsilon..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    // Left child has epsilon=true with single join variable value
    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}}, left_vars, true);

    // Right child with matching join values
    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 10}, {200, 10}}, right_vars);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> actual;
    while (sj_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100}, {1, 10, 200}, {10, 10, 100}, {10, 10, 200}
    };

    if (!compare_results_order_insensitive(actual, expected)) {
        return true;
    }

    std::cout << "SJ left epsilon test passed!" << std::endl;
    return false;
}

bool test_sj_right_epsilon() {
    std::cout << "Testing SJ with right epsilon..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    // Left child with consistent join values
    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}, {2, 10}}, left_vars);

    // Right child has epsilon=true
    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 10}}, right_vars, true);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> actual;
    while (sj_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    // Expected: normal joins + right epsilon synthetic rows
    // Normal joins: (1,10,100), (2,10,100)
    // Right epsilon synthetic rows when join vars are equal: (1,10,10), (2,10,10)
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100}, {2, 10, 100}, {1, 10, 10}, {2, 10, 10}
    };

    if (!compare_results_order_insensitive(actual, expected)) {
        return true;
    }

    std::cout << "SJ right epsilon test passed!" << std::endl;
    return false;
}

bool test_sj_both_epsilon() {
    std::cout << "Testing SJ with both left and right epsilon..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    // Both children have epsilon=true
    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10}}, left_vars, true);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 10}}, right_vars, true);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> actual;
    while (sj_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    // Expected: normal joins + left epsilon + right epsilon synthetic rows
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
        {1, 10, 100}, {1, 10, 10}, {10, 10, 100}
    };

    if (!compare_results_order_insensitive(actual, expected)) {
        return true;
    }

    std::cout << "SJ both epsilon test passed!" << std::endl;
    return false;
}

bool test_sj_epsilon_no_equal_join_vars() {
    std::cout << "Testing SJ epsilon with non-equal join variables..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);
    VarId var_p(3);

    std::vector<VarId> left_vars = {var_x, var_y, var_p};
    std::vector<VarId> right_vars = {var_z, var_y, var_p};

    // Left child with mixed join values (not all equal)
    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10, 20}, {2, 20, 30}}, left_vars, true);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 20, 30}}, right_vars, true);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> actual;
    while (sj_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id, binding[var_p].id});
    }

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> expected = {
        {2, 20, 100, 30}
    };

    if (!compare_results_order_insensitive(actual, expected)) {
        return true;
    }

    std::cout << "SJ epsilon non-equal join vars test passed!" << std::endl;
    return false;
}

bool test_sj_epsilon_equal_join_vars() {
    std::cout << "Testing SJ epsilon with equal join variables..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);
    VarId var_p(3);

    std::vector<VarId> left_vars = {var_x, var_y, var_p};
    std::vector<VarId> right_vars = {var_z, var_y, var_p};

    // Left child with mixed join values (not all equal)
    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{1, 10, 10}, {2, 20, 30}}, left_vars, true);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{{100, 20, 20}, {101, 20, 30}}, right_vars, true);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> actual;
    while (sj_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id, binding[var_p].id});
    }

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> expected = {
        {2, 20, 101, 30}, {1, 10, 10, 10}, {20, 20, 100, 20}
    };

    if (!compare_results_order_insensitive(actual, expected)) {
        return true;
    }

    std::cout << "SJ epsilon equal join vars test passed!" << std::endl;
    return false;
}

bool test_sj_epsilon_empty_children() {
    std::cout << "Testing SJ epsilon with empty children..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    VarId var_z(2);

    std::vector<VarId> left_vars = {var_x, var_y};
    std::vector<VarId> right_vars = {var_z, var_y};

    // Both children empty but with epsilon
    auto left_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{}, left_vars, true);

    auto right_child = std::make_unique<MockBindingIter>(
        std::vector<std::vector<uint64_t>>{}, right_vars, true);

    SJOperator sj_op(std::move(left_child), std::move(right_child),
                     left_vars, right_vars);

    Binding binding(10);
    sj_op.begin(binding);

    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> results;
    while (sj_op.next()) {
        results.push_back({binding[var_x].id, binding[var_y].id, binding[var_z].id});
    }

    // Expected: no results because no data to generate synthetic rows from
    if (results.size() != 0) {
        std::cerr << "Expected 0 results with empty epsilon children, got " << results.size() << std::endl;
        return true;
    }

    std::cout << "SJ epsilon empty children test passed!" << std::endl;
    return false;
}

int main() {
    std::vector<TestFunction*> tests;

    tests.push_back(&test_basic_sj_join);
    tests.push_back(&test_sj_no_matches);
    tests.push_back(&test_sj_empty_left_child);
    tests.push_back(&test_sj_empty_right_child);
    tests.push_back(&test_sj_multiple_matches);
    tests.push_back(&test_sj_multi_column_join);
    tests.push_back(&test_sj_reset_functionality);
    tests.push_back(&test_sj_assign_nulls);
    tests.push_back(&test_sj_operator_type);

    auto error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
    }

    if (!error) {
        std::cout << "\nAll SJOperator tests passed!" << std::endl;
    } else {
        std::cout << "\nSome SJOperator tests failed!" << std::endl;
    }

    return error;
}