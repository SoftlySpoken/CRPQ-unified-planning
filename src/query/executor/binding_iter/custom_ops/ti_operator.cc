#include "ti_operator.h"
#include <algorithm>
#include <unordered_set>
#include <iostream>

namespace CustomOps {

TIOperator::TIOperator(std::unique_ptr<BindingIter> _leftmost_operand,
                       std::vector<std::unique_ptr<TwoColumnBindingIter>> _edge_path_operands,
                    bool _intersect_in_leftmost)
    : leftmost_operand(std::move(_leftmost_operand))
    , tcbit_leftmost(dynamic_cast<const TwoColumnBindingIter *>(leftmost_operand.get()))
    , edge_path_operands(std::move(_edge_path_operands))
    , parent_binding_ptr(nullptr)
    , has_current_row(false) 
    , intersect_in_leftmost(_intersect_in_leftmost)
    , left_epsilon_added(false)
    , left_epsilon_terminated(false)
    , left_epsilon_biter(nullptr)
    , cur_left_epsilon_src(0)
    , leftmost_has_constant_col1(false)
    , leftmost_constant_col1(0)
    , only_edge_path_has_constant_col2(false)
    , only_edge_path_constant_col2(0) {
    // this->epsilon = leftmost_operand->epsilon;
    // if (this->epsilon) {
    //     for (const auto &epo : edge_path_operands) {
    //         if (!epo->epsilon) {
    //             this->epsilon = false;
    //             break;
    //         }
    //     }
    // }
    if (tcbit_leftmost) {
        leftmost_has_constant_col1 = tcbit_leftmost->get_has_constant_col1();
        if (leftmost_has_constant_col1)
            leftmost_constant_col1 = tcbit_leftmost->get_constant_col1();
    } else if (const TIOperator *tio = dynamic_cast<const TIOperator *>(leftmost_operand.get())) {
        leftmost_has_constant_col1 = tio->has_leftmost_constant_col1();
        if (leftmost_has_constant_col1)
            leftmost_constant_col1 = tio->get_leftmost_constant_col1();
    }
    if (edge_path_operands.size() == 1 && edge_path_operands[0]->get_has_constant_col2()) {
        only_edge_path_has_constant_col2 = true;
        only_edge_path_constant_col2 = edge_path_operands[0]->get_constant_col2();
    }
}

TIOperator::~TIOperator() = default;

void TIOperator::IntersectionState::reset() {
    neighbor_lists.clear();
    copied_lists.clear();
    neighbor_nums.clear();
    current_positions.clear();
    intersection_complete = false;
}

void TIOperator::_begin(Binding& parent_binding) {
    parent_binding_ptr = &parent_binding;
    leftmost_operand->begin(parent_binding);
    for (auto &epo : edge_path_operands) {
        epo->begin(parent_binding);
    }
    has_current_row = leftmost_operand->next();
    intersection_state.reset();

    if (has_current_row) {
        reset_intersection_for_new_row();
    }
}

bool TIOperator::_next() {
    if (has_current_row) {
        // Try to get next intersection result for current leftmost row
        if (get_next_intersection_result()) {
            return true;
        }
        
        // No more intersection results for current row, advance to next leftmost row
        while (leftmost_operand->next()) {
            reset_intersection_for_new_row();
            if (get_next_intersection_result()) {
                return true;
            }
        }
        
        has_current_row = false;
    }
    
    if (tcbit_leftmost && tcbit_leftmost->get_epsilon() && !left_epsilon_added) {
        left_epsilon_added = true;
        edge_path_operands[0]->reset();
        edge_path_operands[0]->begin(*parent_binding_ptr);
        // Test equivalence when left col1 is constant (if left is two column binding iter)
        if (leftmost_has_constant_col1) {
            if (edge_path_operands[0]->is_lazy())
                edge_path_operands[0]->get_neighbors(leftmost_constant_col1.id);
            edge_path_operands[0]->seek_to_vertex(leftmost_constant_col1.id);
        }
        if (!edge_path_operands[0]->next())
            return false;
        std::pair<ObjectId, ObjectId> cur_pr;
        edge_path_operands[0]->get_current_edge(cur_pr);
        if (leftmost_has_constant_col1 && cur_pr.first != leftmost_constant_col1) {
            left_epsilon_terminated = true;
            return false;
        }
        cur_left_epsilon_src = cur_pr.first.id;
        reset_intersection_for_new_row_left_epsilon();
    }
    if (left_epsilon_added && !left_epsilon_terminated) {
        while (true) {
            if (get_next_intersection_result()) {
                return true;
            }
            else {
                do {
                    if (!edge_path_operands[0]->next()) {
                        left_epsilon_terminated = true;
                        if (edge_path_operands[0]->get_has_constant_col2()) {
                            size_t pb_size = parent_binding_ptr->size;
                            for (size_t i = 0; i < pb_size; i++) {
                                parent_binding_ptr->add(VarId(i), edge_path_operands[0]->get_constant_col2());
                            }
                            return true;
                        }
                        return false;
                    }
                    std::pair<ObjectId, ObjectId> cur_pr;
                    edge_path_operands[0]->get_current_edge(cur_pr);
                    if (cur_pr.first.id != cur_left_epsilon_src) {
                        cur_left_epsilon_src = cur_pr.first.id;
                        break;
                    }
                } while (true);
                if (leftmost_has_constant_col1) {
                    left_epsilon_terminated = true;
                    return false;
                }
                reset_intersection_for_new_row_left_epsilon();
            }
        }
    }

    return false;
}

void TIOperator::_reset() {
    leftmost_operand->reset();
    for (auto &epo : edge_path_operands)
        epo->reset();
    has_current_row = leftmost_operand->next();
    if (has_current_row)
        reset_intersection_for_new_row();
    left_epsilon_added = false;
    left_epsilon_terminated = false;
    cur_left_epsilon_src = 0;
}

void TIOperator::assign_nulls() {
    leftmost_operand->assign_nulls();
    // Note: Edge/path operands (TwoColumnStoreBindingIter) don't need null assignment
    // as they are intermediate results, not binding iterators
}

void TIOperator::accept_visitor(BindingIterVisitor& visitor) {
    // leftmost_operand->accept_visitor(visitor);
    visitor.visit(*this);
}

void TIOperator::sort_neighbor_lists_on_demand() {
    intersection_state.neighbor_lists.clear();
    intersection_state.copied_lists.clear();
    intersection_state.neighbor_nums.clear();
    intersection_state.neighbor_lists.reserve(edge_path_operands.size());
    intersection_state.neighbor_nums.reserve(edge_path_operands.size());

    // For each edge/path operand, collect and sort neighbor lists for common vertices
    // Only one neighbor list for each edge/path, don't copy, just use pointer
    bool only_one_non_leftmost = (edge_path_operands.size() == 1);
    for (const auto& operand : edge_path_operands) {
        // Collect neighbors for all common vertices from this operand
        // The common variable should be col1_var of the operand. If the current col1_var is not in leftmost, output an error msg
        const auto &var = operand->get_col1_var();
        // Skip NULL_ID values
        ObjectId obj_id = (*parent_binding_ptr)[var];
        if (obj_id.is_null()) {
            std::cerr << "Error in TIOperator::sort_neighbor_lists_on_demand: col1_var of the operand is not in leftmost" << std::endl;
            continue;
        }
        uint64_t vertex = obj_id.id;
        auto [neighbors_ptr, count] = operand->get_neighbors(vertex);
        if (operand->get_epsilon() && !operand->get_has_constant_col2()
        && (count == 0 || std::find(neighbors_ptr, neighbors_ptr + count, vertex) == neighbors_ptr + count)) {
            intersection_state.copied_lists.emplace_back();
            intersection_state.copied_lists.back().reserve(count + 1);
            intersection_state.copied_lists.back().assign(neighbors_ptr, neighbors_ptr + count);
            intersection_state.copied_lists.back().emplace_back(vertex);
            std::sort(intersection_state.copied_lists.back().begin(), intersection_state.copied_lists.back().end());
            intersection_state.neighbor_lists.emplace_back(intersection_state.copied_lists.back().data());
            intersection_state.neighbor_nums.emplace_back(count + 1);
        } else if (count == 0) {
            if (operand->get_epsilon() && operand->get_has_constant_col2() && vertex == operand->get_constant_col2().id) {
                intersection_state.copied_lists.emplace_back();
                intersection_state.copied_lists.back().emplace_back(vertex);
                intersection_state.neighbor_lists.emplace_back(intersection_state.copied_lists.back().data());
                intersection_state.neighbor_nums.emplace_back(1);
            } else {
                intersection_state.intersection_complete = true;            
                break;
            }
        } else {
            if (!only_one_non_leftmost) {
                if (TwoColumnStoreBindingIter *derived = dynamic_cast<TwoColumnStoreBindingIter *>(operand.get()))
                    derived->get_store()->sort_neighbors(vertex);
            }
            intersection_state.neighbor_lists.emplace_back(neighbors_ptr);
            intersection_state.neighbor_nums.emplace_back(count);
        }
    }

    // Initialize positions for intersection
    if (!(intersection_state.intersection_complete))
        intersection_state.current_positions.assign(intersection_state.neighbor_lists.size(), 0);
}

bool TIOperator::conduct_multi_way_intersection() {
    if (intersection_state.neighbor_lists.empty()) {
        intersection_state.intersection_complete = true;
        return false;
    }

    // If any list is empty, intersection is empty
    for (size_t sz : intersection_state.neighbor_nums) {
        if (sz == 0) {
            intersection_state.intersection_complete = true;
            return false;
        }
    }

    // Multi-way intersection using sorted lists
    while (!intersection_state.intersection_complete) {
        // Find the maximum value at current positions
        uint64_t max_val = intersection_state.neighbor_lists[0][intersection_state.current_positions[0]];
        bool all_equal = true;

        for (size_t i = 1; i < intersection_state.neighbor_lists.size(); ++i) {
            uint64_t current_val = intersection_state.neighbor_lists[i][intersection_state.current_positions[i]];
            if (current_val > max_val) {
                max_val = current_val;
                all_equal = false;
            } else if (current_val < max_val) {
                all_equal = false;
            }
        }

        if (all_equal) {
            // Found intersection element, bind it to the result
            // The leftmost operand variables should already be in parent_binding
            // from the most recent leftmost_operand->next() call, but we write
            // the intersection result to parent_binding for all col2_vars
            ObjectId intersection_obj_id(max_val);
            bool res_valid = false;

            if (!has_current_row && tcbit_leftmost && tcbit_leftmost->get_epsilon() && left_epsilon_added) {
                size_t pb_size = parent_binding_ptr->size;
                std::pair<ObjectId, ObjectId> cur_pr;
                edge_path_operands[0]->get_current_edge(cur_pr);
                for (size_t i = 0; i < pb_size; i++) {
                    parent_binding_ptr->add(VarId(i), cur_pr.first);
                }
            }
            if (edge_path_operands[0]->get_has_constant_col2()) {
                res_valid = true;
            } else {
                VarId col2_var = edge_path_operands[0]->get_col2_var();
                ObjectId cur_obj_id = (*parent_binding_ptr)[col2_var];
                if (!intersect_in_leftmost) {
                    parent_binding_ptr->add(col2_var, intersection_obj_id);
                    res_valid = true;
                } else if (intersection_obj_id == cur_obj_id) {
                    res_valid = true;
                }
            }

            // Advance all positions to the next element for next iteration
            for (size_t i = 0; i < intersection_state.neighbor_lists.size(); ++i) {
                intersection_state.current_positions[i]++;
                // Check if we've reached the end of any list
                if (intersection_state.current_positions[i] >= intersection_state.neighbor_nums[i]) {
                    intersection_state.intersection_complete = true;
                    break;
                }
            }
            if (res_valid) {
                return true;
            }
        }

        else {
            // Advance pointers for lists with values less than max_val
            bool can_advance = false;
            for (size_t i = 0; i < intersection_state.neighbor_lists.size(); ++i) {
                if (intersection_state.neighbor_lists[i][intersection_state.current_positions[i]] < max_val) {
                    intersection_state.current_positions[i]++;
                    can_advance = true;
    
                    // Check if we've reached the end of this list
                    if (intersection_state.current_positions[i] >= intersection_state.neighbor_nums[i]) {
                        intersection_state.intersection_complete = true;
                        return false;
                    }
                }
            }
    
            if (!can_advance) {
                // This shouldn't happen if the algorithm is correct
                intersection_state.intersection_complete = true;
                return false;
            }
        }
    }
    return false;
}

bool TIOperator::get_next_intersection_result() {
    if (intersection_state.intersection_complete) {
        return false;
    }

    return conduct_multi_way_intersection();
}

void TIOperator::reset_intersection_for_new_row() {
    intersection_state.reset();

    if (!has_current_row || !parent_binding_ptr) {
        return;
    }

    // Sort neighbor lists on demand for these common vertices
    sort_neighbor_lists_on_demand();
}

void TIOperator::sort_neighbor_lists_on_demand_left_epsilon() {
    intersection_state.neighbor_lists.clear();
    intersection_state.neighbor_nums.clear();
    intersection_state.neighbor_lists.reserve(edge_path_operands.size());
    intersection_state.neighbor_nums.reserve(edge_path_operands.size());

    // Get the guiding variable value from the left_epsilon_iter
    uint64_t guiding_vertex = cur_left_epsilon_src;

    // For each edge/path operand, collect and sort neighbor lists using the guiding vertex
    bool only_one_non_leftmost = (edge_path_operands.size() == 1);
    for (const auto& operand : edge_path_operands) {
        auto [neighbors_ptr, count] = operand->get_neighbors(guiding_vertex);
        if (operand->get_epsilon() && !operand->get_has_constant_col2()
        && (count == 0 || std::find(neighbors_ptr, neighbors_ptr + count, guiding_vertex) == neighbors_ptr + count)) {
            intersection_state.copied_lists.emplace_back();
            intersection_state.copied_lists.back().reserve(count + 1);
            intersection_state.copied_lists.back().assign(neighbors_ptr, neighbors_ptr + count);
            intersection_state.copied_lists.back().emplace_back(guiding_vertex);
            std::sort(intersection_state.copied_lists.back().begin(), intersection_state.copied_lists.back().end());
            intersection_state.neighbor_lists.emplace_back(intersection_state.copied_lists.back().data());
            intersection_state.neighbor_nums.emplace_back(count + 1);
        } else if (count == 0) {
            // TODO: operand->get_has_constant_col2, add (const_col2, const_col2)
            intersection_state.intersection_complete = true;            
            break;
        } else {
            if (!only_one_non_leftmost) {
                if (TwoColumnStoreBindingIter *derived = dynamic_cast<TwoColumnStoreBindingIter *>(operand.get()))
                    derived->get_store()->sort_neighbors(guiding_vertex);
            }
            intersection_state.neighbor_lists.emplace_back(neighbors_ptr);
            intersection_state.neighbor_nums.emplace_back(count);
        }
    }

    // Initialize positions for intersection
    if (!(intersection_state.intersection_complete))
        intersection_state.current_positions.assign(intersection_state.neighbor_lists.size(), 0);
}

void TIOperator::reset_intersection_for_new_row_left_epsilon() {
    intersection_state.reset();

    if (!parent_binding_ptr) {
        return;
    }

    sort_neighbor_lists_on_demand_left_epsilon();
}
} // namespace CustomOps