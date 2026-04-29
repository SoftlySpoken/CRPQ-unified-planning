#include <iostream>
#include <vector>
#include <algorithm>

#include "storage/custom_buffer/two_column_store.h"

using namespace CustomOps;

typedef bool TestFunction();

/**
 * Test basic sort_neighbors functionality
 */
bool test_sort_neighbors_basic() {
    TwoColumnStore store;

    // Add edges with unsorted neighbors
    store.append(1, 5);
    store.append(1, 2);
    store.append(1, 8);
    store.append(1, 1);
    store.append(2, 10);
    store.append(2, 3);

    // Compact to CSR format
    store.compact();

    // Sort neighbors for vertex 1
    store.sort_neighbors(1);

    // Get neighbors and check if sorted
    auto [neighbors, count] = store.get_neighbors(1);
    if (count != 4) {
        std::cerr << "Expected 4 neighbors for vertex 1, got " << count << std::endl;
        return true;
    }

    std::vector<int> expected = {1, 2, 5, 8};
    for (size_t i = 0; i < count; i++) {
        if (neighbors[i] != expected[i]) {
            std::cerr << "Neighbor " << i << ": expected " << expected[i]
                      << ", got " << neighbors[i] << std::endl;
            return true;
        }
    }

    // Check that vertex 2's neighbors are not sorted yet
    auto [neighbors2, count2] = store.get_neighbors(2);
    if (count2 != 2) {
        std::cerr << "Expected 2 neighbors for vertex 2, got " << count2 << std::endl;
        return true;
    }

    // Original order should be [10, 3]
    if (neighbors2[0] != 10 || neighbors2[1] != 3) {
        std::cerr << "Vertex 2 neighbors should be unsorted: [10, 3]" << std::endl;
        return true;
    }

    return false; // No error
}

/**
 * Test sort_neighbors with nonexistent vertex
 */
bool test_sort_neighbors_nonexistent() {
    TwoColumnStore store;

    store.append(1, 5);
    store.append(1, 2);
    store.compact();

    // Should not crash when sorting nonexistent vertex
    store.sort_neighbors(999);

    // Original neighbors should be unchanged
    auto [neighbors, count] = store.get_neighbors(1);
    if (count != 2 || neighbors[0] != 5 || neighbors[1] != 2) {
        std::cerr << "Sorting nonexistent vertex affected existing data" << std::endl;
        return true;
    }

    return false; // No error
}

/**
 * Test sort_neighbors called multiple times (should be idempotent)
 */
bool test_sort_neighbors_idempotent() {
    TwoColumnStore store;

    store.append(1, 9);
    store.append(1, 1);
    store.append(1, 5);
    store.compact();

    // Sort multiple times
    store.sort_neighbors(1);
    store.sort_neighbors(1);
    store.sort_neighbors(1);

    auto [neighbors, count] = store.get_neighbors(1);
    if (count != 3) {
        std::cerr << "Expected 3 neighbors, got " << count << std::endl;
        return true;
    }

    std::vector<int> expected = {1, 5, 9};
    for (size_t i = 0; i < count; i++) {
        if (neighbors[i] != expected[i]) {
            std::cerr << "Multiple sorts failed: neighbor " << i
                      << " expected " << expected[i] << ", got " << neighbors[i] << std::endl;
            return true;
        }
    }

    return false; // No error
}

/**
 * Test sort_neighbors with empty vertex (no neighbors)
 */
bool test_sort_neighbors_empty() {
    TwoColumnStore store;

    store.append(1, 5);
    store.append(2, 10); // vertex 3 will have no neighbors
    store.append(3, 20);
    store.compact();

    // Sort vertex that exists but has neighbors
    store.sort_neighbors(1);
    auto [neighbors1, count1] = store.get_neighbors(1);
    if (count1 != 1 || neighbors1[0] != 5) {
        std::cerr << "Single neighbor vertex failed" << std::endl;
        return true;
    }

    return false; // No error
}

/**
 * Test sort_neighbors with duplicate values
 */
bool test_sort_neighbors_duplicates() {
    TwoColumnStore store;

    store.append(1, 5);
    store.append(1, 3);
    store.append(1, 5); // duplicate
    store.append(1, 1);
    store.append(1, 3); // duplicate
    store.compact();

    store.sort_neighbors(1);

    auto [neighbors, count] = store.get_neighbors(1);
    if (count != 5) {
        std::cerr << "Expected 5 neighbors (including duplicates), got " << count << std::endl;
        return true;
    }

    std::vector<int> expected = {1, 3, 3, 5, 5};
    for (size_t i = 0; i < count; i++) {
        if (neighbors[i] != expected[i]) {
            std::cerr << "Duplicate sorting failed: neighbor " << i
                      << " expected " << expected[i] << ", got " << neighbors[i] << std::endl;
            return true;
        }
    }

    return false; // No error
}

int main() {
    std::vector<TestFunction*> tests;

    tests.push_back(&test_sort_neighbors_basic);
    tests.push_back(&test_sort_neighbors_nonexistent);
    tests.push_back(&test_sort_neighbors_idempotent);
    tests.push_back(&test_sort_neighbors_empty);
    tests.push_back(&test_sort_neighbors_duplicates);

    auto error = false;

    for (auto& test_func : tests) {
        if (test_func()) {
            error = true;
        }
    }

    if (!error) {
        std::cout << "All two_column_store tests passed!" << std::endl;
    }

    return error;
}