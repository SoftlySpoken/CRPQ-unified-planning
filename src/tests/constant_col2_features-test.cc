#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_set>

#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include "query/executor/binding_iter/bplus_tree_two_column_binding_iter.h"
#include "query/executor/binding_iter/custom_ops/kc_operator.h"
#include "query/executor/binding_iter/custom_ops/ti_two_way_operator.h"
#include "query/executor/binding_iter/custom_ops/ti_operator.h"
#include "query/executor/binding_iter/custom_ops/union_operator.h"
#include "storage/custom_buffer/two_column_store.h"
#include "storage/index/bplus_tree/bplus_tree.h"
#include "storage/index/record.h"
#include "storage/file_manager.h"
#include "storage/buffer_manager.h"
#include "query/query_context.h"
#include "query/executor/binding_iter_visitor.h"
#include "query/executor/binding.h"
#include "query/var_id.h"
#include <filesystem>

using namespace CustomOps;

typedef bool TestFunction();

// B+ tree test setup and cleanup
class BPlusTreeTestSetup {
private:
    static QueryContext query_ctx;

public:
    static void init() {
        // Clean up any previous test data
        cleanup();

        // Initialize the storage system
        FileManager::init("test_const_col2_db");
        BufferManager::init(40960, 40960, 40960, 1); // Small buffers for testing

        // Set up QueryContext so BufferManager can access it
        QueryContext::set_query_ctx(&query_ctx);
    }

    static void cleanup() {
        // Clean up test files
        std::filesystem::remove_all("test_const_col2_db");
    }
};

// Define the static member
QueryContext BPlusTreeTestSetup::query_ctx;

// Helper function to create test store
std::unique_ptr<TwoColumnStore> create_test_store(const std::vector<std::pair<long long, long long>>& edges) {
    auto store = std::make_unique<TwoColumnStore>();
    for (const auto& edge : edges) {
        store->append(edge.first, edge.second);
    }
    store->compact();
    return store;
}

// Helper function to compare results
template<typename T>
bool compare_results(const std::vector<T>& actual, const std::vector<T>& expected, const std::string& test_name) {
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
            std::cerr << "Expected: " << sorted_expected[i].first << "," << sorted_expected[i].second
                      << " Got: " << sorted_actual[i].first << "," << sorted_actual[i].second << std::endl;
            return true;  // true indicates failure
        }
    }

    return false;  // false indicates success
}

// Mock TwoColumnStoreBindingIter for testing with constant col2 support
class MockTwoColumnStoreBindingIter : public TwoColumnStoreBindingIter {
public:
    MockTwoColumnStoreBindingIter(const std::vector<std::pair<long long, long long>>& edges,
                                  VarId col1_var, VarId col2_var)
        : TwoColumnStoreBindingIter(create_test_store(edges), col1_var, col2_var) {
    }

    // Constructor with constant col1 value
    MockTwoColumnStoreBindingIter(const std::vector<std::pair<long long, long long>>& edges,
                                  ObjectId constant_col1_value, VarId col2_var)
        : TwoColumnStoreBindingIter(create_test_store(edges), constant_col1_value, col2_var) {
    }

    // Constructor with constant col2 value
    MockTwoColumnStoreBindingIter(const std::vector<std::pair<long long, long long>>& edges,
                                  VarId col1_var, ObjectId constant_col2_value)
        : TwoColumnStoreBindingIter(create_test_store(edges), col1_var, constant_col2_value) {
    }
};

// Test TwoColumnStoreBindingIter with constant col2
bool test_two_column_store_constant_col2_basic() {
    std::cout << "Testing TwoColumnStoreBindingIter with constant col2 (basic)..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(10);  // Only return edges ending at vertex 10

    // Create test data: (1,10), (2,20), (3,10), (4,30), (5,10)
    // With constant col2=10, should only return: (1,10), (3,10), (5,10)
    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 10}, {2, 20}, {3, 10}, {4, 30}, {5, 10}
    };

    MockTwoColumnStoreBindingIter iter(input_edges, col1_var, constant_col2_value);

    Binding binding(10);
    iter.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (iter.next()) {
        long long col1 = binding[col1_var].id;
        // col2 should always be the constant value
        actual_edges.push_back({col1, constant_col2_value.id});
        std::cout << col1 << ' ' << constant_col2_value.id << std::endl;
    }

    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 10}, {3, 10}, {5, 10}
    };

    if (compare_results(actual_edges, expected_edges, "test_two_column_store_constant_col2_basic")) {
        return true;
    }

    std::cout << "TwoColumnStoreBindingIter constant col2 basic test passed!" << std::endl;
    return false;
}

