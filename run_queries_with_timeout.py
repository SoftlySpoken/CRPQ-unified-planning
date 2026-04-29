#!/usr/bin/env python3
"""
Query Runner with Timeout and Server Restart

This script runs SPARQL queries from a file with timeout protection.
If a query exceeds the specified timeout, it kills the MDB server,
restarts it, and continues with the next query.

Features:
- Configurable timeout (default: 60000ms)
- Automatic server restart on timeout with configurable retry attempts
- Query skipping on failure
- Progress tracking
- Detailed logging
- Configurable build type (Debug/Release)

Usage:
    python3 run_queries_with_timeout.py [options]

Examples:
    # Use Debug build (default)
    python3 run_queries_with_timeout.py

    # Use Release build
    python3 run_queries_with_timeout.py --build-type Release

    # Custom server path (overrides build type)
    python3 run_queries_with_timeout.py --server /path/to/mdb-server

    # Set 5 retry attempts for server restart (default is 3)
    python3 run_queries_with_timeout.py --retry-attempts 5
"""

import os
import sys
import time
import argparse
import subprocess
import json
import threading
import re
from pathlib import Path
from typing import Optional, List, Dict, Any
import urllib.request
import urllib.parse
import urllib.error

class TimeoutQueryRunner:
    """Runs queries with timeout protection and server management."""

    def __init__(self,
                 db_path: str = "wikidata-prerelease-new.db",
                 queries_file: str = "c2rpqs-connected.txt",
                 mdb_server_path: Optional[str] = None,
                 port: int = 8080,
                 timeout_ms: int = 60000,
                 temp_query_file: str = "temp_query.rq",
                 use_custom_planner: bool = False,
                 build_type: str = "Debug",
                 retry_attempts: int = 3):

        self.db_path = Path(db_path)
        self.queries_file = Path(queries_file)

        # Set default server path based on build type if not specified
        if mdb_server_path is None:
            self.mdb_server_path = f"build/{build_type}/bin/mdb-server"
        else:
            self.mdb_server_path = mdb_server_path
        self.port = port
        self.timeout_ms = timeout_ms
        self.timeout_seconds = timeout_ms / 1000.0
        self.temp_query_file = temp_query_file
        self.use_custom_planner = use_custom_planner
        self.retry_attempts = retry_attempts

        # State tracking
        self.server_process: Optional[subprocess.Popen] = None
        self.current_query_number = 0
        self.successful_queries = 0
        self.failed_queries = 0
        self.timeout_queries = 0
        self.server_restarts = 0

        # Server output monitoring
        self.server_output_lock = threading.Lock()
        self.last_execution_duration: Optional[float] = None
        self.last_optimizer_duration: Optional[float] = None

        # Results storage
        self.results: List[Dict[str, Any]] = []

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.cleanup()

    def cleanup(self):
        """Clean up server and temporary files."""
        self.stop_server()
        if os.path.exists(self.temp_query_file):
            try:
                os.remove(self.temp_query_file)
            except OSError:
                pass

    def stop_server(self):
        """Stop the MDB server if running."""
        if self.server_process:
            try:
                print(f"Stopping MDB server (PID: {self.server_process.pid})...", flush=True)

                # Try graceful termination first
                self.server_process.terminate()
                try:
                    self.server_process.wait(timeout=5)
                    print("Server terminated gracefully", flush=True)
                except subprocess.TimeoutExpired:
                    # Force kill if graceful termination fails
                    print("Server not responding, force killing...", flush=True)
                    self.server_process.kill()
                    self.server_process.wait()
                    print("Server force killed", flush=True)

            except Exception as e:
                print(f"Error stopping server: {e}", flush=True)

            self.server_process = None

        # Also try to kill any remaining server processes on this port
        try:
            subprocess.run(["pkill", "-f", f"mdb-server.*--port {self.port}"],
                          capture_output=True, timeout=5)
        except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
            pass

    def monitor_server_output(self):
        """Monitor server output to capture execution duration."""
        if not self.server_process or not self.server_process.stdout:
            return

        def output_reader():
            try:
                for line in iter(self.server_process.stdout.readline, b''):
                    if line:
                        print("line:", line)
                        line_str = line.decode('utf-8', errors='ignore').strip()

                        # Look for "Execution duration :" pattern
                        match = re.search(r'Execution duration\s*:\s*(\d+(?:\.\d+)?)\s*ms', line_str)
                        if match:
                            duration_ms = float(match.group(1))
                            duration_seconds = duration_ms / 1000.0

                            with self.server_output_lock:
                                self.last_execution_duration = duration_seconds

                            print(f"  📊 Server execution duration: {duration_seconds:.3f}s", flush=True)

                        # Look for "Optimizer duration :" pattern
                        match = re.search(r'Optimizer duration\s*:\s*(\d+(?:\.\d+)?)\s*ms', line_str)
                        if match:
                            duration_ms = float(match.group(1))
                            duration_seconds = duration_ms / 1000.0

                            with self.server_output_lock:
                                self.last_optimizer_duration = duration_seconds

                            print(f"  🔧 Server optimizer duration: {duration_seconds:.3f}s", flush=True)

            except Exception as e:
                # Silently handle output reading errors
                pass

        # Start output monitoring thread
        output_thread = threading.Thread(target=output_reader, daemon=True)
        output_thread.start()

    def start_server(self) -> bool:
        """Start the MDB server."""
        print(f"Starting MDB server on port {self.port} with database: {self.db_path}", flush=True)

        # Ensure the database exists
        if not self.db_path.exists():
            print(f"Error: Database not found at {self.db_path}", flush=True)
            return False

        try:
            # Start server process
            cmd = [
                self.mdb_server_path,
                str(self.db_path),
                "--port", str(self.port)
            ]

            # Add custom planner flags if enabled
            if self.use_custom_planner:
                cmd.extend(["--custom-planner", "--custom-planner-verbose"])

            print(f"Running command: {' '.join(cmd)}", flush=True)
            self.server_process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                preexec_fn=os.setsid  # Create new process group for easier cleanup
            )

            # Wait for server to be ready
            if self.wait_for_server():
                print(f"Server started successfully (PID: {self.server_process.pid})", flush=True)
                sys.stdout.flush()

                # Start monitoring server output for execution duration
                self.monitor_server_output()

                return True
            else:
                print("Server failed to start properly", flush=True)
                self.stop_server()
                return False

        except Exception as e:
            print(f"Error starting server: {e}", flush=True)
            return False

    def wait_for_server(self, timeout: int = 30) -> bool:
        """Wait for server to be ready to accept queries."""
        print("Waiting for server to be ready...", flush=True)
        start_time = time.time()

        while time.time() - start_time < timeout:
            try:
                # Try a simple test query
                test_query = "SELECT * WHERE { ?s <http://www.wikidata.org/prop/direct/P31> ?o } LIMIT 1"
                params = urllib.parse.urlencode({'query': test_query})
                url = f"http://localhost:{self.port}/sparql?{params}"

                response = urllib.request.urlopen(url, timeout=2)
                if response.status == 200:
                    print("Server is ready!", flush=True)
                    sys.stdout.flush()
                    return True

            except (urllib.error.URLError, urllib.error.HTTPError, OSError):
                time.sleep(1)

        return False

    def restart_server(self) -> bool:
        """Restart the MDB server with retry logic."""
        print("Restarting MDB server...", flush=True)
        self.server_restarts += 1

        self.stop_server()
        time.sleep(2)  # Give some time for cleanup

        # Try to start server with retry attempts
        for attempt in range(1, self.retry_attempts + 1):
            print(f"Server restart attempt {attempt}/{self.retry_attempts}...", flush=True)

            if self.start_server():
                print(f"Server restart successful on attempt {attempt}", flush=True)
                return True
            else:
                print(f"Server restart attempt {attempt} failed", flush=True)
                if attempt < self.retry_attempts:
                    print(f"Waiting 5 seconds before next attempt...", flush=True)
                    time.sleep(5)  # Wait before next retry
                else:
                    print(f"All {self.retry_attempts} restart attempts failed", flush=True)

        return False

    def execute_query_with_timeout(self, query: str) -> Dict[str, Any]:
        """Execute a query with active timeout protection."""
        result = {
            'query_number': self.current_query_number,
            'query': query[:200] + "..." if len(query) > 200 else query,
            'success': False,
            'timeout': False,
            'server_restart': False,
            'execution_time': None,
            'server_execution_duration': None,
            'server_optimizer_duration': None,
            'result_count': None,
            'error': None
        }

        # Shared state for the timeout mechanism
        query_completed = threading.Event()
        timeout_triggered = threading.Event()
        query_result = {'data': None, 'error': None}

        def query_worker():
            """Worker thread that executes the query."""
            try:
                # Prepare query request
                params = urllib.parse.urlencode({
                    'query': query,
                    'format': 'json'
                })
                url = f"http://localhost:{self.port}/sparql?{params}"

                # Execute query without timeout (we handle it actively)
                with urllib.request.urlopen(url, timeout=None) as response:
                    result_data = json.loads(response.read().decode('utf-8'))
                    query_result['data'] = result_data

            except Exception as e:
                query_result['error'] = str(e)
            finally:
                query_completed.set()

        def timeout_monitor():
            """Monitor thread that triggers timeout after specified duration."""
            if not query_completed.wait(timeout=self.timeout_seconds):
                timeout_triggered.set()
                print(f"  ⏰ Active timeout triggered after {self.timeout_seconds}s", flush=True)

        print(f"Executing query {self.current_query_number} (timeout: {self.timeout_seconds}s)...", flush=True)
        start_time = time.time()

        # Start query and timeout threads
        query_thread = threading.Thread(target=query_worker, daemon=True)
        timeout_thread = threading.Thread(target=timeout_monitor, daemon=True)

        query_thread.start()
        timeout_thread.start()

        # Wait for either completion or timeout
        while query_thread.is_alive():
            if timeout_triggered.is_set():
                # Timeout occurred - force server restart
                execution_time = time.time() - start_time
                result['timeout'] = True
                result['execution_time'] = execution_time
                result['error'] = f"Active timeout after {execution_time:.3f}s"

                print(f"  🔄 Killing server due to timeout...", flush=True)

                # Force restart the server
                if self.restart_server():
                    result['server_restart'] = True
                    print("  ♻  Server restarted successfully after timeout", flush=True)
                else:
                    result['error'] += f"; Failed to restart server after timeout (tried {self.retry_attempts} attempts)"
                    print(f"  ✗ Failed to restart server after timeout (tried {self.retry_attempts} attempts)", flush=True)

                break

            time.sleep(0.1)  # Small sleep to avoid busy waiting

        # Wait for threads to finish
        timeout_thread.join(timeout=1)

        # Check if query completed successfully (not timeout)
        if not timeout_triggered.is_set() and query_completed.is_set():
            execution_time = time.time() - start_time
            result['execution_time'] = execution_time

            # Capture server execution duration if available
            with self.server_output_lock:
                if self.last_execution_duration is not None:
                    result['server_execution_duration'] = self.last_execution_duration
                    self.last_execution_duration = None  # Reset for next query

                if self.last_optimizer_duration is not None:
                    result['server_optimizer_duration'] = self.last_optimizer_duration
                    self.last_optimizer_duration = None  # Reset for next query

            if query_result['error']:
                result['error'] = f"Query failed: {query_result['error']}"
                print(f"  ✗ Failed ({execution_time:.3f}s): {query_result['error']}", flush=True)
            else:
                # Process successful result
                try:
                    result_data = query_result['data']
                    result['success'] = True

                    # Count results
                    if 'results' in result_data and 'bindings' in result_data['results']:
                        result['result_count'] = len(result_data['results']['bindings'])
                    elif 'boolean' in result_data:
                        result['result_count'] = 1
                    else:
                        result['result_count'] = 0

                    # Enhanced success message with server duration if available
                    success_msg = f"  ✓ Success ({execution_time:.3f}s"
                    if result['server_execution_duration'] is not None:
                        success_msg += f", server: {result['server_execution_duration']:.3f}s"
                    if result['server_optimizer_duration'] is not None:
                        success_msg += f", optimizer: {result['server_optimizer_duration']:.3f}s"
                    success_msg += f", {result['result_count']} results)"
                    print(success_msg, flush=True)

                except Exception as e:
                    result['error'] = f"Failed to parse query result: {e}"
                    print(f"  ✗ Parse error ({execution_time:.3f}s): {e}", flush=True)

        return result

    def parse_query_line(self, line: str) -> Optional[str]:
        """Parse a line from the queries file into a SPARQL query."""
        line = line.strip()
        if not line or not ',' in line:
            return None

        # Extract query part after the comma (remove query number)
        query_part = line[line.find(',') + 1:]

        # Wrap in SELECT statement
        query = f"SELECT * WHERE {{ {query_part} }}"
        print(query, flush=True)

        return query

    def run_all_queries(self) -> Dict[str, Any]:
        """Run all queries from the queries file."""
        print(f"Starting query execution from: {self.queries_file}", flush=True)
        print(f"Database: {self.db_path}", flush=True)
        print(f"Timeout: {self.timeout_ms}ms", flush=True)
        print(f"Port: {self.port}", flush=True)
        print("-" * 60, flush=True)
        sys.stdout.flush()

        # Start initial server
        if not self.start_server():
            return {
                'success': False,
                'error': 'Failed to start initial MDB server',
                'results': []
            }

        try:
            with open(self.queries_file, 'r') as f:
                for line_number, line in enumerate(f, 1):
                    self.current_query_number = line_number

                    # Parse query from line
                    query = self.parse_query_line(line)
                    if query is None:
                        print(f"Skipping invalid line {line_number}", flush=True)
                        continue

                    # Execute query
                    result = self.execute_query_with_timeout(query)
                    self.results.append(result)
                    sys.stdout.flush()  # Force flush after each query

                    # Update counters
                    if result['success']:
                        self.successful_queries += 1
                    elif result['timeout']:
                        self.timeout_queries += 1
                    else:
                        self.failed_queries += 1

                    # Print progress every 10 queries
                    if line_number % 10 == 0:
                        print(f"\nProgress: {line_number} queries processed", flush=True)
                        print(f"  Success: {self.successful_queries}, Failed: {self.failed_queries}, Timeout: {self.timeout_queries}", flush=True)
                        print(f"  Server restarts: {self.server_restarts}\n", flush=True)
                        sys.stdout.flush()  # Force flush after progress report

        except FileNotFoundError:
            return {
                'success': False,
                'error': f'Query file not found: {self.queries_file}',
                'results': self.results
            }
        except Exception as e:
            return {
                'success': False,
                'error': f'Unexpected error: {e}',
                'results': self.results
            }

        # Generate final summary
        total_queries = len(self.results)
        success_rate = self.successful_queries / total_queries if total_queries > 0 else 0

        successful_results = [r for r in self.results if r['success'] and r['execution_time']]
        avg_execution_time = sum(r['execution_time'] for r in successful_results) / len(successful_results) if successful_results else 0

        # Calculate server execution duration average (only for successful queries with non-null server execution duration)
        successful_server_exec_results = [r for r in self.results if r['success'] and r['server_execution_duration'] is not None]
        avg_server_execution_duration = sum(r['server_execution_duration'] for r in successful_server_exec_results) / len(successful_server_exec_results) if successful_server_exec_results else None

        # Calculate server optimizer duration average (only for successful queries with non-null server optimizer duration)
        successful_server_opt_results = [r for r in self.results if r['success'] and r['server_optimizer_duration'] is not None]
        avg_server_optimizer_duration = sum(r['server_optimizer_duration'] for r in successful_server_opt_results) / len(successful_server_opt_results) if successful_server_opt_results else None

        # Calculate combined server duration average (only for successful queries with both non-null durations)
        successful_combined_results = [r for r in self.results if r['success'] and r['server_execution_duration'] is not None and r['server_optimizer_duration'] is not None]
        avg_server_combined_duration = sum(r['server_execution_duration'] + r['server_optimizer_duration'] for r in successful_combined_results) / len(successful_combined_results) if successful_combined_results else None

        summary = {
            'success': True,
            'total_queries': total_queries,
            'successful_queries': self.successful_queries,
            'failed_queries': self.failed_queries,
            'timeout_queries': self.timeout_queries,
            'server_restarts': self.server_restarts,
            'success_rate': success_rate,
            'avg_execution_time': avg_execution_time,
            'avg_server_execution_duration': avg_server_execution_duration,
            'avg_server_optimizer_duration': avg_server_optimizer_duration,
            'avg_server_combined_duration': avg_server_combined_duration,
            'results': self.results
        }

        print("\n" + "=" * 60, flush=True)
        print("FINAL SUMMARY", flush=True)
        print("=" * 60, flush=True)
        print(f"Total queries processed: {total_queries}", flush=True)
        print(f"Successful: {self.successful_queries} ({success_rate:.2%})", flush=True)
        print(f"Failed: {self.failed_queries}", flush=True)
        print(f"Timeouts: {self.timeout_queries}", flush=True)
        print(f"Server restarts: {self.server_restarts}", flush=True)
        if avg_execution_time > 0:
            print(f"Average execution time: {avg_execution_time:.3f}s", flush=True)
        if avg_server_execution_duration is not None:
            print(f"Average server execution duration: {avg_server_execution_duration:.3f}s", flush=True)
        if avg_server_optimizer_duration is not None:
            print(f"Average server optimizer duration: {avg_server_optimizer_duration:.3f}s", flush=True)
        if avg_server_combined_duration is not None:
            print(f"Average server combined duration: {avg_server_combined_duration:.3f}s", flush=True)
        print("=" * 60, flush=True)
        sys.stdout.flush()

        return summary


