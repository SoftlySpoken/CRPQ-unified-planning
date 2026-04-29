#include "bplus_tree_two_column_binding_iter_plan.h"

#include "query/executor/binding_iter/bplus_tree_two_column_binding_iter.h"

template<std::size_t N>
BPlusTreeTwoColumnBindingIterPlan<N>::BPlusTreeTwoColumnBindingIterPlan(
    const BPlusTree<N>& _btree,
    ObjectId _predicate,
    VarId _col1_var,
    VarId _col2_var,
    IndexType _type
) :
    TwoColumnPlan(_col1_var, _col2_var),
    btree      (_btree),
    predicate  (_predicate),
    index_type (_type)
{
    // Estimate based on B+ tree statistics
    // For a fixed predicate, we expect to scan a subset of the tree
    Record<N> min_record, max_record;
    min_record[0] = predicate.id;
    min_record[1] = 0;  // Start from first subject
    min_record[2] = 0;  // Start from first object
    max_record[0] = predicate.id;
    max_record[1] = UINT64_MAX;  // End at last subject
    max_record[2] = UINT64_MAX;  // End at last object
    estimated_output_size = static_cast<double>(btree.estimate_records(min_record, max_record)) * 10.0; // Rough estimate  [TODO: refine]
    estimated_cost = estimated_output_size * 0.2; // B+ tree scan cost (more efficient than sequential) [TODO: refine]
}

template<std::size_t N>
BPlusTreeTwoColumnBindingIterPlan<N>::BPlusTreeTwoColumnBindingIterPlan(
    const BPlusTree<N>& _btree,
    ObjectId _predicate,
    ObjectId _constant_col1_value,
    VarId _col2_var,
    IndexType _type
) :
    TwoColumnPlan(_constant_col1_value, _col2_var),
    btree      (_btree),
    predicate  (_predicate),
    index_type (_type)
{
    // Estimate based on B+ tree statistics
    // For a fixed predicate and fixed col1, we expect to scan an even smaller subset
    Record<N> min_record, max_record;
    min_record[0] = predicate.id;
    min_record[1] = get_constant_col1_value().id;  // Use constant col1 value
    min_record[2] = 0;  // Start from first object
    max_record[0] = predicate.id;
    max_record[1] = get_constant_col1_value().id;  // Use constant col1 value
    max_record[2] = UINT64_MAX;  // End at last object
    estimated_output_size = static_cast<double>(btree.estimate_records(min_record, max_record)) * 5.0; // Smaller estimate due to more constraints [TODO: refine]
    estimated_cost = estimated_output_size * 0.1; // Even more efficient due to tighter constraints [TODO: refine]
}

template<std::size_t N>
BPlusTreeTwoColumnBindingIterPlan<N>::BPlusTreeTwoColumnBindingIterPlan(
    const BPlusTree<N>& _btree,
    ObjectId _predicate,
    VarId _col1_var,
    ObjectId _constant_col2_value,
    IndexType _type
) :
    TwoColumnPlan(_col1_var, _constant_col2_value),
    btree      (_btree),
    predicate  (_predicate),
    index_type (_type)
{
    // Estimate based on B+ tree statistics
    // For a fixed predicate and fixed col1, we expect to scan an even smaller subset
    Record<N> min_record, max_record;
    min_record[0] = predicate.id;
    min_record[1] = 0;  // Start from first subject
    min_record[2] = get_constant_col2_value().id;
    max_record[0] = predicate.id;
    max_record[1] = UINT64_MAX;  // End at last subject
    max_record[2] = get_constant_col2_value().id;
    estimated_output_size = static_cast<double>(btree.estimate_records(min_record, max_record)) * 5.0; // Smaller estimate due to more constraints [TODO: refine]
    estimated_cost = estimated_output_size * 0.1; // Even more efficient due to tighter constraints [TODO: refine]
}

template<std::size_t N>
int BPlusTreeTwoColumnBindingIterPlan<N>::relation_size() const {
    // This is an estimate since we don't know exact count without scanning
    return static_cast<int>(estimated_output_size); // [TODO: refine]
}

template<std::size_t N>
void BPlusTreeTwoColumnBindingIterPlan<N>::print(std::ostream& os, int indent) const {
    for (int i = 0; i < indent; ++i) {
        os << ' ';
    }
    os << "BPlusTreeTwoColumnBindingIter(";
    os << (index_type == PSO ? "PSO" : "POS");
    os << ", pred=" << predicate.id;
    if (get_has_constant_col1()) {
        os << ", col1=CONST(" << get_constant_col1_value().id << ")";
    } else {
        os << ", col1=VAR(" << get_col1_var().id << ")";
    }
    os << ", col2=VAR(" << get_col2_var().id << "))";
}

template<std::size_t N>
std::set<VarId> BPlusTreeTwoColumnBindingIterPlan<N>::get_vars() const {
    return get_two_column_vars();
}

template<std::size_t N>
void BPlusTreeTwoColumnBindingIterPlan<N>::set_input_vars(const std::set<VarId>& input_vars) {
    // BPlusTreeTwoColumnBindingIter is a base plan, so it doesn't depend on input vars
    // But we can use this for optimization hints in the future
}

template<std::size_t N>
std::unique_ptr<BindingIter> BPlusTreeTwoColumnBindingIterPlan<N>::get_binding_iter() const {
    if (get_has_constant_col1()) {
        return std::make_unique<BPlusTreeTwoColumnBindingIter<N>>(
            btree,
            predicate,
            get_constant_col1_value(),
            get_col2_var(),
            static_cast<typename BPlusTreeTwoColumnBindingIter<N>::IndexType>(index_type),
            epsilon
        );
    } else if (get_has_constant_col2()) {
        return std::make_unique<BPlusTreeTwoColumnBindingIter<N>>(
            btree,
            predicate,
            get_col1_var(),
            get_constant_col2_value(),
            static_cast<typename BPlusTreeTwoColumnBindingIter<N>::IndexType>(index_type),
            epsilon
        );
    } else {
        return std::make_unique<BPlusTreeTwoColumnBindingIter<N>>(
            btree,
            predicate,
            get_col1_var(),
            get_col2_var(),
            static_cast<typename BPlusTreeTwoColumnBindingIter<N>::IndexType>(index_type),
            epsilon
        );
    }
}

// Explicit template instantiations for common sizes
template class BPlusTreeTwoColumnBindingIterPlan<3>;
template class BPlusTreeTwoColumnBindingIterPlan<4>;
// template class BPlusTreeTwoColumnBindingIterPlan<5>;