// Test TwoColumnStoreBindingIter constant col2 with no matches
bool test_two_column_store_constant_col2_no_matches() {
    std::cout << "Testing TwoColumnStoreBindingIter with constant col2 (no matches)..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(99);  // Vertex that doesn't exist as target

    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 10}, {2, 20}, {3, 10}, {4, 30}
    };

    MockTwoColumnStoreBindingIter iter(input_edges, col1_var, constant_col2_value);

    Binding binding(10);
    iter.begin(binding);

    if (iter.next()) {
        std::cerr << "Expected no results for non-existent constant col2" << std::endl;
        return true;
    }

    std::cout << "TwoColumnStoreBindingIter constant col2 no matches test passed!" << std::endl;
    return false;
}

// Test KCOperator with constant col2
bool test_kc_operator_constant_col2_basic() {
    std::cout << "Testing KCOperator with constant col2 (basic)..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(3);  // Only paths ending at vertex 3

    // Create test data: 1->2, 2->3, 1->4, 4->5, 5->3
    // With constant col2=3, should return paths ending at 3: (1,3), (2,3), (5,3)
    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}, {1, 4}, {4, 5}, {5, 3}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, col1_var, VarId(1));

    KCOperator kc_op(std::move(child), col1_var, constant_col2_value, false);

    Binding binding(10);
    kc_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (kc_op.next()) {
        long long col1 = binding[col1_var].id;
        actual_edges.push_back({col1, constant_col2_value.id});
    }

    // Expected: direct paths to 3 + transitive paths to 3
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 3}, {2, 3}, {4, 3}, {5, 3}
    };

    if (compare_results(actual_edges, expected_edges, "test_kc_operator_constant_col2_basic")) {
        return true;
    }

    std::cout << "KCOperator constant col2 basic test passed!" << std::endl;
    return false;
}

// Test KCOperator constant col2 with cycles
bool test_kc_operator_constant_col2_cycle() {
    std::cout << "Testing KCOperator with constant col2 (cycle)..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(2);  // Only paths ending at vertex 2

    // Create test data with cycle: 1->2, 2->3, 3->1, 4->2
    // With constant col2=2, should return: (1,2), (3,2), (4,2)
    std::vector<std::pair<long long, long long>> input_edges = {
        {1, 2}, {2, 3}, {3, 1}, {4, 2}
    };

    auto child = std::make_unique<MockTwoColumnStoreBindingIter>(
        input_edges, col1_var, VarId(1));

    KCOperator kc_op(std::move(child), col1_var, constant_col2_value, false);

    Binding binding(10);
    kc_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (kc_op.next()) {
        long long col1 = binding[col1_var].id;
        actual_edges.push_back({col1, constant_col2_value.id});
    }

    // Expected: all vertices that can reach vertex 2
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 2}, {2, 2}, {3, 2}, {4, 2}
    };

    if (compare_results(actual_edges, expected_edges, "test_kc_operator_constant_col2_cycle")) {
        return true;
    }

    std::cout << "KCOperator constant col2 cycle test passed!" << std::endl;
    return false;
}

// Test TITwoWayOperator with constant col2
bool test_ti_two_way_operator_constant_col2() {
    std::cout << "Testing TITwoWayOperator with constant col2..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(5);  // Only paths ending at vertex 5

    // Create leftmost operand: vertices to start from
    std::vector<std::pair<long long, long long>> leftmost_edges = {
        {1, 2}, {3, 4}  // Starting points: 2 (from 1) and 4 (from 3)
    };

    // Create edge path operand: paths from those vertices
    std::vector<std::pair<long long, long long>> edge_path_edges = {
        {2, 5}, {2, 6}, {4, 5}, {4, 7}  // From 2: can reach 5,6; From 4: can reach 5,7
    };

    auto leftmost_operand = std::make_unique<MockTwoColumnStoreBindingIter>(
        leftmost_edges, ObjectId(1), VarId(1));  // Starting from vertex 1
    auto edge_path_operand = std::make_unique<MockTwoColumnStoreBindingIter>(
        edge_path_edges, VarId(2), VarId(3));

    TITwoWayOperator ti_op(std::move(leftmost_operand), std::move(edge_path_operand),
                          col1_var, constant_col2_value, false);

    Binding binding(10);
    ti_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (ti_op.next()) {
        long long col1 = binding[col1_var].id;
        actual_edges.push_back({col1, constant_col2_value.id});
    }

    // Expected: 1->2->5, so result should be (1,5)
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 5}
    };

    if (compare_results(actual_edges, expected_edges, "test_ti_two_way_operator_constant_col2")) {
        return true;
    }

    std::cout << "TITwoWayOperator constant col2 test passed!" << std::endl;
    return false;
}

