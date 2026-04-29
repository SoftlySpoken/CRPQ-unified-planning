#include <iostream>
#include <vector>
#include <algorithm>
#include <memory>
#include <filesystem>
#include <unordered_set>

// BPlusTree and storage infrastructure
#include "query/executor/binding_iter/bplus_tree_two_column_binding_iter.h"
#include "storage/index/bplus_tree/bplus_tree.h"
#include "storage/index/record.h"
#include "storage/file_manager.h"
#include "storage/buffer_manager.h"
#include "query/executor/binding.h"
#include "query/executor/binding_iter_visitor.h"
#include "graph_models/object_id.h"
#include "query/var_id.h"
#include "query/query_context.h"

// Custom operators
#include "query/executor/binding_iter/custom_ops/kc_operator.h"
#include "query/executor/binding_iter/custom_ops/mc_operator.h"
#include "query/executor/binding_iter/custom_ops/ti_two_way_operator.h"
#include "query/executor/binding_iter/custom_ops/ti_operator.h"
#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include "storage/custom_buffer/two_column_store.h"

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

// Test setup and cleanup
class TestSetup {
private:
    static QueryContext query_ctx;

public:
    static void init() {
        // Clean up any previous test data
        cleanup();

        // Initialize the storage system
        FileManager::init("test_custom_ops_db");
        BufferManager::init(40960, 40960, 40960, 1); // Small buffers for testing

        // Set up QueryContext so BufferManager can access it
        QueryContext::set_query_ctx(&query_ctx);
    }

    static void cleanup() {
        // Clean up test files
        std::filesystem::remove_all("test_custom_ops_db");
    }
};

// Define the static member
QueryContext TestSetup::query_ctx;

// Mock BindingIter for TI Operator testing (since TI needs a general BindingIter as leftmost operand)
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
        // Mock implementation - empty for testing purposes
    }
};

// Helper function to create a B+ tree with test data
template<int N>
std::unique_ptr<BPlusTree<N>> create_test_btree(const std::string& name,
                                                const std::vector<std::array<uint64_t, N>>& records) {
    // Make name unique by adding timestamp/counter to avoid conflicts
    static int counter = 0;
    std::string unique_name = name + "_" + std::to_string(++counter);
    auto btree = std::make_unique<BPlusTree<N>>(unique_name);

    for (const auto& record_data : records) {
        Record<N> record;
        for (int i = 0; i < N; ++i) {
            record[i] = record_data[i];
        }
        btree->insert(record);
    }

    return btree;
}

// Helper function to create BPlusTreeTwoColumnBindingIter from B+ tree data
// Returns both the B+ tree and iterator to ensure proper lifetime management
std::pair<std::unique_ptr<BPlusTree<3>>, std::unique_ptr<BPlusTreeTwoColumnBindingIter<3>>>
create_bplus_two_column_iter(
    const std::string& name,
    const std::vector<std::array<uint64_t, 3>>& records,
    ObjectId fixed_predicate,
    VarId col1_var,
    VarId col2_var,
    BPlusTreeTwoColumnBindingIter<3>::IndexType index_type) {

    auto btree = create_test_btree<3>(name, records);
    auto iter = std::make_unique<BPlusTreeTwoColumnBindingIter<3>>(
        *btree, fixed_predicate, col1_var, col2_var, index_type);
    return std::make_pair(std::move(btree), std::move(iter));
}

// Helper function to create TwoColumnStore from test data
std::unique_ptr<TwoColumnStore> create_test_store(const std::vector<std::pair<long long, long long>>& edges) {
    auto store = std::make_unique<TwoColumnStore>();
    for (const auto& edge : edges) {
        store->append(edge.first, edge.second);
    }
    store->compact();
    return store;
}

// Helper function to collect results from any BindingIter
template<typename T>
std::vector<std::pair<uint64_t, uint64_t>> collect_two_column_results(T& iter, VarId col1_var, VarId col2_var) {
    std::vector<std::pair<uint64_t, uint64_t>> results;
    Binding binding(10);
    iter.begin(binding);

    while (iter.next()) {
        ObjectId col1_val = binding[col1_var];
        ObjectId col2_val = binding[col2_var];
        results.push_back({col1_val.id, col2_val.id});
        std::cout << col1_val.id << " " << col2_val.id << std::endl;
    }

    std::sort(results.begin(), results.end());
    return results;
}

