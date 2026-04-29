#pragma once

#include "query/executor/binding_iter/two_column_binding_iter.h"
#include "query/executor/binding_iter/two_column_store_binding_iter.h"
#include "storage/custom_buffer/two_column_store.h"
#include "query/executor/binding.h"
#include <memory>
#include <vector>
#include <unordered_set>

// Forward declaration to avoid circular includes
class BindingIterVisitor;

namespace CustomOps {

// MC (Merge Closure) Operator
// MC has two child TwoColumnBindingIter A and B, and itself is TwoColumnStoreBindingIter.
// MC first computes A/B (concatenation) similar to TI operator.
// Then it recursively expands on the left A's until no new results are produced, to get A^+/B.
// Finally, it recursively expands on the right B's to get A^+/B^+.
class MCOperator : public TwoColumnStoreBindingIter {
private:
    // Two child operands: A and B (two-column results)
    std::unique_ptr<TwoColumnBindingIter> child_A;
    std::unique_ptr<TwoColumnBindingIter> child_B;

    // Separate bindings for children
    std::unique_ptr<Binding> binding_A;
    std::unique_ptr<Binding> binding_B;

    // Current processing phase
    enum class ProcessingPhase {
        INITIAL_CONCATENATION,  // Computing A/B
        LEFT_EXPANSION,         // Computing A^+/B (expanding left A's)
        RIGHT_EXPANSION         // Computing A^+/B^+ (expanding right B's)
    };
    ProcessingPhase current_phase;

    // Results storage for different phases
    std::unique_ptr<TwoColumnStore> initial_results;     // A/B results
    std::unique_ptr<TwoColumnStore> left_expanded;       // A^+/B results
    std::unique_ptr<TwoColumnStore> final_results;       // A^+/B^+ results

    // Custom hash for std::pair<uint64_t, uint64_t>
    struct PairHash {
        std::size_t operator()(const std::pair<uint64_t, uint64_t>& p) const {
            return std::hash<uint64_t>()(p.first) ^ (std::hash<uint64_t>()(p.second) << 1);
        }
    };

    // Expansion tracking
    std::unordered_set<std::pair<uint64_t, uint64_t>, PairHash> seen_pairs;
    bool expansion_changed;

    bool left_epsilon;
    bool right_epsilon;

protected:
    void _begin(Binding& parent_binding) override;
    void _reset() override;

public:
    // Constructor takes two TwoColumnBindingIter children
    MCOperator(std::unique_ptr<TwoColumnBindingIter> child_A,
               std::unique_ptr<TwoColumnBindingIter> child_B,
               bool _left_epsilon,
               bool _right_epsilon);

    ~MCOperator() = default;

    void accept_visitor(BindingIterVisitor& visitor) override;

    // MC-specific methods

    // Phase 1: Compute initial concatenation A/B
    void compute_initial_concatenation();

    // Phase 2: Expand left A's recursively to get A^+/B
    void expand_left_recursively();

    // Phase 3: Expand right B's recursively to get A^+/B^+
    void expand_right_recursively();

    // Helper to check if a pair is already seen
    bool is_pair_seen(uint64_t first, uint64_t second);

    // Statistics
    size_t get_current_phase_size() const;

    // Additional getter methods for detailed printing
    ProcessingPhase get_current_phase() const { return current_phase; }
    const char* get_phase_name() const {
        switch (current_phase) {
            case ProcessingPhase::INITIAL_CONCATENATION: return "INITIAL_CONCATENATION";
            case ProcessingPhase::LEFT_EXPANSION: return "LEFT_EXPANSION";
            case ProcessingPhase::RIGHT_EXPANSION: return "RIGHT_EXPANSION";
            default: return "UNKNOWN";
        }
    }
    size_t get_seen_pairs_count() const { return seen_pairs.size(); }
    bool is_expansion_changed() const { return expansion_changed; }
    bool has_left_epsilon() const { return left_epsilon; }
    bool has_right_epsilon() const { return right_epsilon; }
};

} // namespace CustomOps