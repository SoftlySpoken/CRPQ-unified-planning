#pragma once

#include "query/executor/binding_iter/custom_ops/custom_operator_base.h"
#include <memory>
#include <vector>
#include <unordered_map>
#include "graph_models/object_id.h"

namespace CustomOps {

// SJ (Subgraph Join) Operator
class SJOperator : public CustomOperatorBase {
private:
    std::unique_ptr<BindingIter> left_child;
    std::unique_ptr<BindingIter> right_child;

    // Variables
    std::vector<VarId> left_vars;
    std::vector<VarId> right_vars;
    std::vector<VarId> join_vars;   // conjunction of left_vars & right_vars

    // Internal state for execution
    bool left_exhausted;
    bool hash_table_built;
    Binding* parent_binding_ptr;

    // Hash table: maps join key to list of right bindings
    struct HashKey {
        std::vector<ObjectId> values;

        bool operator==(const HashKey& other) const {
            return values == other.values;
        }
    };

    struct HashKeyHasher {
        std::size_t operator()(const HashKey& key) const {
            std::size_t hash = 0;
            for (const auto& val : key.values) {
                hash ^= std::hash<uint64_t>{}(val.id) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            }
            return hash;
        }
    };

    std::unordered_map<HashKey, std::vector<std::unique_ptr<Binding>>, HashKeyHasher> hash_table;

    // Current probe state
    std::vector<std::unique_ptr<Binding>>::iterator current_matches_iter;
    std::vector<std::unique_ptr<Binding>>::iterator current_matches_end;
    bool in_enumeration_state;

    // Hash table iteration state for synthetic rows
    bool left_epsilon_added;
    bool right_epsilon_added;
    std::unordered_map<HashKey, std::vector<std::unique_ptr<Binding>>, HashKeyHasher>::iterator hash_table_iter;
    std::vector<std::unique_ptr<Binding>>::iterator current_hash_bindings_iter;
    std::vector<std::unique_ptr<Binding>>::iterator current_hash_bindings_end;
    bool hash_bindings_iter_initialized;

    // Helper methods
    void build_hash_table();
    HashKey extract_key(const Binding& binding, const std::vector<VarId>& vars);

protected:
    void _begin(Binding& parent_binding) override;
    bool _next() override;
    void _reset() override;

public:
    SJOperator(std::unique_ptr<BindingIter> left,
               std::unique_ptr<BindingIter> right,
               std::vector<VarId> _left_vars,
               std::vector<VarId> _right_vars);

    ~SJOperator() = default;

    void assign_nulls() override;
    void accept_visitor(BindingIterVisitor& visitor) override;

    OperatorType get_operator_type() const override {
        return OperatorType::SJ;
    }

    // Additional getter methods for detailed printing
    const std::vector<VarId>& get_left_vars() const { return left_vars; }
    const std::vector<VarId>& get_right_vars() const { return right_vars; }
    const std::vector<VarId>& get_join_vars() const { return join_vars; }
    bool is_hash_table_built() const { return hash_table_built; }
    size_t get_hash_table_size() const { return hash_table.size(); }
    bool is_in_enumeration_state() const { return in_enumeration_state; }
    BindingIter *get_left_child() { return left_child.get(); }
    BindingIter *get_right_child() { return right_child.get(); }
};

} // namespace CustomOps