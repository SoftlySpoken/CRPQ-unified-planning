#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>

#include "query/executor/binding_iter/custom_ops/union_operator.h"
#include "query/executor/binding_iter/two_column_binding_iter.h"
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

// Mock TwoColumnBindingIter for testing
class MockTwoColumnBindingIter : public TwoColumnBindingIter {
private:
    std::vector<std::pair<uint64_t, uint64_t>> results;
    size_t current_result;

public:
    MockTwoColumnBindingIter(const std::vector<std::pair<uint64_t, uint64_t>>& data, VarId col1_var, VarId col2_var)
        : TwoColumnBindingIter(col1_var, col2_var), results(data), current_result(0) {}

    MockTwoColumnBindingIter(const std::vector<std::pair<uint64_t, uint64_t>>& data, ObjectId constant_col1, VarId col2_var)
        : TwoColumnBindingIter(constant_col1, col2_var), results(data), current_result(0) {}

protected:
    bool advance_to_next_edge() override {
        if (current_result >= results.size()) {
            return false;
        }

        // Set values in parent binding
        if (has_constant_col1) {
            (*parent_binding_ptr)[col2_var] = ObjectId(results[current_result].second);
        } else {
            (*parent_binding_ptr)[col1_var] = ObjectId(results[current_result].first);
            (*parent_binding_ptr)[col2_var] = ObjectId(results[current_result].second);
        }

        current_result++;
        return true;
    }

    void reset_iteration_state() override {
        current_result = 0;
    }

public:
    void get_current_edge(std::pair<ObjectId, ObjectId> &pr) override {
        if (current_result > 0 && current_result <= results.size()) {
            pr.first = ObjectId(results[current_result - 1].first);
            pr.second = ObjectId(results[current_result - 1].second);
        }
    }

    bool seek_to_vertex(uint64_t vertex) override {
        // Simple implementation: reset and advance until we find the vertex
        current_result = 0;
        while (current_result < results.size()) {
            if (results[current_result].first == vertex) {
                return true;
            }
            current_result++;
        }
        return false;
    }

    std::pair<const uint64_t*, size_t> get_neighbors(uint64_t vid) const override {
        // Simplified mock implementation
        static std::vector<uint64_t> neighbors;
        neighbors.clear();
        for (const auto& edge : results) {
            if (edge.first == vid) {
                neighbors.push_back(edge.second);
            }
        }
        return {neighbors.data(), neighbors.size()};
    }

    void accept_visitor(BindingIterVisitor& visitor) override {
        // Mock implementation
    }
};

bool test_basic_union() {
    std::cout << "Testing basic union functionality..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);

    // Create child iterators
    std::vector<std::unique_ptr<TwoColumnBindingIter>> children;

    // Child 1: results (1,10), (2,20)
    children.push_back(std::make_unique<MockTwoColumnBindingIter>(
        std::vector<std::pair<uint64_t, uint64_t>>{{1, 10}, {2, 20}}, var_x, var_y));

    // Child 2: results (3,30), (4,40)
    children.push_back(std::make_unique<MockTwoColumnBindingIter>(
        std::vector<std::pair<uint64_t, uint64_t>>{{3, 30}, {4, 40}}, var_x, var_y));

    UnionOperator union_op(std::move(children), var_x, var_y, false);

    Binding binding(2);
    union_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> expected = {{1,10}, {2,20}, {3,30}, {4,40}};
    std::vector<std::pair<uint64_t, uint64_t>> actual;

    while (union_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id});
    }

    if (compare_results(actual, expected, "test_basic_union")) {
        return true;
    }

    std::cout << "Basic union test passed!" << std::endl;
    return false;
}

bool test_union_with_duplicates() {
    std::cout << "Testing union with duplicate elimination..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);

    std::vector<std::unique_ptr<TwoColumnBindingIter>> children;

    // Child 1: results (1,10), (2,20), (3,30)
    children.push_back(std::make_unique<MockTwoColumnBindingIter>(
        std::vector<std::pair<uint64_t, uint64_t>>{{1, 10}, {2, 20}, {3, 30}}, var_x, var_y));

    // Child 2: results (2,20), (3,30), (4,40) - has duplicates with child 1
    children.push_back(std::make_unique<MockTwoColumnBindingIter>(
        std::vector<std::pair<uint64_t, uint64_t>>{{2, 20}, {3, 30}, {4, 40}}, var_x, var_y));

    UnionOperator union_op(std::move(children), var_x, var_y, true); // Enable duplicate elimination

    Binding binding(10);
    union_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual;
    while (union_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id});
    }

    // Should have unique pairs: (1,10), (2,20), (3,30), (4,40)
    std::vector<std::pair<uint64_t, uint64_t>> expected = {{1,10}, {2,20}, {3,30}, {4,40}};

    if (actual.size() != expected.size()) {
        std::cerr << "Expected " << expected.size() << " unique results, got " << actual.size() << std::endl;
        return true;
    }

    // Sort both to compare (order might vary due to hash-based deduplication)
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());

    for (size_t i = 0; i < expected.size(); ++i) {
        if (actual[i] != expected[i]) {
            std::cerr << "Mismatch at position " << i << ": expected ("
                      << expected[i].first << "," << expected[i].second
                      << "), got (" << actual[i].first << "," << actual[i].second << ")" << std::endl;
            return true;
        }
    }

    std::cout << "Union with duplicates test passed!" << std::endl;
    return false;
}