// Test 1: Basic BPlusTree with KC Operator
bool test_bplus_tree_with_kc_operator() {
    std::cout << "Testing BPlusTree with KC Operator..." << std::endl;

    try {
        VarId source_var(0);
        VarId target_var(1);

        // Create test data in B+ tree: edges forming a chain 1->2->3
        // PSO format: [predicate, subject, object]
        std::vector<std::array<uint64_t, 3>> test_data = {
            {1, 1, 2}, // predicate=1, subject=1, object=2 (1->2)
            {1, 2, 3}  // predicate=1, subject=2, object=3 (2->3)
        };

        // Create BPlusTreeTwoColumnBindingIter for predicate=1 using PSO index
        auto [bplus_tree, bplus_iter] = create_bplus_two_column_iter(
            "test_kc_btree", test_data, ObjectId(1), source_var, target_var,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create KC operator (Kleene Closure)
        KCOperator kc_op(std::move(bplus_iter), source_var, target_var, false);

        // Collect results
        auto results = collect_two_column_results(kc_op, source_var, target_var);

        // Expected: original edges (1,2), (2,3) plus transitive closure (1,3)
        std::vector<std::pair<uint64_t, uint64_t>> expected = {
            {1, 2}, {2, 3}, {1, 3}
        };
        if (compare_results(results, expected, "test_bplus_tree_with_kc_operator"))
            return true;
        std::cout << "BPlusTree with KC Operator test passed!" << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_bplus_tree_with_kc_operator: " << e.what() << std::endl;
        return true;
    }
}

// Test 2: BPlusTree with MC Operator
bool test_bplus_tree_with_mc_operator() {
    std::cout << "Testing BPlusTree with MC Operator..." << std::endl;

    try {
        VarId x(0), y(1), z(2);

        // Create two B+ trees for MC operator inputs
        // Tree A: 1->2, 3->4
        std::vector<std::array<uint64_t, 3>> data_A = {
            {1, 1, 2}, // 1->2
            {1, 3, 4}  // 3->4
        };

        // Tree B: 2->5, 4->6
        std::vector<std::array<uint64_t, 3>> data_B = {
            {1, 2, 5}, // 2->5
            {1, 4, 6}  // 4->6
        };

        // Create BPlusTree iterators

        auto [bplus_tree_B, bplus_iter_B] = create_bplus_two_column_iter(
            "test_mc_btree_B", data_B, ObjectId(1), y, z,
            BPlusTreeTwoColumnBindingIter<3>::PSO);
        auto [bplus_tree_A, bplus_iter_A] = create_bplus_two_column_iter(
            "test_mc_btree_A", data_A, ObjectId(1), x, y,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create MC operator (Merge Closure)
        MCOperator mc_op(std::move(bplus_iter_A), std::move(bplus_iter_B), false, false);

        // Collect results
        auto results = collect_two_column_results(mc_op, x, z);

        // Expected: concatenation results 1->5 (1->2->5), 3->6 (3->4->6)
        // Plus potential expansions
        std::vector<std::pair<uint64_t, uint64_t>> expected = {
            {1, 5}, {3, 6}
        };

        if (compare_results(results, expected, "test_bplus_tree_with_mc_operator"))
            return true;
        std::cout << "BPlusTree with MC Operator test passed!" << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_bplus_tree_with_mc_operator: " << e.what() << std::endl;
        return true;
    }
}

// Test 3: BPlusTree with TI Two-Way Operator
bool test_bplus_tree_with_ti_two_way_operator() {
    std::cout << "Testing BPlusTree with TI Two-Way Operator..." << std::endl;

    try {
        VarId x(0), y(1), z(2);

        // Create leftmost operand data: pairs (1,10), (2,20)
        std::vector<std::array<uint64_t, 3>> leftmost_data = {
            {1, 1, 10}, // 1->10
            {1, 2, 20}  // 2->20
        };

        // Create edge operand data: pairs (1,100), (2,200), (1,101)
        std::vector<std::array<uint64_t, 3>> edge_data = {
            {1, 10, 100}, // 1->100
            {1, 20, 200}, // 2->200
            {1, 1, 101}  // 1->101
        };

        // Create BPlusTree iterators and convert to TwoColumnStore
        auto [bplus_tree_leftmost, bplus_leftmost] = create_bplus_two_column_iter(
            "test_ti_leftmost", leftmost_data, ObjectId(1), y, x,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        auto [bplus_tree_edge, bplus_edge] = create_bplus_two_column_iter(
            "test_ti_edge", edge_data, ObjectId(1), x, z,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create TI Two-Way operator
        TITwoWayOperator ti_op(std::move(bplus_leftmost), std::move(bplus_edge), y, z);

        // Collect results
        auto results = collect_two_column_results(ti_op, y, z);

        std::vector<std::pair<uint64_t, uint64_t>> expected = {
            {1, 100}, {2, 200}
        };
        if (compare_results(results, expected, "test_bplus_tree_with_ti_two_way_operator"))
            return true;

        std::cout << "BPlusTree with TI Two-Way Operator test passed! Got "
                  << results.size() << " results." << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_bplus_tree_with_ti_two_way_operator: " << e.what() << std::endl;
        return true;
    }
}

// Test 4: Nested Custom Operators with BPlusTree
bool test_nested_custom_operators_with_bplus_tree() {
    std::cout << "Testing Nested Custom Operators with BPlusTree..." << std::endl;

    try {
        VarId x(0), y(1), z(2);

        // Create base data from B+ tree
        std::vector<std::array<uint64_t, 3>> base_data = {
            {1, 2, 3}, // 2->3
            {1, 3, 4}  // 3->4
        };

        auto [bplus_tree_base, bplus_iter] = create_bplus_two_column_iter(
            "test_nested_base", base_data, ObjectId(1), x, y,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Step 1: Apply KC operator to get transitive closure
        std::unique_ptr<KCOperator> kc_op_ptr = std::make_unique<KCOperator>(std::move(bplus_iter), x, y, false);

        // Step 2: Create additional data for MC operator
        auto additional_store = create_test_store({{4, 5}, {5, 6}});

        auto additional_iter = std::make_unique<TwoColumnStoreBindingIter>(
            std::move(additional_store), y, z);

        // Step 3: Apply MC operator on KC results
        MCOperator mc_op(std::move(kc_op_ptr), std::move(additional_iter), false, false);

        // Collect final results
        auto results = collect_two_column_results(mc_op, x, z);

        std::vector<std::pair<uint64_t, uint64_t>> expected = {
            {3, 5}, {3, 6}, {2, 5}, {2, 6}
        };
        if (compare_results(results, expected, "test_nested_custom_operators_with_bplus_tree"))
            return true;

        std::cout << "Nested Custom Operators test passed! Got "
                  << results.size() << " results." << std::endl;

        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_nested_custom_operators_with_bplus_tree: " << e.what() << std::endl;
        return true;
    }
}

// Test 5: Complex Multi-Level Nesting
bool test_complex_nested_operations() {
    std::cout << "Testing Complex Multi-Level Nested Operations..." << std::endl;

    try {
        VarId source_var(0);
        VarId target_var(1);

        // Create three different B+ trees with different data patterns

        // Tree 1: Linear chain
        std::vector<std::array<uint64_t, 3>> chain_data = {
            {1, 1, 2}, {1, 2, 3}, {1, 3, 4}
        };

        // Tree 2: Star pattern
        std::vector<std::array<uint64_t, 3>> star_data = {
            {1, 5, 6}, {1, 5, 7}, {1, 5, 8}
        };

        // Tree 3: Bridge connections
        std::vector<std::array<uint64_t, 3>> bridge_data = {
            {1, 4, 5}, {1, 8, 9}
        };

        auto [chain_bplus_tree, chain_bplus_ptr] = create_bplus_two_column_iter(
            "test_empty", chain_data, ObjectId(1), source_var, target_var,
            BPlusTreeTwoColumnBindingIter<3>::PSO);
        auto [star_bplus_tree, star_bplus_ptr] = create_bplus_two_column_iter(
            "test_empty", star_data, ObjectId(1), source_var, target_var,
            BPlusTreeTwoColumnBindingIter<3>::PSO);
        auto [bridge_bplus_tree, bridge_bplus_ptr] = create_bplus_two_column_iter(
            "test_empty", bridge_data, ObjectId(1), source_var, target_var,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Level 1: Apply KC to chain (creates transitive closure)
        std::unique_ptr<KCOperator> kc_chain_ptr = 
            std::make_unique<KCOperator>(std::move(chain_bplus_ptr), source_var, target_var, false);

        // Level 2: Apply MC to combine KC result with bridge
        std::unique_ptr<MCOperator> mc_combined_ptr = 
            std::make_unique<MCOperator>(std::move(kc_chain_ptr), std::move(bridge_bplus_ptr), false, false);

        // Level 3: Apply another MC with star pattern
        MCOperator final_mc(std::move(mc_combined_ptr), std::move(star_bplus_ptr), false, false);

        // Collect final results
        auto results = collect_two_column_results(final_mc, source_var, target_var);
        std::vector<std::pair<uint64_t, uint64_t>> expected = {
            {3, 6}, {3, 7}, {3, 8}, {1, 6}, {1, 7}, {1, 8}, {2, 6}, {2, 7}, {2, 8}
        };
        if (compare_results(results, expected, "test_complex_nested_operations"))
            return true;
        std::cout << "Complex nested operations completed with "
                  << results.size() << " final results." << std::endl;

        // The test passes if we get through all levels without crashing
        // and produce some results
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_complex_nested_operations: " << e.what() << std::endl;
        return true;
    }
}

// Test 6: Empty BPlusTree Handling
bool test_empty_bplus_tree_with_operators() {
    std::cout << "Testing Empty BPlusTree with Custom Operators..." << std::endl;

    try {
        VarId source_var(0);
        VarId target_var(1);

        // Create empty B+ tree
        std::vector<std::array<uint64_t, 3>> empty_data = {};

        auto [bplus_tree_empty, bplus_iter] = create_bplus_two_column_iter(
            "test_empty", empty_data, ObjectId(1), source_var, target_var,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Test KC with empty input
        KCOperator kc_op(std::move(bplus_iter), source_var, target_var, false);
        auto results = collect_two_column_results(kc_op, source_var, target_var);

        if (!results.empty()) {
            std::cerr << "Expected empty results from KC on empty input" << std::endl;
            return true;
        }

        std::cout << "Empty BPlusTree with Custom Operators test passed!" << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_empty_bplus_tree_with_operators: " << e.what() << std::endl;
        return true;
    }
}

// Test 7: Large Dataset with Custom Operators
bool test_large_dataset_performance() {
    std::cout << "Testing Large Dataset Performance..." << std::endl;

    try {
        VarId source_var(0);
        VarId target_var(1);

        // Create a larger dataset: chain of 50 nodes
        std::vector<std::array<uint64_t, 3>> large_data;
        for (uint64_t i = 1; i < 50; ++i) {
            large_data.push_back({1, i, i + 1});
        }

        auto [bplus_tree_large, bplus_iter] = create_bplus_two_column_iter(
            "test_large", large_data, ObjectId(1), source_var, target_var,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Apply KC operator (this will create O(n^2) transitive closure)
        KCOperator kc_op(std::move(bplus_iter), source_var, target_var, false);

        auto results = collect_two_column_results(kc_op, source_var, target_var);

        // For a chain of 49 edges, we expect 49 + 48 + 47 + ... + 1 = 49*50/2 = 1225 total edges
        size_t expected_size = 49 * 50 / 2;

        if (results.size() != expected_size) {
            std::cerr << "Expected " << expected_size << " results, got " << results.size() << std::endl;
            return true;
        }

        std::cout << "Large Dataset Performance test passed! Processed "
                  << results.size() << " edges correctly." << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_large_dataset_performance: " << e.what() << std::endl;
        return true;
    }
}

// Test 8: TI Operator with Lazy KC Operators as Edge Operands
bool test_ti_operator_with_lazy_kc_operators() {
    std::cout << "Testing TI Operator with Lazy KC Operators as Edge Operands..." << std::endl;

    try {
        VarId x_var(0);
        VarId y_var(1);
        VarId z_var(2);

        // Create leftmost operand data: simple binding results (x, y) pairs
        // These represent the starting points for the TI operation
        std::vector<std::vector<uint64_t>> leftmost_data = {
            {1, 10},  // x=1, y=10
            {2, 20}   // x=2, y=20
        };

        // Create first edge operand: chain 1->2->3 for lazy KC operator
        std::vector<std::array<uint64_t, 3>> edge1_data = {
            {1, 1, 2}, // 1->2
            {1, 2, 3}  // 2->3
        };

        // Create second edge operand: chain 10->11->12 for lazy KC operator
        std::vector<std::array<uint64_t, 3>> edge2_data = {
            {1, 10, 11}, // 10->11
            {1, 11, 3}  // 11->3
        };

        // Create BPlusTree iterators for edge operands
        auto [bplus_tree_edge1, bplus_iter_edge1] = create_bplus_two_column_iter(
            "test_ti_lazy_edge1", edge1_data, ObjectId(1), x_var, z_var,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        auto [bplus_tree_edge2, bplus_iter_edge2] = create_bplus_two_column_iter(
            "test_ti_lazy_edge2", edge2_data, ObjectId(1), y_var, z_var,
            BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create lazy KC operators as edge operands
        // The key difference: setting lazy=true to defer fix-point computation
        auto lazy_kc_edge1 = std::make_unique<KCOperator>(
            std::move(bplus_iter_edge1), x_var, z_var, false, true);  // lazy=true

        auto lazy_kc_edge2 = std::make_unique<KCOperator>(
            std::move(bplus_iter_edge2), y_var, z_var, false, true);  // lazy=true

        // Create leftmost operand using MockBindingIter
        auto leftmost_operand = std::make_unique<MockBindingIter>(
            leftmost_data, std::vector<VarId>{x_var, y_var});

        // Prepare edge operands vector
        std::vector<std::unique_ptr<TwoColumnBindingIter>> edge_operands;
        edge_operands.push_back(std::move(lazy_kc_edge1));
        edge_operands.push_back(std::move(lazy_kc_edge2));

        // Create TI operator with lazy KC operators as edge operands
        TIOperator ti_op(std::move(leftmost_operand), std::move(edge_operands));

        // Collect results from TI operator
        std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> ti_results;
        Binding binding(10);
        ti_op.begin(binding);

        while (ti_op.next()) {
            uint64_t x_val = binding[x_var].id;
            uint64_t y_val = binding[y_var].id;
            uint64_t z_val = binding[z_var].id;
            ti_results.push_back({x_val, y_val, z_val});
            std::cout << "TI result: x=" << x_val << ", y=" << y_val << ", z=" << z_val << std::endl;
        }

        std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> expected = {
            {1, 10, 3}
        };
        if (compare_results(ti_results, expected, "test_ti_operator_with_lazy_kc_operators"))
            return true;

        // Expected results: TI operator should find intersections
        // For (x=1, y=10): KC1 gives paths from x=1 (1->2, 1->3), KC2 gives paths from y=10 (10->11, 10->3)
        // The intersection would be on z values, intersect on 3
        // For (x=2, y=20): KC1 gives paths from x=2 (2->3), KC2 has no paths from y=20
        // Expected: empty results or specific intersections depending on the data pattern
        std::cout << "TI Operator with Lazy KC Operators test completed with "
                  << ti_results.size() << " results." << std::endl;
        return false;

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_ti_operator_with_lazy_kc_operators: " << e.what() << std::endl;
        return true;
    }
}

int main() {
    // Initialize the storage system
    TestSetup::init();

    std::vector<TestFunction*> tests;

    // Basic integration tests
    tests.push_back(&test_bplus_tree_with_kc_operator);
    tests.push_back(&test_bplus_tree_with_mc_operator);
    tests.push_back(&test_bplus_tree_with_ti_two_way_operator);

    // Nesting tests
    tests.push_back(&test_nested_custom_operators_with_bplus_tree);
    tests.push_back(&test_complex_nested_operations);

    // Edge cases
    tests.push_back(&test_empty_bplus_tree_with_operators);

    // Performance test
    tests.push_back(&test_large_dataset_performance);

    // TI Operator with lazy KC operators test
    tests.push_back(&test_ti_operator_with_lazy_kc_operators);

    auto error = false;

    for (auto& test_func : tests) {
        // Clean up and reinitialize storage between each test for proper isolation
        TestSetup::cleanup();
        TestSetup::init();

        if (test_func()) {
            error = true;
        }
    }

    // Clean up
    TestSetup::cleanup();

    if (!error) {
        std::cout << "\nAll Custom Operators with BPlusTree integration tests passed!" << std::endl;
    } else {
        std::cout << "\nSome Custom Operators with BPlusTree integration tests failed!" << std::endl;
    }

    return error;
}