#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <unordered_set>
#include <bitset>
#include <filesystem>

#include "query/optimizer/custom_model/custom_planner.h"
#include "query/parser/op/sparql/op_basic_graph_pattern.h"
#include "query/parser/op/sparql/op_triple.h"
#include "query/parser/op/sparql/op_path.h"
#include "query/parser/paths/path_atom.h"
#include "query/parser/paths/path_sequence.h"
#include "query/parser/paths/path_alternatives.h"
#include "query/parser/paths/path_kleene_plus.h"
#include "query/parser/paths/path_kleene_star.h"
#include "query/optimizer/plan/plan.h"
#include "query/id.h"
#include "query/var_id.h"
#include "graph_models/object_id.h"
#include "graph_models/rdf_model/conversions.h"

// BPlus tree and storage infrastructure for mocking
#include "storage/index/bplus_tree/bplus_tree.h"
#include "storage/file_manager.h"
#include "storage/buffer_manager.h"
#include "query/query_context.h"
#include "graph_models/rdf_model/rdf_model.h"

using namespace SPARQL;

typedef bool TestFunction();

// Test setup and cleanup for storage system and rdf_model mocking
class TestSetup {
private:
    static QueryContext query_ctx;

public:
    static void init() {
        // Clean up any previous test data
        cleanup();

        // Initialize the storage system
        FileManager::init("test_custom_planner_db");
        BufferManager::init(40960, 40960, 40960, 1); // Small buffers for testing

        // Set up QueryContext so BufferManager can access it
        QueryContext::set_query_ctx(&query_ctx);

        // Initialize mock BPlus trees for rdf_model
        // We need to mock rdf_model.pso and rdf_model.pos to prevent segfaults
        static int counter = 0;

        // Create empty BPlus trees with unique names
        if (!rdf_model.pso) {
            rdf_model.pso = std::make_unique<BPlusTree<3>>("test_pso_" + std::to_string(++counter));
        }
        if (!rdf_model.pos) {
            rdf_model.pos = std::make_unique<BPlusTree<3>>("test_pos_" + std::to_string(++counter));
        }
    }

    static void cleanup() {
        // Clean up test files
        std::filesystem::remove_all("test_custom_planner_db");

        // Reset BPlus trees
        rdf_model.pso.reset();
        rdf_model.pos.reset();
    }
};

// Define the static member
QueryContext TestSetup::query_ctx;

// Mock objects for testing
ObjectId create_mock_oid(uint64_t id) {
    return ObjectId(id);
}

// Helper function to create test OpBasicGraphPattern
OpBasicGraphPattern create_test_pattern(
    const std::vector<OpTriple>& triples = {},
    const std::vector<OpPath>& paths = {}
) {
    return OpBasicGraphPattern(triples, paths);
}

// Test helper functions
bool test_init_next_var() {
    std::cout << "Testing init_next_var functionality..." << std::endl;

    // Create test triples with various variable IDs
    std::vector<OpTriple> triples = {
        OpTriple(VarId(1), create_mock_oid(100), VarId(2)),    // ?x1 predicate ?x2
        OpTriple(VarId(2), create_mock_oid(101), VarId(3)),    // ?x5 predicate ?x3
        OpTriple(VarId(1), create_mock_oid(102), VarId(7))     // ?x0 predicate ?x7
    };

    auto pattern = create_test_pattern(triples);

    // Test that get_plan initializes next_var correctly
    auto plan = CustomPlanner::get_plan(pattern);

    // The test passes if no exception is thrown and plan is created
    if (!plan) {
        std::cerr << "Failed to create plan from test pattern" << std::endl;
        return true;
    }

    std::cout << "init_next_var test passed!" << std::endl;
    return false;
}