bool test_empty_children() {
    std::cout << "Testing union with empty children..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);

    std::vector<std::unique_ptr<TwoColumnBindingIter>> children;

    // Child 1: empty results
    children.push_back(std::make_unique<MockTwoColumnBindingIter>(
        std::vector<std::pair<uint64_t, uint64_t>>{}, var_x, var_y));

    // Child 2: results (1,10), (2,20)
    children.push_back(std::make_unique<MockTwoColumnBindingIter>(
        std::vector<std::pair<uint64_t, uint64_t>>{{1, 10}, {2, 20}}, var_x, var_y));

    // Child 3: empty results
    children.push_back(std::make_unique<MockTwoColumnBindingIter>(
        std::vector<std::pair<uint64_t, uint64_t>>{}, var_x, var_y));

    UnionOperator union_op(std::move(children), var_x, var_y, false);

    Binding binding(10);
    union_op.begin(binding);

    std::vector<std::pair<uint64_t, uint64_t>> actual;
    while (union_op.next()) {
        actual.push_back({binding[var_x].id, binding[var_y].id});
    }

    std::vector<std::pair<uint64_t, uint64_t>> expected = {{1, 10}, {2, 20}};

    if (compare_results(actual, expected, "test_empty_children")) {
        return true;
    }

    std::cout << "Empty children test passed!" << std::endl;
    return false;
}

bool test_no_children() {
    std::cout << "Testing union with no children..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);
    std::vector<std::unique_ptr<TwoColumnBindingIter>> children; // Empty

    UnionOperator union_op(std::move(children), var_x, var_y, false);

    Binding binding(10);
    union_op.begin(binding);

    if (union_op.next()) {
        std::cerr << "Expected no results from empty union" << std::endl;
        return true;
    }

    std::cout << "No children test passed!" << std::endl;
    return false;
}

bool test_reset_functionality() {
    std::cout << "Testing reset functionality..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);

    std::vector<std::unique_ptr<TwoColumnBindingIter>> children;

    children.push_back(std::make_unique<MockTwoColumnBindingIter>(
        std::vector<std::pair<uint64_t, uint64_t>>{{1, 10}, {2, 20}}, var_x, var_y));

    UnionOperator union_op(std::move(children), var_x, var_y, false);

    Binding binding(10);
    union_op.begin(binding);

    // First iteration
    std::vector<std::pair<uint64_t, uint64_t>> first_run;
    while (union_op.next()) {
        first_run.push_back({binding[var_x].id, binding[var_y].id});
    }

    // Reset and iterate again
    union_op.reset();
    std::vector<std::pair<uint64_t, uint64_t>> second_run;
    while (union_op.next()) {
        second_run.push_back({binding[var_x].id, binding[var_y].id});
    }

    if (compare_results(second_run, first_run, "test_reset_functionality")) {
        return true;
    }

    std::cout << "Reset functionality test passed!" << std::endl;
    return false;
}

bool test_union_modes() {
    std::cout << "Testing union modes..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);

    // Test UNION_ALL mode
    {
        std::vector<std::unique_ptr<TwoColumnBindingIter>> children;
        children.push_back(std::make_unique<MockTwoColumnBindingIter>(
            std::vector<std::pair<uint64_t, uint64_t>>{{1, 10}, {1, 10}}, var_x, var_y)); // Duplicates

        UnionOperator union_op(std::move(children), var_x, var_y, false);
        union_op.set_union_mode(UnionOperator::UnionMode::UNION_ALL);

        Binding binding(10);
        union_op.begin(binding);

        int count = 0;
        while (union_op.next()) {
            count++;
        }

        if (count != 2) {
            std::cerr << "UNION_ALL should preserve duplicates" << std::endl;
            return true;
        }
    }

    // Test UNION_DISTINCT mode
    {
        std::vector<std::unique_ptr<TwoColumnBindingIter>> children;
        children.push_back(std::make_unique<MockTwoColumnBindingIter>(
            std::vector<std::pair<uint64_t, uint64_t>>{{1, 10}, {1, 10}}, var_x, var_y)); // Duplicates

        UnionOperator union_op(std::move(children), var_x, var_y, false);
        union_op.set_union_mode(UnionOperator::UnionMode::UNION_DISTINCT);

        Binding binding(10);
        union_op.begin(binding);

        int count = 0;
        while (union_op.next()) {
            count++;
        }

        if (count != 1) {
            std::cerr << "UNION_DISTINCT should eliminate duplicates" << std::endl;
            return true;
        }
    }

    std::cout << "Union modes test passed!" << std::endl;
    return false;
}

// test_operator_type removed - method doesn't exist in UnionOperator

bool test_add_child() {
    std::cout << "Testing add_child functionality..." << std::endl;

    VarId var_x(0);
    VarId var_y(1);

    std::vector<std::unique_ptr<TwoColumnBindingIter>> children;
    UnionOperator union_op(std::move(children), var_x, var_y, false);

    // Initially no children
    if (union_op.get_num_children() != 0) {
        std::cerr << "Expected 0 children initially" << std::endl;
        return true;
    }

    // Add a child
    union_op.add_child(std::make_unique<MockTwoColumnBindingIter>(
        std::vector<std::pair<uint64_t, uint64_t>>{{1, 10}}, var_x, var_y));

    if (union_op.get_num_children() != 1) {
        std::cerr << "Expected 1 child after adding" << std::endl;
        return true;
    }

    std::cout << "Add child test passed!" << std::endl;
    return false;
}

int main() {
    std::vector<TestFunction*> tests;

    tests.push_back(&test_basic_union);
    tests.push_back(&test_union_with_duplicates);
    tests.push_back(&test_empty_children);
    tests.push_back(&test_no_children);
    tests.push_back(&test_reset_functionality);
    tests.push_back(&test_union_modes);
    tests.push_back(&test_add_child);

    auto error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
    }

    if (!error) {
        std::cout << "\nAll UnionOperator tests passed!" << std::endl;
    } else {
        std::cout << "\nSome UnionOperator tests failed!" << std::endl;
    }

    return error;
}