#include <iostream>
#include <bitset>
#include <set>
#include <unordered_set>
#include <stdexcept>

#include "custom_planner.h"
#include "graph_models/rdf_model/rdf_model.h"
#include "graph_models/rdf_model/conversions.h"

#define CALC_RIGHT_POOL_VALUE(right_cand) \
      ((right_cand) > 0 ? ((right_cand) - 1) : \
       (right_cand) < 0 ? (-(right_cand) - 1) : 0)

void CustomPlanner::update_max_var(VarId& max_var, const Id& id) {
    if (id.is_var()) {
        VarId var = id.get_var();
        if (max_var < var)
            max_var = var;
    }
}

void CustomPlanner::init_next_var(OpBasicGraphPattern& op_basic_graph_pattern) {
    next_var = VarId(0);
    for (const auto& op_triple : op_basic_graph_pattern.triples) {
        this->update_max_var(next_var, op_triple.subject);
        this->update_max_var(next_var, op_triple.predicate);
        this->update_max_var(next_var, op_triple.object);
    }
    for (const auto & op_path : op_basic_graph_pattern.paths) {
        this->update_max_var(next_var, op_path.subject);
        this->update_max_var(next_var, op_path.object);
        if (next_var < op_path.var)
            next_var = op_path.var;
    }
    next_var = VarId(next_var.id + 1);
}

std::unique_ptr<Plan> CustomPlanner::get_plan(OpBasicGraphPattern& op_basic_graph_pattern) {
    CustomPlanner planner;
    planner.init_next_var(op_basic_graph_pattern);
    std::vector<std::unique_ptr<Plan>> plan_vec;
    planner.dp(op_basic_graph_pattern.triples, op_basic_graph_pattern.paths, false, plan_vec);
    if (plan_vec.empty())
        return nullptr;
    double current_best_cost = MAX_COST;
    size_t best_plan_idx = 0;
    for (size_t i = 0; i < plan_vec.size(); i++) {
        const auto *cur_plan_ptr = dynamic_cast<TwoColumnPlan *>(plan_vec[i].get());
        if (cur_plan_ptr && cur_plan_ptr->get_has_constant_col2())
            continue;   // If all has constant col2, returning plan_vec[0] is correct
        if (plan_vec[i]->estimate_cost() < current_best_cost)
            best_plan_idx = i;
    }
    return plan_vec[best_plan_idx]->clone();
}

// Helper function to assign new variable if Id is null OID
Id CustomPlanner::assign_var_if_null_oid(Id id) {
    if (id.is_OID() && id.get_OID().is_null()) {
        return assign_var();
    }
    return id;
}

VarId CustomPlanner::assign_var() {
    VarId new_var = next_var;
    next_var = VarId(next_var.id + 1);
    return new_var;
}

// Helper function to create BPlusTreeTwoColumnBindingIterPlan with appropriate index selection
std::unique_ptr<Plan> CustomPlanner::create_btree_plan(const SPARQL::OpTriple& triple, bool use_pso_direction) {
    if (use_pso_direction) {
        if (triple.subject.is_var() && triple.object.is_var()) {
            return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
                *(rdf_model.pso),
                triple.predicate.get_OID(),
                triple.subject.get_var(),
                triple.object.get_var(),
                BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::PSO
            );
        } else if (triple.subject.is_OID() && triple.object.is_var()) {
            return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
                *(rdf_model.pso),
                triple.predicate.get_OID(),
                triple.subject.get_OID(),
                triple.object.get_var(),
                BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::PSO
            );
        } else if (triple.subject.is_var() && triple.object.is_OID()) {
            return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
                *(rdf_model.pso),
                triple.predicate.get_OID(),
                triple.subject.get_var(),
                triple.object.get_OID(),
                BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::PSO
            );
        }
    } else {
        if (triple.subject.is_var() && triple.object.is_var()) {
            return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
                *(rdf_model.pos),
                triple.predicate.get_OID(),
                triple.object.get_var(),
                triple.subject.get_var(),
                BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::POS
            );
        } else if (triple.subject.is_var() && triple.object.is_OID()) {
            return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
                *(rdf_model.pos),
                triple.predicate.get_OID(),
                triple.object.get_OID(),
                triple.subject.get_var(),
                BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::POS
            );
        } else if (triple.subject.is_OID() && triple.object.is_var()) {
            return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
                *(rdf_model.pos),
                triple.predicate.get_OID(),
                triple.object.get_var(),
                triple.subject.get_OID(),
                BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::POS
            );
        }
    }
    throw std::logic_error("CustomPlanner: Invalid triple configuration for BTree plan creation.");
}

// Helper function to create atom-based BPlusTreeTwoColumnBindingIterPlan
std::unique_ptr<Plan> CustomPlanner::create_atom_plan(const PathAtom* atom, Id src, Id dst, bool use_pso_direction) {
    BPlusTreeTwoColumnBindingIterPlan<3>::IndexType index_type;
    const BPlusTree<3>* bpt_ptr = nullptr;
    
    if (use_pso_direction) {
        index_type = BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::PSO;
        bpt_ptr = rdf_model.pso.get();
    } else {
        index_type = BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::POS;
        bpt_ptr = rdf_model.pos.get();
    }

    // if (use_pso_direction) {
    //     index_type = atom->inverse ? BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::POS :
    //                  BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::PSO;
    //     bpt_ptr = atom->inverse ? rdf_model.pos.get() : rdf_model.pso.get();
    // } else {
    //     index_type = atom->inverse ? BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::PSO :
    //                  BPlusTreeTwoColumnBindingIterPlan<3>::IndexType::POS;
    //     bpt_ptr = atom->inverse ? rdf_model.pso.get() : rdf_model.pos.get();
    // }

    if (src.is_var() && dst.is_var()) {
        // VarId src_var = use_pso_direction ? src.get_var() : dst.get_var();
        // VarId dst_var = use_pso_direction ? dst.get_var() : src.get_var();
        return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
            *bpt_ptr, Conversions::pack_iri(atom->atom), src.get_var(), dst.get_var(), index_type
        );
    } else if (src.is_OID() && dst.is_var()) {
        return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
            *bpt_ptr, Conversions::pack_iri(atom->atom), src.get_OID(), dst.get_var(), index_type
        );
    } else if (src.is_var() && dst.is_OID()) {
        return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
            *bpt_ptr, Conversions::pack_iri(atom->atom), src.get_var(), dst.get_OID(), index_type
        );
        // return std::make_unique<BPlusTreeTwoColumnBindingIterPlan<3>>(
        //     *bpt_ptr, Conversions::pack_iri(atom->atom), dst.get_OID(), src.get_var(), index_type
        // );
    }
    throw std::logic_error("CustomPlanner: Invalid atom configuration for plan creation.");
}

