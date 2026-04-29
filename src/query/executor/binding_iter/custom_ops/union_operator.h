#pragma once

#include "query/executor/binding_iter/custom_ops/custom_operator_base.h"
#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include <memory>
#include <vector>
#include <unordered_set>

namespace CustomOps {

// Union Operator
// This operator combines results from multiple children
class UnionOperator : public TwoColumnStoreBindingIter {
private:
    std::vector<std::unique_ptr<TwoColumnBindingIter>> children;

    // Union configuration
    struct UnionConfig {
        bool eliminate_duplicates;
        bool maintain_order;
        bool use_hash_based_dedup;
    } union_config;


    // Duplicate detection (if needed)
    struct DuplicateDetector {
        // Custom hash function for pairs to ensure good distribution
        struct PairHash {
            std::size_t operator()(const std::pair<uint64_t, uint64_t>& p) const {
                std::hash<uint64_t> hasher;
                std::size_t h1 = hasher(p.first);
                std::size_t h2 = hasher(p.second);
                // Use boost-style hash_combine
                return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            }
        };

        std::unordered_set<std::pair<uint64_t, uint64_t>, PairHash> seen_pairs;
        bool check_and_add(uint64_t id1, uint64_t id2);
    } dup_detector;

    // Lazy computation flag
    // bool lazy;

    // Tracking for lazy computation
    mutable std::unordered_set<uint64_t> computed_vertices;
    mutable bool global_computation_done;


protected:
    void _begin(Binding& parent_binding) override;

public:
    UnionOperator(std::vector<std::unique_ptr<TwoColumnBindingIter>> children,
                VarId col1_var,
                VarId col2_var,
                  bool eliminate_duplicates = true,
                  bool _lazy = false);
    UnionOperator(std::vector<std::unique_ptr<TwoColumnBindingIter>> children,
                ObjectId constant_col1_value,
                VarId col2_var,
                  bool eliminate_duplicates = true,
                  bool _lazy = false);
    UnionOperator(std::vector<std::unique_ptr<TwoColumnBindingIter>> children,
        VarId col1_var,
                ObjectId constant_col2_value,
                  bool eliminate_duplicates = true,
                  bool _lazy = false);

    ~UnionOperator() = default;

    void assign_nulls() override;
    void accept_visitor(BindingIterVisitor& visitor) override;

    CustomOps::CustomOperatorBase::OperatorType get_operator_type() const {
        return CustomOps::CustomOperatorBase::OperatorType::UNION;
    }

    // Union-specific methods
    void set_duplicate_elimination(bool enable);
    void add_child(std::unique_ptr<TwoColumnBindingIter> child);

    // Processing modes
    enum class UnionMode {
        UNION_ALL,      // No duplicate elimination
        UNION_DISTINCT, // With duplicate elimination
        UNION_ORDERED   // Maintain order from children
    };

    void set_union_mode(UnionMode mode);

    // Override get_neighbors for lazy computation
    std::pair<const uint64_t*, size_t> get_neighbors(uint64_t vid) const override;

    // Lazy computation control
    // void set_lazy(bool _lazy) { lazy = _lazy; }
    // bool is_lazy() const { return lazy; }

    // Statistics
    size_t get_num_children() const { return children.size(); }
    size_t get_duplicates_eliminated() const;

    // Additional getter methods for detailed printing
    bool is_eliminate_duplicates() const { return union_config.eliminate_duplicates; }
    bool is_maintain_order() const { return union_config.maintain_order; }
    bool is_use_hash_based_dedup() const { return union_config.use_hash_based_dedup; }
    TwoColumnBindingIter* get_child(size_t index) {
        return index < children.size() ? children[index].get() : nullptr;
    }

    bool seek_to_vertex(uint64_t vertex) override;

private:
    // Helper methods for lazy computation
    void compute_union_for_vertex(uint64_t vertex) const;
    void compute_full_union() const;
};

} // namespace CustomOps