bool test_extract_vars_from_pattern() {
    std::cout << "Testing extract_vars_from_pattern functionality..." << std::endl;

    std::vector<OpTriple> triples = {
        OpTriple(VarId(1), create_mock_oid(100), VarId(2)),
        OpTriple(VarId(2), create_mock_oid(101), VarId(3))
    };

    std::vector<OpPath> paths; // Empty for this test

    std::bitset<BITSET_SIZE> pattern;
    pattern.set(0); // Include first triple
    pattern.set(1); // Include second triple

    std::unordered_set<VarId> vars;
    CustomPlanner::extract_vars_from_pattern(vars, pattern, triples, paths);

    // Expected variables: 1, 2, 3
    std::unordered_set<VarId> expected_vars = {VarId(1), VarId(2), VarId(3)};

    if (vars.size() != expected_vars.size()) {
        std::cerr << "Expected " << expected_vars.size() << " variables, got " << vars.size() << std::endl;
        return true;
    }

    for (const auto& var : expected_vars) {
        if (vars.find(var) == vars.end()) {
            std::cerr << "Missing expected variable: " << var.id << std::endl;
            return true;
        }
    }

    std::cout << "extract_vars_from_pattern test passed!" << std::endl;
    return false;
}

bool test_has_common_elements() {
    std::cout << "Testing has_common_elements functionality..." << std::endl;

    std::unordered_set<VarId> set1 = {VarId(1), VarId(2), VarId(3)};
    std::unordered_set<VarId> set2 = {VarId(3), VarId(4), VarId(5)};
    std::unordered_set<VarId> set3 = {VarId(6), VarId(7), VarId(8)};

    // Test with common elements
    if (!CustomPlanner::has_common_elements(set1, set2)) {
        std::cerr << "Expected common elements between set1 and set2" << std::endl;
        return true;
    }

    // Test without common elements
    if (CustomPlanner::has_common_elements(set1, set3)) {
        std::cerr << "Unexpected common elements between set1 and set3" << std::endl;
        return true;
    }

    // Test with empty sets
    std::unordered_set<VarId> empty_set;
    if (CustomPlanner::has_common_elements(set1, empty_set)) {
        std::cerr << "Unexpected common elements with empty set" << std::endl;
        return true;
    }

    std::cout << "has_common_elements test passed!" << std::endl;
    return false;
}

bool test_next_combination() {
    std::cout << "Testing next_combination functionality..." << std::endl;

    // Test basic combination generation: C(4,2) = {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}
    std::vector<size_t> indices = {0, 1};
    size_t k = 2, n = 4;

    std::vector<std::vector<size_t>> expected_combinations = {
        {0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}
    };

    std::vector<std::vector<size_t>> actual_combinations;

    // Add initial combination
    actual_combinations.push_back(indices);

    // Generate remaining combinations
    while (CustomPlanner::next_combination(indices, k, n)) {
        actual_combinations.push_back(indices);
    }

    if (actual_combinations.size() != expected_combinations.size()) {
        std::cerr << "Expected " << expected_combinations.size() << " combinations, got "
                  << actual_combinations.size() << std::endl;
        return true;
    }

    for (size_t i = 0; i < expected_combinations.size(); ++i) {
        if (actual_combinations[i] != expected_combinations[i]) {
            std::cerr << "Combination mismatch at position " << i << std::endl;
            return true;
        }
    }

    std::cout << "next_combination test passed!" << std::endl;
    return false;
}

bool test_basic_triple_pattern_planning() {
    std::cout << "Testing basic triple pattern planning..." << std::endl;

    try {
        // Create a simple triple pattern: ?x predicate ?y
        std::vector<OpTriple> triples = {
            OpTriple(VarId(1), create_mock_oid(100), VarId(2))
        };

        auto pattern = create_test_pattern(triples);
        auto plan = CustomPlanner::get_plan(pattern);

        if (!plan) {
            std::cerr << "Failed to create plan for basic triple pattern" << std::endl;
            return true;
        }

        // Verify plan properties
        auto vars = plan->get_vars();
        if (vars.find(VarId(1)) == vars.end() || vars.find(VarId(2)) == vars.end()) {
            std::cerr << "Plan missing expected variables" << std::endl;
            return true;
        }

        std::cout << "Basic triple pattern planning test passed!" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception during basic triple pattern planning: " << e.what() << std::endl;
        return true;
    }
}

