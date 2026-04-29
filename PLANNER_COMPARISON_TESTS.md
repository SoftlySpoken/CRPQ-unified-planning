# Planner Comparison Test Suite

This document describes the planner comparison test suite that has been set up to compare the custom planner against the original planner implementation in MillenniumDB.

## Overview

The test suite provides a framework for:
- **Correctness Testing**: Ensuring both planners produce identical results
- **Performance Comparison**: Measuring and comparing execution times
- **Easy Test Addition**: Simple structure for adding new test cases
- **Automated Integration**: Tests run as part of the standard test suite

## Quick Start

### Running Tests

```bash
# Run all planner comparison tests (recommended)
./scripts/run-planner-comparison-tests

# Run specific test categories
./scripts/run-planner-comparison-tests simple_patterns
./scripts/run-planner-comparison-tests complex_patterns performance_patterns

# Run with verbose output for debugging
./scripts/run-planner-comparison-tests --verbose

# Save detailed results to JSON file
./scripts/run-planner-comparison-tests --output results.json

# Run as part of full SPARQL test suite
./scripts/run-tests sparql
```

### Adding New Tests

1. **Add test data** to `tests/sparql/test_suites/internal/planner_comparison/test_data.ttl`
2. **Create query file** (e.g., `my_test.rq`) in one of:
   - `simple_patterns/` - Basic patterns and joins
   - `complex_patterns/` - Star patterns, chains, OPTIONAL clauses
   - `performance_patterns/` - Performance-focused queries
3. **Generate expected results** by running the query and saving output as `my_test.json`
4. **Verify** both planners produce the same results

### Example Test Creation

```bash
# Add data to test_data.ttl
echo ":newEntity :property 'value' ." >> tests/sparql/test_suites/internal/planner_comparison/test_data.ttl

# Create query file
cat > tests/sparql/test_suites/internal/planner_comparison/simple_patterns/my_test.rq << 'EOF'
PREFIX : <http://example.org/>
SELECT ?entity WHERE {
    ?entity :property 'value' .
}
EOF

# The test runner will automatically generate expected results or you can create them manually
```

## Test Structure

```
tests/sparql/test_suites/internal/planner_comparison/
├── README.md                           # Detailed documentation
├── test_data.ttl                       # Shared RDF test data
├── test_data_prefixes.txt             # Prefixes for test data
├── simple_patterns/                    # Basic pattern tests
│   ├── basic_triple.rq                # Single triple pattern
│   ├── basic_triple.json              # Expected result
│   ├── two_triple_join.rq             # Simple join
│   ├── two_triple_join.json           # Expected result
│   └── three_way_join.rq              # Multi-way join
├── complex_patterns/                   # Multi-pattern tests
│   ├── star_pattern.rq                # Star query with filter
│   ├── chain_pattern.rq               # Chain query pattern
│   └── optional_pattern.rq            # OPTIONAL clause test
├── performance_patterns/               # Performance-focused tests
│   ├── large_join.rq                  # Large multi-way join
│   └── selective_filter.rq            # High selectivity query
└── run_planner_comparison.py          # Detailed test runner script
```

## Test Data

The test data (`test_data.ttl`) includes:
- **Person data**: Names, ages, cities, companies, relationships
- **Company data**: Names, industries, locations, employee counts
- **Product data**: Names, prices, categories, manufacturers
- **Order data**: Customer-product relationships with dates
- **City data**: Names, populations, countries

This data supports various query patterns:
- Simple triple patterns and basic joins
- Star patterns (multiple properties of same entity)
- Chain patterns (traversing relationships)
- Complex filters and constraints
- Multi-way joins with different selectivities

## Example Test Cases

### Simple Pattern Test
```sparql
PREFIX : <http://example.org/>
SELECT ?name ?age WHERE {
    ?person :name ?name ;
            :age ?age .
}
```

### Star Pattern Test
```sparql
PREFIX : <http://example.org/>
SELECT ?person ?name ?age ?city ?company WHERE {
    ?person :name ?name ;
            :age ?age ;
            :city ?city ;
            :worksFor ?company .
    FILTER(?age > 30)
}
```

### Chain Pattern Test
```sparql
PREFIX : <http://example.org/>
SELECT ?customerName ?productName ?manufacturerName WHERE {
    ?order :customer ?customer ;
           :product ?product .
    ?customer :name ?customerName .
    ?product :productName ?productName ;
             :manufacturer ?manufacturer .
    ?manufacturer :companyName ?manufacturerName .
}
```

## Integration with Existing Tests

The planner comparison tests are integrated into the standard MillenniumDB test suite:

1. **Added to internal tests**: Listed in `tests/sparql/scripts/testing/options.py`
2. **Auto-discovery**: Tests are automatically discovered and run
3. **Standard interface**: Use existing test infrastructure and reporting
4. **CI/CD ready**: Tests run with `./scripts/run-tests sparql`

## Test Output

The test runner provides:
- **Pass/fail status** for each test
- **Execution times** for both planners
- **Performance comparison** (speedup ratios)
- **Detailed error messages** when tests fail
- **Summary statistics** across all tests

Example output:
```
Running test: basic_triple.rq
  ✓ PASS (0.012s original, 0.008s custom)

Running test: star_pattern.rq
  ✓ PASS (0.045s original, 0.032s custom)

Test Summary:
  Total tests: 8
  Passed: 8
  Failed: 0
  Success rate: 100.00%
  Avg time - Original: 0.031s
  Avg time - Custom: 0.024s
  Speedup: 1.29x
```

## Troubleshooting

### Tests Fail with "Results differ between planners"
- Check if both planners are using the same configuration
- Verify the custom planner implementation is complete
- Look for differences in join ordering or cost estimation

### Tests Fail with "doesn't match expected results"
- Regenerate expected results after verifying correctness
- Check if test data was modified
- Ensure prefixes are correctly defined

### Server startup fails
- Verify MillenniumDB is built (`cmake --build build/Release/`)
- Check that ports 8080 and 8081 are available
- Look at server logs for detailed error messages

## Performance Analysis

The test suite can help identify:
- **Query types** where the custom planner excels
- **Performance regressions** in specific patterns
- **Cost estimation accuracy** compared to actual execution times
- **Join ordering effectiveness** for different query shapes

For detailed performance analysis, use:
```bash
./scripts/run-planner-comparison-tests --output detailed_results.json --verbose
```

This creates a JSON file with per-query timing data that can be analyzed further.

## Future Enhancements

Potential improvements to the test suite:
- **Larger datasets** for more realistic performance testing
- **Query plan comparison** (not just results)
- **Memory usage measurement**
- **Concurrent query testing**
- **Statistical significance testing** for performance comparisons
- **Automated test case generation** from query logs

## Contributing

To contribute new test cases:
1. Follow the existing patterns for query complexity
2. Include diverse join patterns and selectivities
3. Add descriptive comments in query files
4. Verify tests pass on both planners
5. Update this documentation if adding new categories