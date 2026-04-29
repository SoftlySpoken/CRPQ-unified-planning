#include <iostream>
#include <vector>
#include <algorithm>
#include <memory>
#include <filesystem>

#include "query/executor/binding_iter/bplus_tree_two_column_binding_iter.h"
#include "storage/index/bplus_tree/bplus_tree.h"
#include "storage/index/record.h"
#include "storage/file_manager.h"
#include "storage/buffer_manager.h"
#include "query/executor/binding.h"
#include "graph_models/object_id.h"
#include "query/var_id.h"
#include "query/query_context.h"

typedef bool TestFunction();

// Test setup and cleanup
class TestSetup {
private:
    static QueryContext query_ctx;

public:
    static void init() {
        // Clean up any previous test data
        cleanup();

        // Initialize the storage system
        FileManager::init("test_db");
        BufferManager::init(40960, 40960, 40960, 1); // Small buffers for testing

        // Set up QueryContext so BufferManager can access it
        QueryContext::set_query_ctx(&query_ctx);
    }

    static void cleanup() {
        // Clean up test files
        std::filesystem::remove_all("test_db");
    }
};

// Define the static member
QueryContext TestSetup::query_ctx;

/**
 * Test basic PSO index iteration with valid data
 */
bool test_pso_basic_iteration() {
    try {
        // Create a real B+ tree for testing
        auto btree = std::make_unique<BPlusTree<3>>("test_pso");

        // Add test data: (predicate=1, subject=10, object=20), (predicate=1, subject=30, object=40)
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 10; // subject
        record1[2] = 20; // object
        btree->insert(record1);

        Record<3> record2;
        record2[0] = 1; // predicate
        record2[1] = 30; // subject
        record2[2] = 40; // object
        btree->insert(record2);

        // Different predicate - should not be returned
        Record<3> record3;
        record3[0] = 2; // different predicate
        record3[1] = 50; // subject
        record3[2] = 60; // object
        btree->insert(record3);

        // Create iterator for predicate=1, PSO index
        ObjectId predicate(1);
        VarId subject_var(0);
        VarId object_var(1);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, object_var,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(2);
        iter.begin(binding);

        // Collect results
        std::vector<std::pair<uint64_t, uint64_t>> results;
        while (iter.next()) {
            ObjectId subject = binding[subject_var];
            ObjectId object = binding[object_var];
            results.push_back({subject.id, object.id});
        }

        // Verify results - should have 2 entries for predicate=1
        if (results.size() != 2) {
            std::cerr << "Expected 2 results, got " << results.size() << std::endl;
            return true;
        }

        // Sort results for consistent comparison
        std::sort(results.begin(), results.end());

        if (results[0].first != 10 || results[0].second != 20) {
            std::cerr << "First result incorrect: expected (10,20), got ("
                      << results[0].first << "," << results[0].second << ")" << std::endl;
            return true;
        }

        if (results[1].first != 30 || results[1].second != 40) {
            std::cerr << "Second result incorrect: expected (30,40), got ("
                      << results[1].first << "," << results[1].second << ")" << std::endl;
            return true;
        }

        std::cerr << "test_pso_basic_iteration passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_pso_basic_iteration: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test POS index iteration with valid data
 */
bool test_pos_basic_iteration() {
    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_pos");

        // Add test data: (predicate=1, object=20, subject=10), (predicate=1, object=40, subject=30)
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 20; // object
        record1[2] = 10; // subject
        btree->insert(record1);

        Record<3> record2;
        record2[0] = 1; // predicate
        record2[1] = 40; // object
        record2[2] = 30; // subject
        btree->insert(record2);

        // Create iterator for predicate=1, POS index
        ObjectId predicate(1);
        VarId object_var(0);   // col1_var = object in POS
        VarId subject_var(1);  // col2_var = subject in POS

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, object_var, subject_var,
                                              BPlusTreeTwoColumnBindingIter<3>::POS);

        // Create binding context
        Binding binding(2);
        iter.begin(binding);

        // Collect results
        std::vector<std::pair<uint64_t, uint64_t>> results;
        while (iter.next()) {
            ObjectId object = binding[object_var];
            ObjectId subject = binding[subject_var];
            results.push_back({object.id, subject.id});
        }

        // Verify results
        if (results.size() != 2) {
            std::cerr << "Expected 2 results, got " << results.size() << std::endl;
                return true;
        }

        // Sort results for consistent comparison
        std::sort(results.begin(), results.end());

        if (results[0].first != 20 || results[0].second != 10) {
            std::cerr << "First result incorrect: expected (20,10), got ("
                      << results[0].first << "," << results[0].second << ")" << std::endl;
                return true;
        }

        if (results[1].first != 40 || results[1].second != 30) {
            std::cerr << "Second result incorrect: expected (40,30), got ("
                      << results[1].first << "," << results[1].second << ")" << std::endl;
            return true;
        }

        std::cerr << "test_pos_basic_iteration passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_pos_basic_iteration: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test iteration with nonexistent predicate
 */
bool test_nonexistent_predicate() {
    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_nonexistent");

        // Add some data with predicate=1
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 10; // subject
        record1[2] = 20; // object
        btree->insert(record1);

        // Create iterator for nonexistent predicate=999
        ObjectId predicate(999);
        VarId subject_var(0);
        VarId object_var(1);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, object_var,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(2);
        iter.begin(binding);

        // Should have no results
        int count = 0;
        while (iter.next()) {
            count++;
        }

        if (count != 0) {
            std::cerr << "Expected 0 results for nonexistent predicate, got " << count << std::endl;
                return true;
        }

        std::cerr << "test_nonexistent_predicate passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_nonexistent_predicate: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test reset functionality
 */
bool test_reset_functionality() {
    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_reset");

        // Add test data
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 10; // subject
        record1[2] = 20; // object
        btree->insert(record1);

        Record<3> record2;
        record2[0] = 1; // predicate
        record2[1] = 30; // subject
        record2[2] = 40; // object
        btree->insert(record2);

        // Create iterator
        ObjectId predicate(1);
        VarId subject_var(0);
        VarId object_var(1);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, object_var,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(2);
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

        std::cerr << "test_reset_functionality passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_reset_functionality: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test with empty B+ tree
 */
bool test_empty_btree() {
    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_empty");

        // Create iterator on empty tree
        ObjectId predicate(1);
        VarId subject_var(0);
        VarId object_var(1);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, object_var,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(2);
        iter.begin(binding);

        // Should have no results
        int count = 0;
        while (iter.next()) {
            count++;
        }

        if (count != 0) {
            std::cerr << "Expected 0 results for empty tree, got " << count << std::endl;
                return true;
        }

        std::cerr << "test_empty_btree passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_empty_btree: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test assign_nulls functionality
 */
bool test_assign_nulls() {
    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_nulls");

        // Create iterator
        ObjectId predicate(1);
        VarId subject_var(0);
        VarId object_var(1);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, object_var,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context with some initial values
        Binding binding(2);
        binding[subject_var] = ObjectId(123);
        binding[object_var] = ObjectId(456);

        // Call assign_nulls
        iter.assign_nulls();

        // Verify both variables are set to null
        if (!binding[subject_var].is_null() || !binding[object_var].is_null()) {
            std::cerr << "assign_nulls failed to set variables to null" << std::endl;
                return true;
        }

        std::cerr << "test_assign_nulls passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_assign_nulls: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test with duplicate entries
 */
bool test_duplicate_entries() {
    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_duplicates");

        // Add duplicate entries
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 10; // subject
        record1[2] = 20; // object
        btree->insert(record1);

        // Insert same record again (if B+ tree allows duplicates)
        btree->insert(record1);

        Record<3> record2;
        record2[0] = 1; // predicate
        record2[1] = 30; // subject
        record2[2] = 40; // object
        btree->insert(record2);

        // Create iterator
        ObjectId predicate(1);
        VarId subject_var(0);
        VarId object_var(1);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, subject_var, object_var,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(2);
        iter.begin(binding);

        // Collect results
        std::vector<std::pair<uint64_t, uint64_t>> results;
        while (iter.next()) {
            ObjectId subject = binding[subject_var];
            ObjectId object = binding[object_var];
            results.push_back({subject.id, object.id});
        }

        // Should handle duplicates appropriately (depends on B+ tree implementation)
        if (results.size() != 2) {
            std::cerr << "Expected 2 results, got " << results.size() << std::endl;
                return true;
        }

        std::cerr << "test_duplicate_entries passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_duplicate_entries: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test PSO index with constant col1 (subject) value
 */
bool test_pso_constant_subject() {
    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_pso_constant_subject");

        // Add test data with different subjects for same predicate
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 10; // subject
        record1[2] = 20; // object
        btree->insert(record1);

        Record<3> record2;
        record2[0] = 1; // predicate
        record2[1] = 10; // same subject
        record2[2] = 30; // different object
        btree->insert(record2);

        Record<3> record3;
        record3[0] = 1; // predicate
        record3[1] = 50; // different subject - should not be returned
        record3[2] = 60; // object
        btree->insert(record3);

        // Create iterator with constant subject=10
        ObjectId predicate(1);
        ObjectId constant_subject(10);
        VarId object_var(0); // Only object is variable

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, constant_subject, object_var,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context (only need space for object variable)
        Binding binding(1);
        iter.begin(binding);

        // Collect results
        std::vector<uint64_t> objects;
        while (iter.next()) {
            ObjectId object = binding[object_var];
            objects.push_back(object.id);
        }

        // Should only get objects for subject=10
        if (objects.size() != 2) {
            std::cerr << "Expected 2 results for constant subject, got " << objects.size() << std::endl;
            return true;
        }

        // Sort results for consistent comparison
        std::sort(objects.begin(), objects.end());

        if (objects[0] != 20 || objects[1] != 30) {
            std::cerr << "Constant subject test failed: expected objects (20,30), got ("
                      << objects[0] << "," << objects[1] << ")" << std::endl;
            return true;
        }

        std::cerr << "test_pso_constant_subject passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_pso_constant_subject: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test POS index with constant col1 (object) value
 */
bool test_pos_constant_object() {
    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_pos_constant_object");

        // Add test data with different objects for same predicate
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 20; // object
        record1[2] = 10; // subject
        btree->insert(record1);

        Record<3> record2;
        record2[0] = 1; // predicate
        record2[1] = 20; // same object
        record2[2] = 30; // different subject
        btree->insert(record2);

        Record<3> record3;
        record3[0] = 1; // predicate
        record3[1] = 60; // different object - should not be returned
        record3[2] = 50; // subject
        btree->insert(record3);

        // Create iterator with constant object=20
        ObjectId predicate(1);
        ObjectId constant_object(20);
        VarId subject_var(0); // Only subject is variable

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, constant_object, subject_var,
                                              BPlusTreeTwoColumnBindingIter<3>::POS);

        // Create binding context (only need space for subject variable)
        Binding binding(1);
        iter.begin(binding);

        // Collect results
        std::vector<uint64_t> subjects;
        while (iter.next()) {
            ObjectId subject = binding[subject_var];
            subjects.push_back(subject.id);
        }

        // Should only get subjects for object=20
        if (subjects.size() != 2) {
            std::cerr << "Expected 2 results for constant object, got " << subjects.size() << std::endl;
            return true;
        }

        // Sort results for consistent comparison
        std::sort(subjects.begin(), subjects.end());

        if (subjects[0] != 10 || subjects[1] != 30) {
            std::cerr << "Constant object test failed: expected subjects (10,30), got ("
                      << subjects[0] << "," << subjects[1] << ")" << std::endl;
            return true;
        }

        std::cerr << "test_pos_constant_object passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_pos_constant_object: " << e.what() << std::endl;
        return true;
    }
}

/**
 * Test constant col1 with nonexistent value
 */
bool test_constant_col1_nonexistent() {
    try {
        auto btree = std::make_unique<BPlusTree<3>>("test_constant_nonexistent");

        // Add test data
        Record<3> record1;
        record1[0] = 1; // predicate
        record1[1] = 10; // subject
        record1[2] = 20; // object
        btree->insert(record1);

        // Create iterator with nonexistent constant subject=999
        ObjectId predicate(1);
        ObjectId constant_subject(999);
        VarId object_var(0);

        BPlusTreeTwoColumnBindingIter<3> iter(*btree, predicate, constant_subject, object_var,
                                              BPlusTreeTwoColumnBindingIter<3>::PSO);

        // Create binding context
        Binding binding(1);
        iter.begin(binding);

        // Should have no results
        int count = 0;
        while (iter.next()) {
            count++;
        }

        if (count != 0) {
            std::cerr << "Expected 0 results for nonexistent constant col1, got " << count << std::endl;
            return true;
        }

        std::cerr << "test_constant_col1_nonexistent passed" << std::endl;
        return false; // No error

    } catch (const std::exception& e) {
        std::cerr << "Exception in test_constant_col1_nonexistent: " << e.what() << std::endl;
        return true;
    }
}

int main() {
    // Initialize the storage system
    TestSetup::init();

    std::vector<TestFunction*> tests;

    tests.push_back(&test_pso_basic_iteration);
    tests.push_back(&test_pos_basic_iteration);
    tests.push_back(&test_nonexistent_predicate);
    tests.push_back(&test_reset_functionality);
    tests.push_back(&test_empty_btree);
    tests.push_back(&test_assign_nulls);
    tests.push_back(&test_duplicate_entries);
    tests.push_back(&test_pso_constant_subject);
    tests.push_back(&test_pos_constant_object);
    tests.push_back(&test_constant_col1_nonexistent);

    auto error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
    }

    // Clean up
    TestSetup::cleanup();

    if (!error) {
        std::cout << "All bplus_tree_two_column_binding_iter tests passed!" << std::endl;
    }

    return error;
}