bool test_multiple_triple_pattern_planning() {
    std::cout << "Testing multiple triple pattern planning..." << std::endl;

    try {
        // Create multiple triple patterns with shared variables
        std::vector<OpTriple> triples = {
            OpTriple(VarId(1), create_mock_oid(100), VarId(2)),  // ?x1 p1 ?x2
            OpTriple(VarId(2), create_mock_oid(101), VarId(3))   // ?x2 p2 ?x3 (shares x2)
        };

        auto pattern = create_test_pattern(triples);
        auto plan = CustomPlanner::get_plan(pattern);

        if (!plan) {
            std::cerr << "Failed to create plan for multiple triple patterns" << std::endl;
            return true;
        }

        // Verify all variables are included
        auto vars = plan->get_vars();
        if (vars.size() < 3) {
            std::cerr << "Plan missing expected variables, got " << vars.size() << " expected at least 3" << std::endl;
            return true;
        }

        std::cout << "Multiple triple pattern planning test passed!" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception during multiple triple pattern planning: " << e.what() << std::endl;
        return true;
    }
}

bool test_pattern_with_constants() {
    std::cout << "Testing pattern with constants..." << std::endl;

    try {
        // Create patterns with constants
        std::vector<OpTriple> triples = {
            OpTriple(create_mock_oid(10), create_mock_oid(100), VarId(1)),  // constant subject
            OpTriple(VarId(1), create_mock_oid(101), create_mock_oid(20))   // constant object
        };

        auto pattern = create_test_pattern(triples);
        auto plan = CustomPlanner::get_plan(pattern);

        if (!plan) {
            std::cerr << "Failed to create plan for pattern with constants" << std::endl;
            return true;
        }

        // Verify that the variable is included
        auto vars = plan->get_vars();
        if (vars.find(VarId(1)) == vars.end()) {
            std::cerr << "Plan missing expected variable" << std::endl;
            return true;
        }

        std::cout << "Pattern with constants test passed!" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception during pattern with constants planning: " << e.what() << std::endl;
        return true;
    }
}

bool test_large_pattern_handling() {
    std::cout << "Testing large pattern handling..." << std::endl;

    try {
        // Create a large number of triple patterns (but within BITSET_SIZE limit)
        std::vector<OpTriple> triples;
        const size_t num_triples = std::min(size_t(15), BITSET_SIZE - 1);

        for (size_t i = 0; i < num_triples; ++i) {
            triples.emplace_back(
                VarId(i),
                create_mock_oid(100 + i),
                VarId(i + 1)
            );
        }

        auto pattern = create_test_pattern(triples);
        auto plan = CustomPlanner::get_plan(pattern);

        if (!plan) {
            std::cerr << "Failed to create plan for large pattern" << std::endl;
            return true;
        }

        // Verify we have the expected number of variables
        auto vars = plan->get_vars();
        if (vars.size() < num_triples) {
            std::cerr << "Plan missing expected variables for large pattern" << std::endl;
            return true;
        }

        std::cout << "Large pattern handling test passed!" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception during large pattern handling: " << e.what() << std::endl;
        return true;
    }
}

bool test_cost_estimation() {
    std::cout << "Testing cost estimation..." << std::endl;

    try {
        // Create simple pattern
        std::vector<OpTriple> triples = {
            OpTriple(VarId(1), create_mock_oid(100), VarId(2))
        };

        auto pattern = create_test_pattern(triples);
        auto plan = CustomPlanner::get_plan(pattern);

        if (!plan) {
            std::cerr << "Failed to create plan for cost estimation test" << std::endl;
            return true;
        }

        // Verify cost estimation returns a reasonable value
        double cost = plan->estimate_cost();
        if (cost < 0 || cost == MAX_COST) {
            std::cerr << "Unexpected cost estimation: " << cost << std::endl;
            return true;
        }

        std::cout << "Cost estimation test passed!" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception during cost estimation: " << e.what() << std::endl;
        return true;
    }
}

