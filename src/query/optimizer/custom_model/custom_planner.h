#pragma once

#include <unordered_set>
#include <bitset>
#include <cmath>
#include "query/optimizer/plan/plan.h"
#include "query/query_context.h"
#include "query/parser/op/sparql/ops.h"

#include "query/parser/paths/path_sequence.h"
#include "query/parser/paths/path_alternatives.h"
#include "query/parser/paths/path_kleene_plus.h"
#include "query/parser/paths/path_kleene_star.h"
#include "query/parser/paths/path_atom.h"
#include "query/parser/paths/path_optional.h"

#include "query/optimizer/plan/custom_ops/bplus_tree_two_column_binding_iter_plan.h"
#include "query/optimizer/plan/custom_ops/kc_operator_plan.h"
#include "query/optimizer/plan/custom_ops/mc_operator_plan.h"
#include "query/optimizer/plan/custom_ops/sj_operator_plan.h"
#include "query/optimizer/plan/custom_ops/ti_operator_plan.h"
#include "query/optimizer/plan/custom_ops/ti_two_way_operator_plan.h"
#include "query/optimizer/plan/custom_ops/union_operator_plan.h"

using namespace SPARQL;

// Constants for CustomPlanner
constexpr size_t BITSET_SIZE = 32;
constexpr double MAX_COST = 1e18;

class CustomPlanner {
public:
    CustomPlanner(): next_var(0) {}
    static std::unique_ptr<Plan> get_plan(OpBasicGraphPattern& op_basic_graph_pattern);
    static void extract_vars_from_pattern(std::unordered_set<VarId>& vars,
                                          const std::bitset<32>& pattern,
                                          const std::vector<SPARQL::OpTriple>& triples,
                                          const std::vector<SPARQL::OpPath>& paths);
    static bool has_common_elements(const std::unordered_set<VarId>& set1,
                                   const std::unordered_set<VarId>& set2);
    // Static helper functions that don't need state
    static bool next_combination(std::vector<size_t>& indices, size_t k, size_t n);
private:
    // Non-static helper methods that use instance state
    void rpq_preprocess(const RegularPathExpr *path, Id path_subject, Id path_object, std::vector<std::unique_ptr<Plan>> &plan_vec);
    void dp(const std::vector<SPARQL::OpTriple> &_triples_copy, const std::vector<SPARQL::OpPath> &_paths_copy, bool inRPQ,
        std::vector<std::unique_ptr<Plan>> &ret_plan_vec);
    void kleene_planning(const RegularPathExpr *path, Id path_subject, Id path_object, std::vector<std::unique_ptr<Plan>> &plan_vec);

    // Helper function to assign new variable if Id is null OID
    Id assign_var_if_null_oid(Id id);
    VarId assign_var();

    // Helper function to update the maximum variable ID found
    void update_max_var(VarId& max_var, const Id& id);

    // Helper function to get best plan from vector (lowest cost)
    std::unique_ptr<Plan> get_best_plan(const std::vector<std::unique_ptr<Plan>>& plans);

    // Helper functions to create BPlusTreeTwoColumnBindingIterPlan instances
    std::unique_ptr<Plan> create_btree_plan(const SPARQL::OpTriple& triple, bool use_pso_direction);
    std::unique_ptr<Plan> create_atom_plan(const PathAtom* atom, Id src, Id dst, bool use_pso_direction);

    // Helper function for plan cost comparison and replacement
    bool try_replace_with_better_plan(std::unordered_map<std::bitset<BITSET_SIZE>, std::vector<std::unique_ptr<Plan>>>& planMap,
                                     const std::bitset<BITSET_SIZE>& combined_bitset,
                                     std::unique_ptr<Plan> new_plan, bool inverse=false);

    // Helper functions for processing path alternatives
    void process_path_alternative(const RegularPathExpr* alt, Id src, Id dst,
                                 std::vector<std::unique_ptr<Plan>>& pso_plans,
                                 std::vector<std::unique_ptr<Plan>>& pos_plans);
    void process_path_alternative_unidirectional(const RegularPathExpr* alt, Id src, Id dst,
                                                std::unique_ptr<UnionOperatorPlan>& union_plan_ptr,
                                                bool use_pso_direction);

    // Initialize next_var
    void init_next_var(OpBasicGraphPattern& op_basic_graph_pattern);

    // Helper function to get source and destination IDs from a bitset index
    static std::pair<Id, Id> get_src_dst_from_index(size_t index,
                                                     const std::vector<SPARQL::OpTriple>& _triples_copy,
                                                     const std::vector<SPARQL::OpPath>& _paths_copy);

    VarId next_var;   // Instance variable to keep track of the next usable var for RPQ preprocessing
};