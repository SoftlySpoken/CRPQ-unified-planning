#pragma once

#include "query/executor/binding_iter.h"
#include "storage/custom_buffer/two_column_store.h"

namespace CustomOps {

// Base class for all custom operators
class CustomOperatorBase : public BindingIter {
protected:
    // Pointer to the special two-column result store
    TwoColumnStore* result_store;

    // Common metadata for custom operators
    uint64_t estimated_cardinality;
    uint64_t actual_cardinality;

public:
    CustomOperatorBase() :
        result_store(nullptr),
        estimated_cardinality(0),
        actual_cardinality(0) {}

    virtual ~CustomOperatorBase() = default;

    // Set the two-column store for intermediate results
    void set_result_store(TwoColumnStore* store) {
        result_store = store;
    }

    // Get statistics for query optimization
    virtual uint64_t get_estimated_cardinality() const {
        return estimated_cardinality;
    }

    virtual uint64_t get_actual_cardinality() const {
        return actual_cardinality;
    }

    // Custom operator type identification
    enum class OperatorType {
        SJ,    // SJ operator
        TI,    // TI operator
        KC,    // KC operator
        MC,    // MC operator
        UNION  // Union operator
    };

    virtual OperatorType get_operator_type() const = 0;
};

} // namespace CustomOps