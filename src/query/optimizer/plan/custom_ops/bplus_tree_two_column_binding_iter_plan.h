#pragma once

#include "query/optimizer/plan/two_column_plan.h"
#include "storage/index/bplus_tree/bplus_tree.h"
#include "graph_models/object_id.h"

template<std::size_t N>
class BPlusTreeTwoColumnBindingIterPlan : public TwoColumnPlan {
public:
    // RDF index types (same as in the binding iter)
    enum IndexType { PSO, POS };

    // Constructor with variable col1
    BPlusTreeTwoColumnBindingIterPlan(
        const BPlusTree<N>& btree,
        ObjectId predicate,
        VarId col1_var,
        VarId col2_var,
        IndexType type
    );

    // Constructor with constant col1 value
    BPlusTreeTwoColumnBindingIterPlan(
        const BPlusTree<N>& btree,
        ObjectId predicate,
        ObjectId constant_col1_value,
        VarId col2_var,
        IndexType type
    );

    BPlusTreeTwoColumnBindingIterPlan(
        const BPlusTree<N>& btree,
        ObjectId predicate,
        VarId col1_var,
        ObjectId constant_col2_value,
        IndexType type
    );

    BPlusTreeTwoColumnBindingIterPlan(const BPlusTreeTwoColumnBindingIterPlan& other) :
        TwoColumnPlan(other),
        btree               (other.btree),
        predicate           (other.predicate),
        index_type          (other.index_type),
        estimated_cost      (other.estimated_cost),
        estimated_output_size (other.estimated_output_size) { }

    std::unique_ptr<Plan> clone() const override {
        return std::make_unique<BPlusTreeTwoColumnBindingIterPlan>(*this);
    }

    // only meant to be used by base plans, not joins
    int relation_size() const override;

    double estimate_cost() const override { return estimated_cost; }
    double estimate_output_size() const override { return estimated_output_size; }

    std::set<VarId> get_vars() const override;
    void set_input_vars(const std::set<VarId>& input_vars) override;

    std::unique_ptr<BindingIter> get_binding_iter() const override;

    bool get_leapfrog_iter(std::vector<std::unique_ptr<LeapfrogIter>>&,
                           std::vector<VarId>&,
                           uint_fast32_t&) const override { return false; }

    void print(std::ostream& os, int indent) const override;

private:
    const BPlusTree<N>& btree;
    ObjectId predicate;
    IndexType index_type;

    double estimated_cost;
    double estimated_output_size;
};