bool test_ti_operator_constant_col2() {
    std::cout << "Testing TIOperator with constant col2..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(5);  // Only paths ending at vertex 5

    // Create leftmost operand: vertices to start from
    std::vector<std::pair<long long, long long>> leftmost_edges = {
        {1, 2}, {3, 4}  // Starting points: 2 (from 1) and 4 (from 3)
    };

    // Create edge path operand: paths from those vertices
    std::vector<std::pair<long long, long long>> edge_path_edges = {
        {2, 5}, {2, 6}, {4, 5}, {4, 7}  // From 2: can reach 5,6; From 4: can reach 5,7
    };

    auto leftmost_operand = std::make_unique<MockTwoColumnStoreBindingIter>(
        leftmost_edges, ObjectId(1), VarId(1));  // Starting from vertex 1
    auto edge_path_operand = std::make_unique<MockTwoColumnStoreBindingIter>(
        edge_path_edges, VarId(1), ObjectId(5));
    std::vector<std::unique_ptr<TwoColumnBindingIter>> epo;
    epo.emplace_back(std::move(edge_path_operand));

    TIOperator ti_op(std::move(leftmost_operand), std::move(epo));

    Binding binding(10);
    ti_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (ti_op.next()) {
        long long col1 = binding[VarId(1)].id;
        actual_edges.push_back({col1, constant_col2_value.id});
        std::cout << col1 << ' ' << constant_col2_value.id << std::endl;
    }

    // Expected: 1->2->5, so result should be (1,5)
    std::vector<std::pair<long long, long long>> expected_edges = {
        {2, 5}
    };

    if (compare_results(actual_edges, expected_edges, "test_ti_operator_constant_col2")) {
        return true;
    }

    std::cout << "TIOperator constant col2 test passed!" << std::endl;
    return false;
}

// Test UnionOperator with constant col2
bool test_union_operator_constant_col2() {
    std::cout << "Testing UnionOperator with constant col2..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(10);  // Only union edges ending at vertex 10

    // Create child iterators with some edges ending at 10, some not
    std::vector<std::unique_ptr<TwoColumnBindingIter>> children;

    // Child 1: (1,10), (1,20)
    children.push_back(std::make_unique<MockTwoColumnStoreBindingIter>(
        std::vector<std::pair<long long, long long>>{{1, 10}, {1, 20}}, col1_var, VarId(1)));

    // Child 2: (2,10), (3,30)
    children.push_back(std::make_unique<MockTwoColumnStoreBindingIter>(
        std::vector<std::pair<long long, long long>>{{2, 10}, {3, 30}}, col1_var, VarId(1)));

    // Child 3: (4,40), (5,10)
    children.push_back(std::make_unique<MockTwoColumnStoreBindingIter>(
        std::vector<std::pair<long long, long long>>{{4, 40}, {5, 10}}, col1_var, VarId(1)));

    UnionOperator union_op(std::move(children), col1_var, constant_col2_value, true);

    Binding binding(10);
    union_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (union_op.next()) {
        long long col1 = binding[col1_var].id;
        actual_edges.push_back({col1, constant_col2_value.id});
    }

    // Expected: only edges ending at vertex 10
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 10}, {2, 10}, {5, 10}
    };

    if (compare_results(actual_edges, expected_edges, "test_union_operator_constant_col2")) {
        return true;
    }

    std::cout << "UnionOperator constant col2 test passed!" << std::endl;
    return false;
}

