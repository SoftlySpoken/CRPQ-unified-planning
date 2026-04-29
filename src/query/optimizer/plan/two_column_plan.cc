#include "two_column_plan.h"

// Constructor with variable col1
TwoColumnPlan::TwoColumnPlan(VarId col1_var, VarId col2_var) :
    col1_var(col1_var),
    col2_var(col2_var),
    constant_col1_value(ObjectId::get_null()),
    constant_col2_value(ObjectId::get_null()),
    has_constant_col1(false),
    has_constant_col2(false)
    { }

// Constructor with constant col1
TwoColumnPlan::TwoColumnPlan(ObjectId constant_col1_value, VarId col2_var) :
    col1_var(0),
    col2_var(col2_var),
    constant_col1_value(constant_col1_value),
    constant_col2_value(ObjectId::get_null()),
    has_constant_col1(true),
    has_constant_col2(false)
    { }
// Constructor with constant col2
TwoColumnPlan::TwoColumnPlan(VarId _col1_var, ObjectId _constant_col2_value) :
    col1_var(_col1_var),
    col2_var(0),
    constant_col1_value(ObjectId::get_null()),
    constant_col2_value(_constant_col2_value),
    has_constant_col1(false),
    has_constant_col2(true)
    { }

// Copy constructor
TwoColumnPlan::TwoColumnPlan(const TwoColumnPlan& other) :
    Plan(other),
    col1_var(other.col1_var),
    col2_var(other.col2_var),
    constant_col1_value(other.constant_col1_value),
    constant_col2_value(other.constant_col2_value),
    has_constant_col1(other.has_constant_col1),
    has_constant_col2(other.has_constant_col2)
    { }

// Common helper method for getting variables
std::set<VarId> TwoColumnPlan::get_two_column_vars() const {
    std::set<VarId> vars;

    if (!has_constant_col1) {
        vars.insert(col1_var);
    }
    if (!has_constant_col2) {
        vars.insert(col2_var);
    }

    return vars;
}