bool test_edge_case_var_ids() {
    std::cout << "Testing edge case variable IDs..." << std::endl;

    try {
        // Test with edge case variable IDs
        std::vector<OpTriple> triples = {
            OpTriple(VarId(0), create_mock_oid(100), VarId(UINT32_MAX - 1)),
            OpTriple(VarId(UINT32_MAX - 1), create_mock_oid(101), VarId(1))
        };

        auto pattern = create_test_pattern(triples);
        auto plan = CustomPlanner::get_plan(pattern);

        if (!plan) {
            std::cerr << "Failed to create plan for edge case variable IDs" << std::endl;
            return true;
        }

        std::cout << "Edge case variable IDs test passed!" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception during edge case variable IDs test: " << e.what() << std::endl;
        return true;
    }
}

bool test_pattern_validation() {
    std::cout << "Testing pattern validation..." << std::endl;

    try {
        // Test pattern that should trigger validation logic
        std::vector<OpTriple> triples = {
            OpTriple(create_mock_oid(1), create_mock_oid(100), create_mock_oid(2))  // All constants
        };

        auto pattern = create_test_pattern(triples);

        // This should either succeed or throw an expected exception
        try {
            auto plan = CustomPlanner::get_plan(pattern);
            // If it succeeds, that's also valid behavior
            std::cout << "Pattern validation test passed (plan created)!" << std::endl;
            return false;
        } catch (const std::logic_error& e) {
            // Expected exception for all-constant patterns
            std::cout << "Pattern validation test passed (expected exception: " << e.what() << ")!" << std::endl;
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Unexpected exception during pattern validation: " << e.what() << std::endl;
        return true;
    }
}

bool test_plan_cloning() {
    std::cout << "Testing plan cloning functionality..." << std::endl;

    try {
        std::vector<OpTriple> triples = {
            OpTriple(VarId(1), create_mock_oid(100), VarId(2))
        };

        auto pattern = create_test_pattern(triples);
        auto original_plan = CustomPlanner::get_plan(pattern);

        if (!original_plan) {
            std::cerr << "Failed to create original plan for cloning test" << std::endl;
            return true;
        }

        // Test plan cloning
        auto cloned_plan = original_plan->clone();

        if (!cloned_plan) {
            std::cerr << "Failed to clone plan" << std::endl;
            return true;
        }

        // Verify cloned plan has same cost
        if (original_plan->estimate_cost() != cloned_plan->estimate_cost()) {
            std::cerr << "Cloned plan has different cost than original" << std::endl;
            return true;
        }

        std::cout << "Plan cloning test passed!" << std::endl;
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Exception during plan cloning test: " << e.what() << std::endl;
        return true;
    }
}

int main() {
    // Initialize the storage system and mock rdf_model
    TestSetup::init();

    std::vector<TestFunction*> tests;

    // Add helper function tests
    tests.push_back(&test_extract_vars_from_pattern);
    tests.push_back(&test_has_common_elements);
    tests.push_back(&test_next_combination);

    // Add core functionality tests
    tests.push_back(&test_init_next_var);
    tests.push_back(&test_basic_triple_pattern_planning);
    tests.push_back(&test_multiple_triple_pattern_planning);
    tests.push_back(&test_pattern_with_constants);
    tests.push_back(&test_large_pattern_handling);
    tests.push_back(&test_cost_estimation);

    // Add edge case tests
    tests.push_back(&test_edge_case_var_ids);
    tests.push_back(&test_pattern_validation);
    tests.push_back(&test_plan_cloning);

    bool error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
        std::cout << std::endl;
    }

    // Clean up
    TestSetup::cleanup();

    if (!error) {
        std::cout << "All CustomPlanner tests passed!" << std::endl;
    } else {
        std::cout << "Some CustomPlanner tests failed!" << std::endl;
    }

    return error;
}