// Test UnionOperator duplicate detection with constant col2
bool test_union_operator_constant_col2_duplicates() {
    std::cout << "Testing UnionOperator with constant col2 (duplicates)..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(10);

    std::vector<std::unique_ptr<TwoColumnBindingIter>> children;

    // Child 1: (1,10), (2,10)
    children.push_back(std::make_unique<MockTwoColumnStoreBindingIter>(
        std::vector<std::pair<long long, long long>>{{1, 10}, {2, 10}}, col1_var, VarId(1)));

    // Child 2: (1,10), (3,10) - duplicate (1,10)
    children.push_back(std::make_unique<MockTwoColumnStoreBindingIter>(
        std::vector<std::pair<long long, long long>>{{1, 10}, {3, 10}}, col1_var, VarId(1)));

    UnionOperator union_op(std::move(children), col1_var, constant_col2_value, true);  // eliminate duplicates

    Binding binding(10);
    union_op.begin(binding);

    std::vector<std::pair<long long, long long>> actual_edges;
    while (union_op.next()) {
        long long col1 = binding[col1_var].id;
        actual_edges.push_back({col1, constant_col2_value.id});
    }

    // Expected: no duplicates
    std::vector<std::pair<long long, long long>> expected_edges = {
        {1, 10}, {2, 10}, {3, 10}
    };

    if (compare_results(actual_edges, expected_edges, "test_union_operator_constant_col2_duplicates")) {
        return true;
    }

    std::cout << "UnionOperator constant col2 duplicates test passed!" << std::endl;
    return false;
}

// Test edge case: constant col2 with empty input
bool test_constant_col2_empty_input() {
    std::cout << "Testing constant col2 with empty input..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(10);

    std::vector<std::pair<long long, long long>> empty_edges = {};

    MockTwoColumnStoreBindingIter iter(empty_edges, col1_var, constant_col2_value);

    Binding binding(10);
    iter.begin(binding);

    if (iter.next()) {
        std::cerr << "Expected no results for empty input with constant col2" << std::endl;
        return true;
    }

    std::cout << "Constant col2 empty input test passed!" << std::endl;
    return false;
}

// Test edge case: constant col2 accessor methods
bool test_constant_col2_accessors() {
    std::cout << "Testing constant col2 accessor methods..." << std::endl;

    VarId col1_var(0);
    ObjectId constant_col2_value(42);

    std::vector<std::pair<long long, long long>> test_edges = {{1, 42}};

    MockTwoColumnStoreBindingIter iter(test_edges, col1_var, constant_col2_value);

    // Test accessor methods
    if (!iter.get_has_constant_col2()) {
        std::cerr << "get_has_constant_col2() should return true" << std::endl;
        return true;
    }

    if (iter.get_constant_col2().id != constant_col2_value.id) {
        std::cerr << "get_constant_col2() returned wrong value: expected "
                  << constant_col2_value.id << ", got " << iter.get_constant_col2().id << std::endl;
        return true;
    }

    // Should not have constant col1
    if (iter.get_has_constant_col1()) {
        std::cerr << "get_has_constant_col1() should return false" << std::endl;
        return true;
    }

    std::cout << "Constant col2 accessor methods test passed!" << std::endl;
    return false;
}

// B+ Tree Tests for Constant col2

/**
 * Test PSO index with constant col2 (object) value
 */