// Helper function to handle plan cost comparison and replacement
bool CustomPlanner::try_replace_with_better_plan(std::unordered_map<std::bitset<BITSET_SIZE>, std::vector<std::unique_ptr<Plan>>>& planMap,
                                                const std::bitset<BITSET_SIZE>& combined_bitset,
                                                std::unique_ptr<Plan> new_plan, bool inverse) {
    double new_cost = new_plan->estimate_cost();
    double current_best_cost = MAX_COST;

    auto existing_plan_it = planMap.find(combined_bitset);
    size_t replace_pos = 0;

    const auto *ti_two_way_new_plan_ptr = dynamic_cast<TITwoWayOperatorPlan *>(new_plan.get());
    if (ti_two_way_new_plan_ptr) {
        if (existing_plan_it == planMap.end()) {
            planMap[combined_bitset] = std::vector<std::unique_ptr<Plan>>(2);
            existing_plan_it = planMap.find(combined_bitset);
            existing_plan_it->second[0] = nullptr;
            existing_plan_it->second[1] = nullptr;
        } else if (existing_plan_it->second.empty()) {
            existing_plan_it->second = std::vector<std::unique_ptr<Plan>>(2);
            existing_plan_it->second[0] = nullptr;
            existing_plan_it->second[1] = nullptr;
        }
        if (existing_plan_it->second.size() != 2) {
            throw std::logic_error("CustomPlanner: TITwoWay planMap should have two plans, now got " + std::to_string(existing_plan_it->second.size()));
        }
        replace_pos = inverse ? 1 : 0;
        if (existing_plan_it->second[replace_pos]) {
            current_best_cost = existing_plan_it->second[0]->estimate_cost();
        }
        if (new_cost < current_best_cost) {
            existing_plan_it->second[replace_pos] = std::move(new_plan);
            return true;
        }
        return false;
    }
    
    if (existing_plan_it != planMap.end() && !existing_plan_it->second.empty()) {
        const auto *new_plan_ptr = dynamic_cast<TwoColumnPlan *>(new_plan.get());
        if (!new_plan_ptr) {
            // If the current plan is TI with constant col2 edge/paths, only consider it when the stored best plan is also this
            const auto *ti_new_plan_ptr = dynamic_cast<TIOperatorPlan *>(new_plan.get());
            const auto *ti_cur_plan_ptr = dynamic_cast<TIOperatorPlan *>(existing_plan_it->second[0].get());
            bool ti_new_plan_const_col2 = ti_new_plan_ptr ? ti_new_plan_ptr->get_const_col2() : false;
            bool ti_cur_plan_const_col2 = ti_cur_plan_ptr ? ti_cur_plan_ptr->get_const_col2() : false;
            if (ti_new_plan_const_col2 && !ti_cur_plan_const_col2) {
                return false;
            }
            if (!ti_new_plan_const_col2 && ti_cur_plan_const_col2) {
                planMap[combined_bitset][0] = std::move(new_plan);
                return true;
            }
            current_best_cost = existing_plan_it->second[0]->estimate_cost();
        } else {
            // Only consider replacing when of the same direction
            VarId new_plan_col1(0), new_plan_col2(0);
            bool new_plan_has_constant_col1 = new_plan_ptr->get_has_constant_col1();
            bool new_plan_has_constant_col2 = new_plan_ptr->get_has_constant_col2();
            ObjectId new_plan_col1_constant, new_plan_col2_constant;
            if (new_plan_has_constant_col1) {
                new_plan_col1_constant = new_plan_ptr->get_constant_col1_value();
            } else {
                new_plan_col1 = new_plan_ptr->get_col1_var();
            }
            if (new_plan_has_constant_col2) {
                new_plan_col2_constant = new_plan_ptr->get_constant_col2_value();
            } else {
                new_plan_col2 = new_plan_ptr->get_col2_var();
            }
            for (size_t i = 0; i < existing_plan_it->second.size(); i++) {
                const auto &compared_plan = existing_plan_it->second[i];
                const auto *compared_plan_ptr = dynamic_cast<TwoColumnPlan *>(compared_plan.get());
                if (!compared_plan_ptr) {
                    continue;
                }
                VarId compared_plan_col1(0), compared_plan_col2(0);
                bool compared_plan_has_constant_col1 = compared_plan_ptr->get_has_constant_col1();
                bool compared_plan_has_constant_col2 = compared_plan_ptr->get_has_constant_col2();
                ObjectId compared_plan_col1_constant, compared_plan_col2_constant;
                if (compared_plan_has_constant_col1) {
                    compared_plan_col1_constant = compared_plan_ptr->get_constant_col1_value();
                } else {
                    compared_plan_col1 = compared_plan_ptr->get_col1_var();
                }
                if (compared_plan_has_constant_col2) {
                    compared_plan_col2_constant = compared_plan_ptr->get_constant_col2_value();
                } else {
                    compared_plan_col2 = compared_plan_ptr->get_col2_var();
                }
                if (new_plan_has_constant_col1 != compared_plan_has_constant_col1)
                    continue;
                if ((new_plan_has_constant_col1 && new_plan_col1_constant != compared_plan_col1_constant)
                    || (new_plan_col1 != compared_plan_col1)) {
                    continue;
                }
                if (new_plan_has_constant_col2 != compared_plan_has_constant_col2)
                    continue;
                if ((new_plan_has_constant_col2 && new_plan_col2_constant != compared_plan_col2_constant)
                    || (new_plan_col2 != compared_plan_col2)) {
                    continue;
                }
                current_best_cost = compared_plan->estimate_cost();
                replace_pos = i;
            }
         }
    }

    if (new_cost < current_best_cost) {
        // planMap[combined_bitset].clear();
        if (current_best_cost == MAX_COST) {
            planMap[combined_bitset].emplace_back(std::move(new_plan));
        }
        else {
            planMap[combined_bitset][replace_pos] = std::move(new_plan);
        }
        return true;
    }
    return false;
}

