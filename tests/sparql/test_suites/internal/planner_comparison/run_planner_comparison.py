#!/usr/bin/env python3
"""
Planner Comparison Test Runner

This script runs correctness tests that compare the custom planner against the original planner.
It ensures both planners produce identical results while optionally collecting performance metrics.

The script supports two test modes:
1. With expected results: Query files (.rq) with corresponding expected result files (.json)
   - Tests pass when both planners match each other AND match expected results
2. Without expected results: Query files (.rq) only, no expected result files
   - Tests pass when both planners produce identical results (planner agreement mode)
"""

import os
import sys
import json
import time
import argparse
import subprocess
import tempfile
import shutil
from pathlib import Path
from typing import List, Dict, Any, Optional, Tuple

# Add the parent test directory to path for imports
sys.path.append(str(Path(__file__).parent.parent.parent / "scripts"))

class PlannerComparisonRunner:
    """Runs planner comparison tests with configurable options."""

    def __init__(self,
                 test_dir: Path,
                 mdb_import_path: str = "build/Debug/bin/mdb-import",
                 mdb_server_path: str = "build/Debug/bin/mdb-server",
                 port_original: int = 8080,
                 port_custom: int = 8081):
        self.test_dir = test_dir
        self.mdb_import_path = mdb_import_path
        self.mdb_server_path = mdb_server_path
        self.port_original = port_original
        self.port_custom = port_custom
        self.temp_dirs = []

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.cleanup()

    def cleanup(self):
        """Clean up temporary directories and stop any running servers."""
        for temp_dir in self.temp_dirs:
            if temp_dir.exists():
                shutil.rmtree(temp_dir)
        self.temp_dirs.clear()

        # Try to stop servers if they're running
        self._stop_server(self.port_original)
        self._stop_server(self.port_custom)

    def _stop_server(self, port: int):
        """Stop server running on given port."""
        try:
            subprocess.run(["pkill", "-f", f"mdb-server.*--port {port}"],
                         capture_output=True, timeout=5)
        except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
            pass  # Server might not be running

    def create_test_database(self, planner_type: str) -> Path:
        """Create a test database with the specified planner configuration."""
        temp_dir = Path(f"/tmp/planner_test_{planner_type}_{int(time.time() * 1000000) % 1000000:06d}")
        self.temp_dirs.append(temp_dir)

        # Import test data
        data_file = self.test_dir / "test_data.ttl"
        prefixes_file = self.test_dir / "test_data_prefixes.txt"

        import_cmd = [self.mdb_import_path, str(data_file), str(temp_dir)]
        if prefixes_file.exists():
            import_cmd.extend(["--prefixes", str(prefixes_file)])

        result = subprocess.run(import_cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(f"Failed to import test data: {result.stderr}")

        return temp_dir

    def start_server(self, db_path: Path, port: int, planner_type: str) -> subprocess.Popen:
        """Start server with specified planner configuration."""
        cmd = []
        if planner_type == "original":
            cmd = [
                self.mdb_server_path,
                str(db_path),
                "--port", str(port),
            ]
        elif planner_type == "custom":
            cmd = [
                self.mdb_server_path,
                str(db_path),
                "--port", str(port),
                "--custom-planner",
                "--custom-planner-verbose",
                "--custom-planner-output", "planner_metrics.csv"
            ]

        print(cmd)
        return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        # subprocess.Popen(cmd).wait()
        # return True

    def wait_for_server(self, port: int, timeout: int = 30) -> bool:
        """Wait for server to be ready."""
        import urllib.request
        import urllib.error

        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                sparql_query = "SELECT * WHERE { ?s ?p ?o } LIMIT 1"
                params = urllib.parse.urlencode({'query': sparql_query})
                url = f"http://localhost:{port}/sparql?{params}"
                response = urllib.request.urlopen(url, timeout=1)
                return True
            except urllib.error.URLError:
                time.sleep(0.5)
        return False

    def execute_query(self, query: str, port: int) -> Tuple[Dict[str, Any], float]:
        """Execute SPARQL query and return results with timing."""
        import urllib.request
        import urllib.parse

        # Prepare the query request
        params = urllib.parse.urlencode({
            'query': query,
            'format': 'json'
        })

        url = f"http://localhost:{port}/sparql"

        start_time = time.time()
        try:
            with urllib.request.urlopen(f"{url}?{params}", timeout=60) as response:
                result_data = json.loads(response.read().decode('utf-8'))
                execution_time = time.time() - start_time
                return result_data, execution_time
        except Exception as e:
            execution_time = time.time() - start_time
            raise RuntimeError(f"Query execution failed: {e}") from e

    def normalize_results(self, results: Dict[str, Any]) -> Dict[str, Any]:
        """Normalize results for comparison (sort bindings, handle blank nodes)."""
        if 'results' not in results or 'bindings' not in results['results']:
            return results

        # Sort bindings for consistent comparison
        bindings = results['results']['bindings']

        # Create a sort key based on all variable values
        def sort_key(binding):
            return tuple(sorted([
                (var, binding[var].get('value', ''), binding[var].get('type', ''))
                for var in binding.keys()
            ]))

        try:
            sorted_bindings = sorted(bindings, key=sort_key)
            normalized = results.copy()
            normalized['results'] = {'bindings': sorted_bindings}
            if 'head' in results:
                normalized['head'] = results['head']
            return normalized
        except (KeyError, TypeError):
            # If sorting fails, return original results
            return results

    def compare_results(self, result1: Dict[str, Any], result2: Dict[str, Any]) -> Tuple[bool, str]:
        """Compare two result sets for equality."""
        norm1 = self.normalize_results(result1)
        norm2 = self.normalize_results(result2)

        if norm1 == norm2:
            return True, "Results are identical"

        # Provide detailed comparison
        diff_msg = []

        # Compare variable lists
        vars1 = set(norm1.get('head', {}).get('vars', []))
        vars2 = set(norm2.get('head', {}).get('vars', []))
        if vars1 != vars2:
            diff_msg.append(f"Variable mismatch: {vars1} vs {vars2}")

        # Compare result counts
        bindings1 = norm1.get('results', {}).get('bindings', [])
        bindings2 = norm2.get('results', {}).get('bindings', [])
        if len(bindings1) != len(bindings2):
            diff_msg.append(f"Result count mismatch: {len(bindings1)} vs {len(bindings2)}")

        # Find different bindings
        set1 = {json.dumps(binding, sort_keys=True) for binding in bindings1}
        set2 = {json.dumps(binding, sort_keys=True) for binding in bindings2}

        only_in_1 = set1 - set2
        only_in_2 = set2 - set1

        if only_in_1:
            diff_msg.append(f"Only in result 1: {list(only_in_1)[:3]}...")
        if only_in_2:
            diff_msg.append(f"Only in result 2: {list(only_in_2)[:3]}...")

        return False, "; ".join(diff_msg)

    def run_test_case_single_planner(self, query_file: Path, expected_file: Optional[Path],
                                    port: int, planner_name: str) -> Dict[str, Any]:
        """Run a single test case on one planner."""
        print(f"Running test on {planner_name} planner: {query_file.name}")

        # Read query
        with open(query_file, 'r') as f:
            query = f.read()

        # Read expected results if available
        expected_results = None
        if expected_file is not None:
            with open(expected_file, 'r') as f:
                expected_results = json.load(f)

        test_result = {
            'test_name': query_file.stem,
            'query_file': str(query_file.relative_to(self.test_dir)),
            'planner': planner_name,
            'has_expected_results': expected_results is not None,
            'success': False,
            'error': None,
            'execution_time': None,
            'results': None,
            'expected_match': None  # None if no expected results, True/False otherwise
        }

        try:
            # Execute query on the specified planner
            results, execution_time = self.execute_query(query, port)

            test_result['execution_time'] = execution_time
            test_result['results'] = results

            # Compare against expected results if available
            if expected_results is not None:
                expected_match, match_msg = self.compare_results(results, expected_results)
                test_result['expected_match'] = expected_match
                if expected_match:
                    test_result['success'] = True
                    print(f"  ✓ PASS ({execution_time:.3f}s)")
                else:
                    test_result['error'] = f"Results don't match expected: {match_msg}"
                    print(f"  ✗ FAIL: {test_result['error']}")
            else:
                # No expected results - just mark as successful execution
                test_result['success'] = True
                test_result['expected_match'] = None
                print(f"  ✓ EXECUTED ({execution_time:.3f}s) [no expected results]")

        except Exception as e:
            test_result['error'] = str(e)
            print(f"  ✗ ERROR: {e}")

        return test_result

    def find_test_cases(self, pattern_dirs: Optional[List[str]] = None) -> List[Tuple[Path, Optional[Path]]]:
        """Find all test cases (query files with optional expected result files)."""
        test_cases = []

        if pattern_dirs is None:
            pattern_dirs = ["simple_patterns", "complex_patterns", "performance_patterns", "path_patterns"]

        for pattern_dir in pattern_dirs:
            pattern_path = self.test_dir / pattern_dir
            if not pattern_path.exists():
                continue

            for query_file in pattern_path.glob("*.rq"):
                expected_file = query_file.with_suffix(".json")
                # Include test case regardless of whether expected file exists
                test_cases.append((query_file, expected_file if expected_file.exists() else None))

        return sorted(test_cases)

    def run_tests(self, pattern_dirs: Optional[List[str]] = None, verbose: bool = False) -> Dict[str, Any]:
        """Run all planner comparison tests sequentially."""
        print("Setting up test databases...")

        # Create databases for both planners
        original_db = self.create_test_database("original")
        custom_db = self.create_test_database("custom")

        # Find test cases
        test_cases = self.find_test_cases(pattern_dirs)
        if not test_cases:
            print("No test cases found!")
            return {'success': False, 'error': 'No test cases found'}

        print(f"Found {len(test_cases)} test cases")

        # Data structures to store results
        original_results = []
        custom_results = []

        # Phase 1: Run tests with original planner
        print(f"\n=== Phase 1: Running tests with ORIGINAL planner ===")
        print(f"Starting original planner server on port {self.port_original}...")
        original_server = self.start_server(original_db, self.port_original, "original")

        try:
            # Wait for original server to be ready
            if not self.wait_for_server(self.port_original):
                raise RuntimeError(f"Original server failed to start on port {self.port_original}")

            print("Original server ready. Running tests...\n")

            for query_file, expected_file in test_cases:
                test_result = self.run_test_case_single_planner(query_file, expected_file,
                                                              self.port_original, "original")
                original_results.append(test_result)
                if not test_result['success'] and verbose:
                    print(f"    Details: {test_result['error']}")

        finally:
            # Stop original server
            print(f"\nStopping original planner server...")
            original_server.terminate()
            try:
                original_server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                original_server.kill()
            # Give some time for port to be released
            time.sleep(2)

        # Phase 2: Run tests with custom planner
        print(f"\n=== Phase 2: Running tests with CUSTOM planner ===")
        print(f"Starting custom planner server on port {self.port_custom}...")
        custom_server = self.start_server(custom_db, self.port_custom, "custom")

        try:
            # Wait for custom server to be ready
            if not self.wait_for_server(self.port_custom):
                raise RuntimeError(f"Custom server failed to start on port {self.port_custom}")

            print("Custom server ready. Running tests...\n")

            for query_file, expected_file in test_cases:
                test_result = self.run_test_case_single_planner(query_file, expected_file,
                                                              self.port_custom, "custom")
                custom_results.append(test_result)
                if not test_result['success'] and verbose:
                    print(f"    Details: {test_result['error']}")

        finally:
            # Stop custom server
            print(f"\nStopping custom planner server...")
            custom_server.terminate()
            try:
                custom_server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                custom_server.kill()

        # Phase 3: Compare results and generate summary
        print(f"\n=== Phase 3: Comparing results ===")

        comparison_results = []
        passed_tests = 0
        failed_tests = 0

        for i, (query_file, expected_file) in enumerate(test_cases):
            orig_result = original_results[i]
            custom_result = custom_results[i]

            # Create combined result for this test case
            combined_result = {
                'test_name': orig_result['test_name'],
                'query_file': orig_result['query_file'],
                'has_expected_results': orig_result['has_expected_results'],
                'original_success': orig_result['success'],
                'custom_success': custom_result['success'],
                'original_time': orig_result['execution_time'],
                'custom_time': custom_result['execution_time'],
                'original_error': orig_result['error'],
                'custom_error': custom_result['error'],
                'results_match': False,
                'success': False,
                'error': None
            }

            # Compare results between planners (only if both succeeded)
            if orig_result['success'] and custom_result['success'] and orig_result['results'] and custom_result['results']:
                results_match, match_msg = self.compare_results(orig_result['results'], custom_result['results'])
                combined_result['results_match'] = results_match

                if results_match:
                    combined_result['success'] = True
                    passed_tests += 1
                    orig_time = orig_result['execution_time'] or 0
                    custom_time = custom_result['execution_time'] or 0
                    status_msg = f"({orig_time:.3f}s original, {custom_time:.3f}s custom)"
                    if not orig_result['has_expected_results']:
                        status_msg += " [no expected results - planners agree]"
                    print(f"  ✓ PASS {orig_result['test_name']}: {status_msg}")
                else:
                    combined_result['error'] = f"Results differ between planners: {match_msg}"
                    failed_tests += 1
                    print(f"  ✗ FAIL {orig_result['test_name']}: {combined_result['error']}")
            else:
                # At least one planner failed
                error_parts = []
                if not orig_result['success']:
                    error_parts.append(f"Original planner failed: {orig_result['error']}")
                if not custom_result['success']:
                    error_parts.append(f"Custom planner failed: {custom_result['error']}")
                combined_result['error'] = "; ".join(error_parts)
                failed_tests += 1
                print(f"  ✗ FAIL {orig_result['test_name']}: {combined_result['error']}")

            comparison_results.append(combined_result)

        # Generate final summary
        tests_with_expected = [r for r in comparison_results if r['has_expected_results']]
        tests_without_expected = [r for r in comparison_results if not r['has_expected_results']]

        successful_tests = [r for r in comparison_results if r['success']]
        avg_original_time = sum(r['original_time'] for r in successful_tests if r['original_time']) / len(successful_tests) if successful_tests else 0
        avg_custom_time = sum(r['custom_time'] for r in successful_tests if r['custom_time']) / len(successful_tests) if successful_tests else 0

        results = {
            'total_tests': len(test_cases),
            'passed_tests': passed_tests,
            'failed_tests': failed_tests,
            'test_results': comparison_results,
            'original_results': original_results,
            'custom_results': custom_results,
            'summary': {
                'success_rate': passed_tests / len(test_cases) if test_cases else 0,
                'tests_with_expected_results': len(tests_with_expected),
                'tests_without_expected_results': len(tests_without_expected),
                'avg_original_time': avg_original_time,
                'avg_custom_time': avg_custom_time
            }
        }

        print(f"\nFinal Test Summary:")
        print(f"  Total tests: {results['total_tests']}")
        print(f"    With expected results: {results['summary']['tests_with_expected_results']}")
        print(f"    Without expected results: {results['summary']['tests_without_expected_results']}")
        print(f"  Passed: {results['passed_tests']}")
        print(f"  Failed: {results['failed_tests']}")
        print(f"  Success rate: {results['summary']['success_rate']:.2%}")
        if results['summary']['avg_original_time'] > 0:
            print(f"  Avg time - Original: {results['summary']['avg_original_time']:.3f}s")
            print(f"  Avg time - Custom: {results['summary']['avg_custom_time']:.3f}s")
            speedup = results['summary']['avg_original_time'] / results['summary']['avg_custom_time'] if results['summary']['avg_custom_time'] > 0 else 1
            print(f"  Speedup: {speedup:.2f}x")

        return results


def main():
    """Main entry point for the test runner."""
    parser = argparse.ArgumentParser(description="Run planner comparison tests")
    parser.add_argument("patterns", nargs="*",
                       help="Pattern directories to test (default: all)")
    parser.add_argument("--verbose", "-v", action="store_true",
                       help="Verbose output with detailed error messages")
    parser.add_argument("--port-original", type=int, default=8080,
                       help="Port for original planner server (default: 8080)")
    parser.add_argument("--port-custom", type=int, default=8081,
                       help="Port for custom planner server (default: 8081)")
    parser.add_argument("--output", "-o", type=str,
                       help="Output file for detailed results (JSON format)")

    args = parser.parse_args()

    # Find test directory
    test_dir = Path(__file__).parent
    if not (test_dir / "test_data.ttl").exists():
        print(f"Error: Test data not found in {test_dir}")
        sys.exit(1)

    # Determine pattern directories to test
    pattern_dirs = args.patterns if args.patterns else None

    try:
        with PlannerComparisonRunner(test_dir,
                                   port_original=args.port_original,
                                   port_custom=args.port_custom) as runner:
            results = runner.run_tests(pattern_dirs, verbose=args.verbose)

            # Save detailed results if requested
            if args.output:
                with open(args.output, 'w') as f:
                    json.dump(results, f, indent=2)
                print(f"\nDetailed results saved to: {args.output}")

            # Exit with error code if tests failed
            if results['failed_tests'] > 0:
                sys.exit(1)

    except KeyboardInterrupt:
        print("\nTest execution interrupted by user")
        sys.exit(130)
    except Exception as e:
        print(f"Error running tests: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()