bool test_bplus_tree_pso_constant_col2_basic() {
    std::cout << "Testing BPlusTreeTwoColumnBindingIter PSO with constant col2 (basic)..." << std::endl;

    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_pso_const_col2_basic");

        // Add test data: (predicate=1, subject=10, object=20), (predicate=1, subject=30, object=20)
        // and (predicate=1, subject=40, object=50) - only first two should match constant col2=20
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 10; // subject
        record1[2] = 20; // object - matches constant
        btree->insert(record1);

        Record<3> record2;
        record2[0] = 1; // predicate
        record2[1] = 30; // subject
        record2[2] = 20; // object - matches constant
        btree->insert(record2);

        Record<3> record3;
        record3[0] = 1; // predicate
        record3[1] = 40; // subject
        record3[2] = 50; // object - doesn't match constant
        btree->insert(record3);

        // Create iterator with constant object=20
        ObjectId predicate(1);
        VarId subject_var(0); // col1_var = subject in PSO
        ObjectId constant_object(20); // constant col2

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, constant_object,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(1);
        iter.begin(binding);

        // Collect results
        std::vector<std::pair<uint64_t, uint64_t>> results;
        while (iter.next()) {
            ObjectId subject = binding[subject_var];
            // col2 should always be the constant value
            results.push_back({subject.id, constant_object.id});
            std::cout << subject.id << ' ' << constant_object.id << std::endl;
        }

        // Should only get subjects for object=20
        std::vector<std::pair<uint64_t, uint64_t>> expected_edges = {
            {10, 20}, {30, 20}
        };

        if (compare_results(results, expected_edges, "test_bplus_tree_pso_constant_col2_basic")) {
            return true;
        }

        std::cout << "BPlusTreeTwoColumnBindingIter PSO constant col2 basic test passed!" << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_bplus_tree_pso_constant_col2_basic: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test POS index with constant col2 (subject) value
 */
bool test_bplus_tree_pos_constant_col2_basic() {
    std::cout << "Testing BPlusTreeTwoColumnBindingIter POS with constant col2 (basic)..." << std::endl;

    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_pos_const_col2_basic");

        // Add test data: (predicate=1, object=20, subject=10), (predicate=1, object=30, subject=10)
        // and (predicate=1, object=40, subject=50) - only first two should match constant col2=10
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 20; // object
        record1[2] = 10; // subject - matches constant
        btree->insert(record1);

        Record<3> record2;
        record2[0] = 1; // predicate
        record2[1] = 30; // object
        record2[2] = 10; // subject - matches constant
        btree->insert(record2);

        Record<3> record3;
        record3[0] = 1; // predicate
        record3[1] = 40; // object
        record3[2] = 50; // subject - doesn't match constant
        btree->insert(record3);

        // Create iterator with constant subject=10
        ObjectId predicate(1);
        VarId object_var(0); // col1_var = object in POS
        ObjectId constant_subject(10); // constant col2

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, object_var, constant_subject,
                                              BPlusTreeTwoColumnBindingIter<3>::POS);

        // Create binding context
        Binding binding(1);
        iter.begin(binding);

        // Collect results
        std::vector<std::pair<uint64_t, uint64_t>> results;
        while (iter.next()) {
            ObjectId object = binding[object_var];
            // col2 should always be the constant value
            results.push_back({object.id, constant_subject.id});
            std::cout << object.id << ' ' << constant_subject.id << std::endl;
        }

        // Should only get objects for subject=10
        std::vector<std::pair<uint64_t, uint64_t>> expected_edges = {
            {20, 10}, {30, 10}
        };

        if (compare_results(results, expected_edges, "test_bplus_tree_pos_constant_col2_basic")) {
            return true;
        }

        std::cout << "BPlusTreeTwoColumnBindingIter POS constant col2 basic test passed!" << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_bplus_tree_pos_constant_col2_basic: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test constant col2 with no matches
 */
bool test_bplus_tree_constant_col2_no_matches() {
    std::cout << "Testing BPlusTreeTwoColumnBindingIter with constant col2 (no matches)..." << std::endl;

    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_const_col2_no_matches");

        // Add test data
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 10; // subject
        record1[2] = 20; // object
        btree->insert(record1);

        // Create iterator with nonexistent constant object=999
        ObjectId predicate(1);
        VarId subject_var(0);
        ObjectId constant_object(999); // doesn't exist

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, constant_object,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(1);
        iter.begin(binding);

        // Should have no results
        if (iter.next()) {
            std::cerr << "Expected no results for nonexistent constant col2" << std::endl;
            return true;
        }

        std::cout << "BPlusTreeTwoColumnBindingIter constant col2 no matches test passed!" << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_bplus_tree_constant_col2_no_matches: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test constant col2 accessor methods
 */
bool test_bplus_tree_constant_col2_accessors() {
    std::cout << "Testing BPlusTreeTwoColumnBindingIter constant col2 accessor methods..." << std::endl;

    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_const_col2_accessors");

        ObjectId predicate(1);
        VarId subject_var(0);
        ObjectId constant_object(42);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, constant_object,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Test accessor methods
        if (!iter.get_has_constant_col2()) {
            std::cerr << "get_has_constant_col2() should return true" << std::endl;
            return true;
        }

        if (iter.get_constant_col2().id != constant_object.id) {
            std::cerr << "get_constant_col2() returned wrong value: expected "
                      << constant_object.id << ", got " << iter.get_constant_col2().id << std::endl;
            return true;
        }

        // Should not have constant col1
        if (iter.get_has_constant_col1()) {
            std::cerr << "get_has_constant_col1() should return false" << std::endl;
            return true;
        }

        std::cout << "BPlusTreeTwoColumnBindingIter constant col2 accessor methods test passed!" << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_bplus_tree_constant_col2_accessors: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test constant col2 with empty B+ tree
 */
bool test_bplus_tree_constant_col2_empty() {
    std::cout << "Testing BPlusTreeTwoColumnBindingIter with constant col2 (empty tree)..." << std::endl;

    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_const_col2_empty");

        ObjectId predicate(1);
        VarId subject_var(0);
        ObjectId constant_object(42);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, constant_object,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(1);
        iter.begin(binding);

        // Should have no results
        if (iter.next()) {
            std::cerr << "Expected no results for empty tree with constant col2" << std::endl;
            return true;
        }

        std::cout << "BPlusTreeTwoColumnBindingIter constant col2 empty tree test passed!" << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_bplus_tree_constant_col2_empty: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test constant col2 with reset functionality
 */
bool test_bplus_tree_constant_col2_reset() {
    std::cout << "Testing BPlusTreeTwoColumnBindingIter constant col2 reset functionality..." << std::endl;

    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_const_col2_reset");

        // Add test data
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 10; // subject
        record1[2] = 20; // object
        btree->insert(record1);

        Record<3> record2;
        record2[0] = 1; // predicate
        record2[1] = 30; // subject
        record2[2] = 20; // object
        btree->insert(record2);

        ObjectId predicate(1);
        VarId subject_var(0);
        ObjectId constant_object(20);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, constant_object,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(1);
        iter.begin(binding);

        // First iteration
        int first_count = 0;
        while (iter.next()) {
            first_count++;
        }

        // Reset and iterate again
        iter.reset();
        int second_count = 0;
        while (iter.next()) {
            second_count++;
        }

        if (first_count != 2 || second_count != 2) {
            std::cerr << "Reset test failed: first count=" << first_count
                      << ", second count=" << second_count << std::endl;
            return true;
        }

        std::cout << "BPlusTreeTwoColumnBindingIter constant col2 reset test passed!" << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_bplus_tree_constant_col2_reset: " << e.what() << std::endl;
        return true;
    }
}

int main() {
    // Initialize the storage system for B+ tree tests
    BPlusTreeTestSetup::init();

    std::vector<TestFunction*> tests;

    // TwoColumnStoreBindingIter constant col2 tests
    tests.push_back(&test_two_column_store_constant_col2_basic);
    tests.push_back(&test_two_column_store_constant_col2_no_matches);

    // KCOperator constant col2 tests
    tests.push_back(&test_kc_operator_constant_col2_basic);
    tests.push_back(&test_kc_operator_constant_col2_cycle);

    // TITwoWayOperator constant col2 tests
    tests.push_back(&test_ti_two_way_operator_constant_col2);
    tests.push_back(&test_ti_operator_constant_col2);

    // UnionOperator constant col2 tests
    tests.push_back(&test_union_operator_constant_col2);
    tests.push_back(&test_union_operator_constant_col2_duplicates);

    // Edge case tests
    tests.push_back(&test_constant_col2_empty_input);
    tests.push_back(&test_constant_col2_accessors);

    // BPlusTreeTwoColumnBindingIter constant col2 tests
    tests.push_back(&test_bplus_tree_pso_constant_col2_basic);
    tests.push_back(&test_bplus_tree_pos_constant_col2_basic);
    tests.push_back(&test_bplus_tree_constant_col2_no_matches);
    tests.push_back(&test_bplus_tree_constant_col2_accessors);
    tests.push_back(&test_bplus_tree_constant_col2_empty);
    tests.push_back(&test_bplus_tree_constant_col2_reset);

    bool error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
    }

    // Clean up the storage system
    BPlusTreeTestSetup::cleanup();

    if (!error) {
        std::cout << "\nAll constant col2 features tests passed!" << std::endl;
    } else {
        std::cout << "\nSome constant col2 features tests failed!" << std::endl;
    }

    return error;
}