void CustomPlanner::rpq_preprocess(const RegularPathExpr *path, Id path_subject, Id path_object, std::vector<std::unique_ptr<Plan>> &plan_vec) {
    if (path_subject.is_OID() && path_object.is_OID())
        throw std::logic_error("CustomPlanner: Two constants in an RPQ PathAtom not handled.");
    const std::type_info& path_type = typeid(*path);
    plan_vec.clear();
    if (path_type == typeid(PathAlternatives)) {
        Id src = assign_var_if_null_oid(path_subject);
        Id dst = assign_var_if_null_oid(path_object);
        std::unique_ptr<UnionOperatorPlan> union_plan_ptr = nullptr;
        auto real_path = dynamic_cast<const PathAlternatives *>(path);
        if (src.is_var() || dst.is_var()) {
            std::vector<std::unique_ptr<Plan>> pso_plans, pos_plans;
            for (const auto &alt : real_path->alternatives) {
                process_path_alternative(alt.get(), src, dst, pso_plans, pos_plans);
            }
            if (src.is_var() && dst.is_var()) {
                union_plan_ptr = std::make_unique<UnionOperatorPlan>(std::vector<std::unique_ptr<Plan>>(), src.get_var(), dst.get_var());
            } else if (src.is_var()) {
                union_plan_ptr = std::make_unique<UnionOperatorPlan>(std::vector<std::unique_ptr<Plan>>(), src.get_var(), dst.get_OID());
            } else {
                union_plan_ptr = std::make_unique<UnionOperatorPlan>(std::vector<std::unique_ptr<Plan>>(), src.get_OID(), dst.get_var());
            }
            for (auto &plan : pso_plans) {
                union_plan_ptr->add_child(std::move(plan));
            }
            plan_vec.emplace_back(std::move(union_plan_ptr));
            if (src.is_var() && dst.is_var()) {
                union_plan_ptr = std::make_unique<UnionOperatorPlan>(std::vector<std::unique_ptr<Plan>>(), dst.get_var(), src.get_var());
            } else if (src.is_var()) {
                union_plan_ptr = std::make_unique<UnionOperatorPlan>(std::vector<std::unique_ptr<Plan>>(), dst.get_OID(), src.get_var());
            } else {
                union_plan_ptr = std::make_unique<UnionOperatorPlan>(std::vector<std::unique_ptr<Plan>>(), dst.get_var(), src.get_OID());
            }
            for (auto &plan : pos_plans)
                union_plan_ptr->add_child(std::move(plan));
            plan_vec.emplace_back(std::move(union_plan_ptr));
        }
        // else if (src.is_OID() && dst.is_var()) {
        //     union_plan_ptr = std::make_unique<UnionOperatorPlan>(std::vector<std::unique_ptr<Plan>>(), src.get_OID(), dst.get_var());
        //     for (const auto &alt : real_path->alternatives) {
        //         process_path_alternative_unidirectional(alt.get(), src, dst, union_plan_ptr, true);
        //     }
        //     plan_vec.emplace_back(std::move(union_plan_ptr));
        // } else if (src.is_var() && dst.is_OID()) {
        //     union_plan_ptr = std::make_unique<UnionOperatorPlan>(std::vector<std::unique_ptr<Plan>>(), dst.get_OID(), src.get_var());
        //     for (const auto &alt : real_path->alternatives) {
        //         process_path_alternative_unidirectional((alt->invert()).get(), dst, src, union_plan_ptr, false);
        //     }
        //     plan_vec.emplace_back(std::move(union_plan_ptr));
        // }
        else {
            throw std::logic_error("CustomPlanner: Two constants in an RPQ PathAtom not handled.");
        }
    } else if (path_type == typeid(PathSequence)) {
        auto real_path = dynamic_cast<const PathSequence *>(path);
        std::vector<SPARQL::OpTriple> cur_triples;
        std::vector<SPARQL::OpPath> cur_paths;
        size_t seq_len = (real_path->sequence).size();
        Id src = assign_var_if_null_oid(path_subject);
        Id dst = (seq_len == 1) ? assign_var_if_null_oid(path_object) : assign_var();
        for (int it = 0; it < int(seq_len); it++) {
            const auto &seq = (real_path->sequence)[it];
            const std::type_info& seq_type = typeid(*seq);
            if (seq_type == typeid(PathAtom)) {
                auto real_seq = dynamic_cast<const PathAtom *>(seq.get());
                if (real_seq->inverse)
                    cur_triples.emplace_back(dst, Conversions::pack_iri(real_seq->atom), src);
                else
                    cur_triples.emplace_back(src, Conversions::pack_iri(real_seq->atom), dst);
            } else {
                VarId cur_path_var = assign_var();
                cur_paths.emplace_back(cur_path_var, src, dst, PathSemantic::DEFAULT, seq->clone());
            }
            src = dst;
            if (seq_len > 1 && it < int(seq_len) - 1) {
                if (it == int(seq_len) - 2)
                    dst = assign_var_if_null_oid(path_object);
                else
                    dst = assign_var();
            }
        }
        dp(cur_triples, cur_paths, true, plan_vec);
    } else if (path_type == typeid(PathKleenePlus) || path_type == typeid(PathKleeneStar)) {
        Id src = assign_var_if_null_oid(path_subject);
        Id dst = assign_var_if_null_oid(path_object);
        kleene_planning(path, src, dst, plan_vec);
    } else if (path_type == typeid(PathAtom)) {
        Id src = assign_var_if_null_oid(path_subject);
        Id dst = assign_var_if_null_oid(path_object);
        auto real_atom = dynamic_cast<const PathAtom *>(path);
        if (src.is_var() || dst.is_var()) {
            bool use_pso = (!(real_atom->inverse));
            plan_vec.emplace_back(create_atom_plan(real_atom, src, dst, use_pso));
            plan_vec.emplace_back(create_atom_plan(real_atom, dst, src, !use_pso));
        }
        // else if (src.is_OID() && dst.is_var()) {
        //     plan_vec.emplace_back(create_atom_plan(real_atom, src, dst, true));
        // } else if (src.is_var() && dst.is_OID()) {
        //     plan_vec.emplace_back(create_atom_plan(real_atom, src, dst, false));
        // }
        else {
            throw std::logic_error("CustomPlanner: Two constants in an RPQ PathAtom not handled.");
        }
    } else if (path_type == typeid(PathOptional)) {
        rpq_preprocess((dynamic_cast<const PathOptional *>(path))->path.get(), path_subject, path_object, plan_vec);
        for (auto &pl : plan_vec) {
            pl->set_epsilon(true);
        }
    }
}