def main():
    """Main entry point for the script."""
    parser = argparse.ArgumentParser(description="Run SPARQL queries with timeout protection")
    parser.add_argument("--db", "-d", default="wikidata-prerelease-new.db",
                       help="Database directory path (default: wikidata-prerelease-new.db)")
    parser.add_argument("--queries", "-q", default="c2rpqs-connected.txt",
                       help="Query file path (default: c2rpqs-connected.txt)")
    parser.add_argument("--timeout", "-t", type=int, default=60000,
                       help="Query timeout in milliseconds (default: 60000)")
    parser.add_argument("--port", "-p", type=int, default=8080,
                       help="Server port (default: 8080)")
    parser.add_argument("--build-type", "-b", choices=["Debug", "Release"], default="Debug",
                       help="Build type to use for server binary (default: Debug)")
    parser.add_argument("--server", "-s", default=None,
                       help="Path to MDB server binary (default: build/<build-type>/bin/mdb-server)")
    parser.add_argument("--custom", action="store_true",
                       help="Use custom planner (adds --custom-planner and --custom-planner-verbose)")
    parser.add_argument("--retry-attempts", "-r", type=int, default=3,
                       help="Number of retry attempts for server restart (default: 3)")
    parser.add_argument("--output", "-o", type=str,
                       help="Output file for detailed results (JSON format)")
    parser.add_argument("--verbose", "-v", action="store_true",
                       help="Verbose output")

    args = parser.parse_args()

    try:
        with TimeoutQueryRunner(
            db_path=args.db,
            queries_file=args.queries,
            mdb_server_path=args.server,
            port=args.port,
            timeout_ms=args.timeout,
            use_custom_planner=args.custom,
            build_type=args.build_type,
            retry_attempts=args.retry_attempts
        ) as runner:

            results = runner.run_all_queries()

            # Save detailed results if requested
            if args.output:
                with open(args.output, 'w') as f:
                    json.dump(results, f, indent=2)
                print(f"\nDetailed results saved to: {args.output}", flush=True)

            # Exit with error code if there were failures
            if not results['success'] or results['failed_queries'] > 0:
                sys.exit(1)

    except KeyboardInterrupt:
        print("\nQuery execution interrupted by user", flush=True)
        sys.exit(130)
    except Exception as e:
        print(f"Error running queries: {e}", flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()