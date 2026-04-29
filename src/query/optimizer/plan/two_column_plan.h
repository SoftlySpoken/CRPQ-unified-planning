#pragma once

#include "query/optimizer/plan/plan.h"
#include "query/var_id.h"
#include "graph_models/object_id.h"

// Base class for all Plan classes that correspond to TwoColumnBindingIter descendants.
// Provides common member variables and interface for two-column operations.
class TwoColumnPlan : public Plan {
public:
    // Constructor with variable col1
    TwoColumnPlan(VarId col1_var, VarId col2_var);

    // Constructor with constant col1
    TwoColumnPlan(ObjectId constant_col1_value, VarId col2_var);

    // Constructor with constant col2
    TwoColumnPlan(VarId col1_var, ObjectId constant_col2_value);

    // Copy constructor
    TwoColumnPlan(const TwoColumnPlan& other);

    virtual ~TwoColumnPlan() = default;

    // Getters for member variables
    VarId get_col1_var() const { return col1_var; }
    VarId get_col2_var() const { return col2_var; }
    ObjectId get_constant_col1_value() const { return constant_col1_value; }
    ObjectId get_constant_col2_value() const { return constant_col2_value; }
    bool get_has_constant_col1() const { return has_constant_col1; }
    bool get_has_constant_col2() const { return has_constant_col2; }

    // Common helper method for getting variables
    std::set<VarId> get_two_column_vars() const;

protected:
    VarId col1_var;
    VarId col2_var;
    ObjectId constant_col1_value;
    ObjectId constant_col2_value;
    bool has_constant_col1;
    bool has_constant_col2;
};