void CustomPlanner::dp(const std::vector<SPARQL::OpTriple> &_triples_copy, const std::vector<SPARQL::OpPath> &_paths_copy, bool inRPQ,
std::vector<std::unique_ptr<Plan>> &ret_plan_vec) {
    size_t triples_num = _triples_copy.size(), paths_num = _paths_copy.size();
    const size_t total_patterns = triples_num + paths_num;
    if (total_patterns > BITSET_SIZE) {
        throw std::logic_error("CustomPlanner: Too many triples/paths for current bitset size. Max is " + std::to_string(BITSET_SIZE));
    }
    std::unordered_map<std::bitset<BITSET_SIZE>, std::vector<std::unique_ptr<Plan>>> planMap;
    // Cache for extract_vars_from_pattern results to avoid recomputation
    std::unordered_map<std::bitset<BITSET_SIZE>, std::unordered_set<VarId>> vars_cache;

    // Helper lambda to get variables from cache or compute them
    auto get_vars_from_pattern = [&](const std::bitset<BITSET_SIZE>& pattern) -> const std::unordered_set<VarId>& {
        auto it = vars_cache.find(pattern);
        if (it != vars_cache.end()) {
            return it->second;
        }
        std::unordered_set<VarId> vars;
        extract_vars_from_pattern(vars, pattern, _triples_copy, _paths_copy);
        return vars_cache.emplace(pattern, std::move(vars)).first->second;
    };

    std::bitset<BITSET_SIZE> cur_bitset;
    for (size_t i = 0; i < triples_num; i++) {
        const auto &op_triple = _triples_copy[i];
        cur_bitset.reset();
        cur_bitset.set(i);
        if (op_triple.subject.is_var() || op_triple.object.is_var()) {
            // PSO direction (subject->object)
            planMap[cur_bitset].emplace_back(create_btree_plan(op_triple, true));
            // POS direction (object->subject)
            planMap[cur_bitset].emplace_back(create_btree_plan(op_triple, false));
        }
        // else if (op_triple.subject.is_OID() && op_triple.object.is_var()) {
        //     planMap[cur_bitset].emplace_back(create_btree_plan(op_triple, true));
        // } else if (op_triple.subject.is_var() && op_triple.object.is_OID()) {
        //     planMap[cur_bitset].emplace_back(create_btree_plan(op_triple, false));
        // }
        else {
            throw std::logic_error("CustomPlanner: Two constants in a triple pattern not supported.");
        }
    }
    for (size_t i = 0; i < paths_num; i++) {
        const auto &op_path = _paths_copy[i];
        cur_bitset.reset();
        cur_bitset.set(triples_num + i);
        rpq_preprocess(op_path.path.get(), op_path.subject, op_path.object, planMap[cur_bitset]);
    }

    // Record the triples & paths pointing to each var (for TI, only consider the edges with 2 vars)
    std::unordered_map<Id, std::vector<int>, IdHash> var2tps;
    if (!inRPQ) {

        // Lambda function to add variable to var2tps map
        auto add_var_or_obj_to_tps = [&var2tps](Id cur_id, int value) {
            auto cur_pos = var2tps.find(cur_id);
            if (cur_pos == var2tps.end())
                var2tps[cur_id] = std::vector<int>(1, value);
            else
                (cur_pos->second).emplace_back(value);
        };

        for (size_t it = 0; it < triples_num; it++) {
            const auto &op_triple = _triples_copy[it];
            if (op_triple.subject.is_var() && op_triple.object.is_var()) {
                add_var_or_obj_to_tps(op_triple.object.get_var(), it + 1);
                add_var_or_obj_to_tps(op_triple.subject.get_var(), -(it + 1));
            } else if (op_triple.subject.is_var() && op_triple.object.is_OID()) {
                add_var_or_obj_to_tps(op_triple.object.get_OID(), it + 1);
            } else if (op_triple.subject.is_OID() && op_triple.object.is_var()) {
                add_var_or_obj_to_tps(op_triple.subject.get_OID(), -(it + 1));
            }
        }
        for (size_t it = 0; it < paths_num; it++) {
            const auto &op_path = _paths_copy[it];
            if (op_path.subject.is_var() && op_path.object.is_var()) {
                add_var_or_obj_to_tps(op_path.object.get_var(), it + triples_num + 1);
                add_var_or_obj_to_tps(op_path.subject.get_var(), -(it + triples_num + 1));
            } else if (op_path.subject.is_var() && op_path.object.is_OID()) {
                add_var_or_obj_to_tps(op_path.object.get_OID(), it + triples_num + 1);
            } else if (op_path.subject.is_OID() && op_path.object.is_var()) {
                add_var_or_obj_to_tps(op_path.subject.get_OID(), -(it + triples_num + 1));
            }
        }
    }
    size_t max_tps_num = 0;
    for (const auto &v2t : var2tps) {
        if (v2t.second.size() > max_tps_num)
            max_tps_num = v2t.second.size();
    }

    // DP process - iterate across all combinations of same size
    for (size_t k = 2; k <= triples_num + paths_num; k++) {
        if (inRPQ) {
            // Consider TITwoWay
            for (size_t left_size = std::ceil(float(k)/2.); left_size < k; left_size++) {
                size_t right_size = k - left_size;
                for (const auto &plan_pair_left : planMap) {
                    if (plan_pair_left.first.count() == left_size) {
                        // const auto& left_vars = get_vars_from_pattern(plan_pair_left.first);
                        if (plan_pair_left.second.size() != 2) {
                            throw std::logic_error("CustomPlanner: TITwoWay planning encountered plan size != 2 in left.");
                        }
                        for (size_t left_it = 0; left_it < plan_pair_left.second.size(); left_it++) {
                            const auto &left_p = plan_pair_left.second[left_it];
                            const auto *left_p_ptr = dynamic_cast<TwoColumnPlan *>(left_p.get());
                            if (!left_p_ptr) {
                                break;
                            }
                            if (left_p_ptr->get_has_constant_col2()) {
                                continue;
                            }
                            VarId left_col1(0), left_col2(left_p_ptr->get_col2_var());
                            ObjectId left_col1_const;
                            if (left_p_ptr->get_has_constant_col1())
                                left_col1_const = left_p_ptr->get_constant_col1_value();
                            else
                                left_col1 = left_p_ptr->get_col1_var();
                            for (const auto &plan_pair_right : planMap) {
                                if (plan_pair_right.first.count() != right_size || (plan_pair_left.first & plan_pair_right.first) != 0)
                                    continue;
                                if (plan_pair_right.second.size() != 2) {
                                    throw std::logic_error("CustomPlanner: TITwoWay planning encountered plan size != 2 in right.");
                                }
                                for (size_t right_it = 0; right_it < plan_pair_right.second.size(); right_it++) {
                                    const auto &right_p = plan_pair_right.second[right_it];
                                    const auto *right_p_ptr = dynamic_cast<TwoColumnPlan *>(right_p.get());
                                    if (!right_p_ptr) {
                                        break;
                                    }
                                    bool right_has_constant_col1 = right_p_ptr->get_has_constant_col1();
                                    if (right_has_constant_col1)
                                        continue;
                                    bool right_has_constant_col2 = right_p_ptr->get_has_constant_col2();
                                    VarId right_col1(right_p_ptr->get_col1_var()), right_col2(0);
                                    ObjectId right_col2_const;
                                    if (!right_has_constant_col2) {
                                        right_col2 = right_p_ptr->get_col2_var();
                                    } else {
                                        right_col2_const = right_p_ptr->get_constant_col2_value();
                                    }
                                    if (left_col2 == right_col1) {
                                        std::bitset<BITSET_SIZE> combined_bitset = plan_pair_left.first | plan_pair_right.first;
                                        std::unique_ptr<TITwoWayOperatorPlan> new_plan_ptr;
                                        if (left_p_ptr->get_has_constant_col1()) {
                                           new_plan_ptr = std::make_unique<TITwoWayOperatorPlan>(
                                               left_p->clone(), right_p->clone(), left_col1_const, right_col2, (left_p->get_epsilon()) && (right_p->get_epsilon())
                                           );
                                        } else if (right_p_ptr->get_has_constant_col2()) {
                                            new_plan_ptr = std::make_unique<TITwoWayOperatorPlan>(
                                                left_p->clone(), right_p->clone(), left_col1, right_col2_const, (left_p->get_epsilon()) && (right_p->get_epsilon())
                                            );
                                        } else {
                                            new_plan_ptr = std::make_unique<TITwoWayOperatorPlan>(
                                                left_p->clone(), right_p->clone(), left_col1, right_col2, (left_p->get_epsilon()) && (right_p->get_epsilon())
                                            );
                                        }
                                        try_replace_with_better_plan(planMap, combined_bitset, std::move(new_plan_ptr), left_it == 1);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            // Consider SJ
            for (size_t left_size = std::ceil(float(k)/2.); left_size < k; left_size++) {
                size_t right_size = k - left_size;
                for (const auto &plan_pair_left : planMap) {
                    if (plan_pair_left.first.count() == left_size) {
                        // const auto& left_vars = get_vars_from_pattern(plan_pair_left.first);
                        if (plan_pair_left.second.size() > 2) {
                            throw std::logic_error("CustomPlanner: SJ planning encountered plan size > 2 in left.");
                        }
                        for (size_t left_it = 0; left_it < plan_pair_left.second.size(); left_it++) {
                            const auto &left_p = plan_pair_left.second[left_it];
                            const auto *left_p_ptr = dynamic_cast<TwoColumnPlan *>(left_p.get());
                            if (left_p_ptr) {
                                break;  // SJ does not handle joining edges / paths
                            }
                            const auto& left_vars = get_vars_from_pattern(plan_pair_left.first);
                            for (const auto &plan_pair_right : planMap) {
                                if (plan_pair_right.first.count() != right_size || (plan_pair_left.first & plan_pair_right.first) != 0)
                                continue;
                                if (plan_pair_right.second.size() > 2) {
                                    throw std::logic_error("CustomPlanner: SJ planning encountered plan size > 2 in right.");
                                }
                                for (size_t right_it = 0; right_it < plan_pair_right.second.size(); right_it++) {
                                    const auto &right_p = plan_pair_right.second[right_it];
                                    const auto *right_p_ptr = dynamic_cast<TwoColumnPlan *>(right_p.get());
                                    if (right_p_ptr) {
                                        break;  // SJ does not handle joining edges / paths
                                    }
                                    const auto& right_vars = get_vars_from_pattern(plan_pair_right.first);
                                    if (has_common_elements(left_vars, right_vars)) {
                                        std::bitset<BITSET_SIZE> combined_bitset = plan_pair_left.first | plan_pair_right.first;
                                        std::unique_ptr<SJOperatorPlan> new_plan_ptr = std::make_unique<SJOperatorPlan>(
                                            left_p->clone(), right_p->clone(),
                                            std::vector<VarId>(left_vars.begin(), left_vars.end()), std::vector<VarId>(right_vars.begin(), right_vars.end())
                                        );
                                        try_replace_with_better_plan(planMap, combined_bitset, std::move(new_plan_ptr));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Consider TI
            // right_num cannot be greater than the max num in var2tps
            size_t min_leftmost_size = (max_tps_num >= k) ? 1 : k - max_tps_num;
            for (size_t leftmost_size = min_leftmost_size; leftmost_size < k; leftmost_size++) {
                size_t right_num = k - leftmost_size;
                for (const auto &leftmost_plan : planMap) {
                    if (leftmost_plan.first.count() == leftmost_size) {
                        // Get variables from the best leftmost plan (for variable checking)
                        const auto &leftmost_vars = leftmost_plan.second[0]->get_vars();
                        for (const auto &v2t : var2tps) {
                            if (v2t.second.size() < right_num)
                                continue;
                            // Exclude edges already in leftmost; make a copy to easily choose from
                            // What happens to edges with only one var? If there's a constant, we treat it as source (col1), so only SJ can be used for them.
                            // The other var in the edge needs to be already in leftmost.
                            std::vector<int> right_pool;
                            for (int right_cand : v2t.second) {
                                if (!(leftmost_plan.first[CALC_RIGHT_POOL_VALUE(right_cand)])) {
                                    if (right_cand > 0) {
                                        if ((right_cand - 1 < int(triples_num) && _triples_copy[right_cand - 1].subject.is_var() 
                                        && leftmost_vars.find(_triples_copy[right_cand - 1].subject.get_var()) != leftmost_vars.end())
                                        || (right_cand - 1 >= int(triples_num) && _paths_copy[right_cand - 1 - triples_num].subject.is_var()
                                        && leftmost_vars.find(_paths_copy[right_cand - 1 - triples_num].subject.get_var()) != leftmost_vars.end())) {
                                            right_pool.emplace_back(right_cand);
                                        }
                                    } else if (right_cand < 0) {
                                        if ((-right_cand - 1 < int(triples_num) && _triples_copy[-right_cand - 1].object.is_var() 
                                        && leftmost_vars.find(_triples_copy[-right_cand - 1].object.get_var()) != leftmost_vars.end())
                                        || (-right_cand - 1 >= int(triples_num) && _paths_copy[-right_cand - 1 - triples_num].object.is_var()
                                        && leftmost_vars.find(_paths_copy[-right_cand - 1 - triples_num].object.get_var()) != leftmost_vars.end())) {
                                            right_pool.emplace_back(right_cand);
                                        }
                                    }
                                }
                            }
                            if (right_pool.size() < right_num)
                                continue;
                            // Choose right_num edges from right_pool to form plan. When there are multiple choices, use next_combination
                            std::vector<size_t> cur_indices(right_num, 0);
                            for (size_t it = 0; it < right_num; it++)
                                cur_indices[it] = it;
                            std::bitset<BITSET_SIZE> combined_bitset;
                            do {
                                combined_bitset = leftmost_plan.first;
                                for (size_t idx : cur_indices) {
                                    int set_idx = CALC_RIGHT_POOL_VALUE(right_pool[idx]);
                                    combined_bitset.set(set_idx);
                                }

                                // Try all combinations of leftmost plans with edge plan combinations
                                for (const auto &leftmost_base_plan : leftmost_plan.second) {
                                    // const auto &leftmost_base_plan = leftmost_plan.second[0];
                                    std::vector<std::unique_ptr<Plan>> edge_path_operands;
                                    for (size_t idx : cur_indices) {
                                        std::bitset<BITSET_SIZE> edge_path_bitset;
                                        edge_path_bitset.reset();
                                        int set_idx = CALC_RIGHT_POOL_VALUE(right_pool[idx]);
                                        edge_path_bitset.set(set_idx);
                                        // Use first plan from each edge (could be enhanced to try all combinations)
                                        const auto &edge_path_bitset_pos = planMap.find(edge_path_bitset);
                                        if (edge_path_bitset_pos == planMap.end()) {
                                            throw std::logic_error("CustomPlanner: Queried edge_path_bitset does not exist.");
                                        }
                                        if ((edge_path_bitset_pos->second).size() == 1) {
                                            edge_path_operands.emplace_back(planMap[edge_path_bitset][0]->clone());
                                        }
                                        else {
                                            edge_path_operands.emplace_back(planMap[edge_path_bitset][right_pool[idx] > 0 ? 0 : 1]->clone());
                                        }
                                    }
                                    for (auto &epo : edge_path_operands) {
                                        epo->set_lazy(true);
                                    }
    
                                    bool intersect_in_leftmost = v2t.first.is_var() && (leftmost_vars.find(v2t.first.get_var()) != leftmost_vars.end());
                                    std::unique_ptr<TIOperatorPlan> new_plan_ptr = std::make_unique<TIOperatorPlan>(
                                        leftmost_base_plan->clone(), std::move(edge_path_operands), intersect_in_leftmost
                                    );
                                    try_replace_with_better_plan(planMap, combined_bitset, std::move(new_plan_ptr));
                                }
                            } while (next_combination(cur_indices, right_num, right_pool.size()));
                        }
                    }
                }
            }
            
        }
    }

    cur_bitset.reset();
    for (size_t i = 0; i < triples_num; i++)
        cur_bitset.set(i);
    for (size_t i = 0; i < paths_num; i++)
        cur_bitset.set(triples_num + i);
    for (const auto &pl : planMap[cur_bitset])
        ret_plan_vec.emplace_back(pl->clone());
    // return std::move(get_best_plan(planMap[cur_bitset]));
}

void CustomPlanner::kleene_planning(const RegularPathExpr *path, Id path_subject, Id path_object, std::vector<std::unique_ptr<Plan>> &plan_vec) {
    // Helper lambda to create KCOperatorPlan for Kleene operators
    auto create_kleene_plan = [this](const RegularPathExpr* inner_path, Id subject, Id object, bool epsilon, std::vector<std::unique_ptr<Plan>> &plan_vec) -> void {
        plan_vec.clear();
        std::vector<std::unique_ptr<Plan>> local_plan_vec;
        if (subject.is_var() && object.is_var()) {
            rpq_preprocess(inner_path, subject, object, local_plan_vec);
            if (local_plan_vec.size() != 2)
                throw std::logic_error("CustomPlanner: Should generate bidirectional plans.");
            plan_vec.emplace_back(std::make_unique<KCOperatorPlan>(std::move(local_plan_vec[0]), subject.get_var(), object.get_var(), epsilon));
            plan_vec.emplace_back(std::make_unique<KCOperatorPlan>(std::move(local_plan_vec[1]), object.get_var(), subject.get_var(), epsilon));
        } else if (subject.is_OID() && object.is_var()) {
            // Add new var as subject to prevent fix-point computation error
            VarId new_subject(assign_var());
            rpq_preprocess(inner_path, new_subject, object, local_plan_vec);
            if (local_plan_vec.size() != 2) {
                throw std::logic_error("CustomPlanner: Should generate 2 plans.");
            }
            local_plan_vec[0]->set_lazy(true);            
            plan_vec.emplace_back(std::make_unique<KCOperatorPlan>(std::move(local_plan_vec[0]), subject.get_OID(), object.get_var(), epsilon));
            plan_vec.emplace_back(std::make_unique<KCOperatorPlan>(std::move(local_plan_vec[1]), object.get_var(), subject.get_OID(), epsilon));
        } else if (subject.is_var() && object.is_OID()) {
            // Add new var as object to prevent fix-point computation error
            VarId new_object(assign_var());
            rpq_preprocess(inner_path, subject, new_object, local_plan_vec);
            if (local_plan_vec.size() != 2) {
                throw std::logic_error("CustomPlanner: Should generate 2 plans.");
            }
            local_plan_vec[1]->set_lazy(true);            
            plan_vec.emplace_back(std::make_unique<KCOperatorPlan>(std::move(local_plan_vec[0]), subject.get_var(), object.get_OID(), epsilon));
            plan_vec.emplace_back(std::make_unique<KCOperatorPlan>(std::move(local_plan_vec[1]), object.get_OID(), subject.get_var(), epsilon));
        } else {
            throw std::logic_error("CustomPlanner: Two constants in a triple pattern not supported.");
        }
    };

    const std::type_info& path_type = typeid(*path);
    if (path_type == typeid(PathKleenePlus)) {
        create_kleene_plan(dynamic_cast<const PathKleenePlus*>(path)->path.get(), path_subject, path_object, false, plan_vec);
    } else if (path_type == typeid(PathKleeneStar)) {
        create_kleene_plan(dynamic_cast<const PathKleeneStar*>(path)->path.get(), path_subject, path_object, true, plan_vec);
    }
}


std::unique_ptr<Plan> CustomPlanner::get_best_plan(const std::vector<std::unique_ptr<Plan>>& plans) {
    if (plans.empty()) {
        return nullptr;
    }

    double min_cost = MAX_COST;
    Plan* best_plan = nullptr;

    for (const auto& plan : plans) {
        if (plan && plan->estimate_cost() < min_cost) {
            min_cost = plan->estimate_cost();
            best_plan = plan.get();
        }
    }
    return best_plan ? best_plan->clone() : nullptr;
}

bool CustomPlanner::next_combination(std::vector<size_t>& indices, size_t k, size_t n) {
    // Find the rightmost index that can be incremented
    for (int i = k - 1; i >= 0; i--) {
        if (indices[i] < n - k + i) {
            // Increment this index
            indices[i]++;
            // Reset all indices to the right
            for (size_t j = i + 1; j < k; j++) {
                indices[j] = indices[j - 1] + 1;
            }
            return true;
        }
    }
    return false; // No more combinations
}

// Helper function to process a single alternative in PathAlternatives
void CustomPlanner::process_path_alternative(const RegularPathExpr* alt, Id src, Id dst,
                                            std::vector<std::unique_ptr<Plan>>& pso_plans,
                                            std::vector<std::unique_ptr<Plan>>& pos_plans) {
    const std::type_info& alt_type = typeid(*alt);
    if (alt_type == typeid(PathKleenePlus) || alt_type == typeid(PathKleeneStar)) {
        std::vector<std::unique_ptr<Plan>> local_plan_vec;
        this->kleene_planning(alt, src, dst, local_plan_vec);
        if (local_plan_vec.size() != 2)
            throw std::logic_error("CustomPlanner: Should generate bidirectional plans.");
        pso_plans.emplace_back(std::move(local_plan_vec[0]));
        pos_plans.emplace_back(std::move(local_plan_vec[1]));
    } else if (alt_type == typeid(PathAtom)) {
        auto real_alt = dynamic_cast<const PathAtom*>(alt);
        bool use_pso = !(real_alt->inverse);
        pso_plans.emplace_back(this->create_atom_plan(real_alt, src, dst, use_pso));
        pos_plans.emplace_back(this->create_atom_plan(real_alt, dst, src, !use_pso));
    } else {
        std::vector<std::unique_ptr<Plan>> local_plan_vec;
        this->rpq_preprocess(alt, src, dst, local_plan_vec);
        if (local_plan_vec.size() != 2)
            throw std::logic_error("CustomPlanner: Should generate bidirectional plans.");
        pso_plans.emplace_back(std::move(local_plan_vec[0]));
        pos_plans.emplace_back(std::move(local_plan_vec[1]));
    }
}

// Helper function to process a single alternative for unidirectional case
void CustomPlanner::process_path_alternative_unidirectional(const RegularPathExpr* alt, Id src, Id dst,
                                                           std::unique_ptr<UnionOperatorPlan>& union_plan_ptr,
                                                           bool use_pso_direction) {
    const std::type_info& alt_type = typeid(*alt);
    if (alt_type == typeid(PathKleenePlus) || alt_type == typeid(PathKleeneStar)) {
        std::vector<std::unique_ptr<Plan>> local_plan_vec;
        this->kleene_planning(alt, src, dst, local_plan_vec);
        if (local_plan_vec.size() != 1)
            throw std::logic_error("CustomPlanner: Should generate unidirectional plan.");
        union_plan_ptr->add_child(std::move(local_plan_vec[0]));
    } else if (alt_type == typeid(PathAtom)) {
        auto real_alt = dynamic_cast<const PathAtom*>(alt);
        union_plan_ptr->add_child(this->create_atom_plan(real_alt, src, dst, !(real_alt->inverse)));
    } else {
        std::vector<std::unique_ptr<Plan>> local_plan_vec;
        this->rpq_preprocess(alt, src, dst, local_plan_vec);
        if (local_plan_vec.size() != 1)
            throw std::logic_error("CustomPlanner: Should generate unidirectional plan.");
        union_plan_ptr->add_child(std::move(local_plan_vec[0]));
    }
}

void CustomPlanner::extract_vars_from_pattern(std::unordered_set<VarId>& vars,
                                              const std::bitset<BITSET_SIZE>& pattern,
                                              const std::vector<SPARQL::OpTriple>& triples,
                                              const std::vector<SPARQL::OpPath>& paths) {
    size_t triples_num = triples.size();
    size_t paths_num = paths.size();

    for (size_t it = 0; it < triples_num + paths_num; it++) {
        if (pattern[it]) {
            if (it < triples_num) {
                if (triples[it].subject.is_var())
                    vars.emplace(triples[it].subject.get_var());
                if (triples[it].object.is_var())
                    vars.emplace(triples[it].object.get_var());
            } else {
                size_t path_idx = it - triples_num;
                if (paths[path_idx].subject.is_var())
                    vars.emplace(paths[path_idx].subject.get_var());
                if (paths[path_idx].object.is_var())
                    vars.emplace(paths[path_idx].object.get_var());
            }
        }
    }
}

bool CustomPlanner::has_common_elements(const std::unordered_set<VarId>& set1,
                                        const std::unordered_set<VarId>& set2) {
    // Choose the smaller set to iterate over for better performance
    const auto& smaller_set = (set1.size() <= set2.size()) ? set1 : set2;
    const auto& larger_set = (set1.size() <= set2.size()) ? set2 : set1;

    for (const auto& element : smaller_set) {
        if (larger_set.find(element) != larger_set.end()) {
            return true;
        }
    }
    return false;
}

std::pair<Id, Id> CustomPlanner::get_src_dst_from_index(size_t index,
                                                         const std::vector<SPARQL::OpTriple>& _triples_copy,
                                                         const std::vector<SPARQL::OpPath>& _paths_copy) {
    size_t triples_num = _triples_copy.size();

    if (index < triples_num) {
        // Index corresponds to a triple
        const auto& triple = _triples_copy[index];
        return std::make_pair(triple.subject, triple.object);
    } else {
        // Index corresponds to a path
        size_t path_index = index - triples_num;
        if (path_index < _paths_copy.size()) {
            const auto& path = _paths_copy[path_index];
            return std::make_pair(path.subject, path.object);
        } else {
            throw std::out_of_range("CustomPlanner::get_src_dst_from_index: Index out of range");
        }
    }
}