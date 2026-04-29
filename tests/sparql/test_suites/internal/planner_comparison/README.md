# Planner Comparison Tests

This directory contains correctness tests that compare the custom planner against the original planner implementation. These tests ensure that both planners produce identical results while allowing performance comparison.

## Directory Structure

```
planner_comparison/
├── README.md                           # This file
├── test_data.ttl                       # Shared RDF test data
├── test_data_prefixes.txt             # Optional prefixes for test data
├── simple_patterns/                    # Basic pattern tests
│   ├── basic_triple.rq                # Single triple pattern
│   ├── basic_triple.json              # Expected result
│   ├── two_triple_join.rq             # Simple two-triple join
│   ├── two_triple_join.json           # Expected result
│   └── ...
├── complex_patterns/                   # Multi-pattern tests
│   ├── star_pattern.rq                # Star query pattern
│   ├── star_pattern.json              # Expected result
│   ├── chain_pattern.rq               # Chain query pattern
│   ├── chain_pattern.json             # Expected result
│   ├── optional_pattern.rq            # OPTIONAL clause test
│   ├── optional_pattern.json          # Expected result
│   └── ...
├── performance_patterns/               # Performance-focused tests
│   ├── large_join.rq                  # Large multi-way join
│   ├── large_join.json                # Expected result
│   ├── selective_filter.rq            # High selectivity query
│   ├── selective_filter.json          # Expected result
│   └── ...
└── run_planner_comparison.py          # Test runner script
```

## Test Data Format

### RDF Data (test_data.ttl)
- Contains representative RDF triples that exercise different join patterns
- Includes both selective and non-selective patterns
- Tests various cardinalities and data distributions

### SPARQL Queries (.rq files)
- Standard SPARQL queries focusing on different pattern types
- Each query should exercise specific planner decisions
- Queries are designed to reveal differences in join ordering and cost estimation

### Expected Results (.json files)
- SPARQL JSON format containing expected query results
- Both planners should produce identical results (correctness requirement)
- Results serve as the baseline for correctness verification

## Running Tests

### Using the Test Runner Script
```bash
# Run all planner comparison tests
python3 tests/sparql/test_suites/internal/planner_comparison/run_planner_comparison.py

# Run specific test category
python3 tests/sparql/test_suites/internal/planner_comparison/run_planner_comparison.py simple_patterns

# Run with detailed output
python3 tests/sparql/test_suites/internal/planner_comparison/run_planner_comparison.py --verbose
```

### Using Standard Test Runner
```bash
# These tests are also included in the standard SPARQL test suite
./scripts/run-tests sparql
```

## Adding New Tests

1. **Create Test Data**: Add relevant RDF triples to `test_data.ttl`
2. **Write Query**: Create a `.rq` file with your SPARQL query
3. **Generate Expected Result**:
   - Run the query against the original planner
   - Save the result as a `.json` file
4. **Verify**: Run the test to ensure both planners produce the same result

### Example Test Creation

```bash
# 1. Add test data to test_data.ttl
echo ":person1 :name 'Alice' ; :age 25 ." >> test_data.ttl

# 2. Create query file
cat > simple_patterns/person_query.rq << 'EOF'
PREFIX : <http://example.org/>
SELECT ?name ?age WHERE {
    ?person :name ?name ;
            :age ?age .
}
EOF

# 3. Generate expected result (run with original planner)
# This will be automated by the test runner

# 4. Verify both planners produce the same result
python3 run_planner_comparison.py simple_patterns/person_query
```

## Test Categories

### Simple Patterns
- Single triple patterns
- Two-triple joins
- Basic graph patterns
- Simple filters

### Complex Patterns
- Star patterns (multiple properties of same subject)
- Chain patterns (subject-object chains)
- Optional patterns (OPTIONAL clauses)
- Union patterns
- Multi-way joins

### Performance Patterns
- Large joins with different selectivities
- Queries with varying join orders
- Complex filters and constraints
- Patterns that stress cost estimation

## Integration with CI/CD

These tests are integrated into the standard test suite and will run automatically with:
```bash
./scripts/run-tests sparql
```

Any test failures indicate either:
1. A correctness bug in the custom planner
2. A change in expected behavior that needs verification
3. Test data or expected results that need updating