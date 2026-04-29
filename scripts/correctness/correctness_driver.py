#!/usr/bin/env python3
import os
import sys
import argparse
import subprocess
from datetime import datetime


def run_command(cmd, timeout_sec):
    """Run a shell command with a timeout."""
    try:
        print(f"Running: {' '.join(cmd)}")
        result = subprocess.run(cmd, check=True, timeout=timeout_sec, text=True)
        return result.returncode
    except subprocess.TimeoutExpired:
        print(f"Command timed out after {timeout_sec} seconds.", file=sys.stderr)
        return -1
    except subprocess.CalledProcessError as e:
        print(f"Command failed with return code {e.returncode}.", file=sys.stderr)
        return e.returncode


def main():
    parser = argparse.ArgumentParser(description="Run benchmark queries and compare results.")
    parser.add_argument("--queries", required=True, help="Path to the queries file.")
    parser.add_argument("--base-timeout", type=int, default=1000, help="Query timeout in seconds (passed to run_queries_with_timeout.py).")
    parser.add_argument("--script-timeout", type=int, default=18000, help="Overall script timeout per command in seconds (for subprocess).")
    parser.add_argument("--output-path", required=True, help="Directory to store output JSON files.")
    parser.add_argument("--build-type", default="Debug", help="Build type passed to run_queries_with_timeout.py.")
    parser.add_argument("--verbose", action="store_true", help="Enable verbose output.")
    parser.add_argument("--original-result", help="Path to existing original result file. If provided, cmd1 will be skipped.")

    args = parser.parse_args()

    # Ensure output path exists
    os.makedirs(args.output_path, exist_ok=True)

    # Generate timestamp (safe for filenames)
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")

    # Use provided original result file or generate new path
    if args.original_result:
        original_out = args.original_result
        print(f"Using existing original result file: {original_out}")
    else:
        original_out = os.path.join(args.output_path, f"test_results-original-{timestamp}.json")

    custom_out = os.path.join(args.output_path, f"test_results-custom-{timestamp}.json")

    # Build command list
    base_cmd = [
        "python3", "../../run_queries_with_timeout.py",
        "--db", "../../wikidata-prerelease-new.db",
        "--server", "../../build/" + args.build_type + "/bin/mdb-server",
        "--queries", args.queries,
        "--timeout", str(args.base_timeout)
    ]
    if args.verbose:
        base_cmd.append("--verbose")

    # Command 1: original (only if no original result file provided)
    if not args.original_result:
        cmd1 = base_cmd + ["--output", original_out]
        ret1 = run_command(cmd1, args.script_timeout)
        # if ret1 != 0:
        #     print("Original query run failed. Skipping custom run and comparison.", file=sys.stderr)
        #     sys.exit(1)
    else:
        # Verify the provided original result file exists
        if not os.path.exists(args.original_result):
            print(f"Error: Original result file does not exist: {args.original_result}", file=sys.stderr)
            sys.exit(1)

    # Command 2: custom (with --custom flag)
    cmd2 = base_cmd + ["--custom", "--output", custom_out]
    ret2 = run_command(cmd2, args.script_timeout)
    # if ret2 != 0:
    #     print("Custom query run failed. Skipping comparison.", file=sys.stderr)
    #     sys.exit(1)

    # Command 3: compare
    cmd3 = ["python3", "compare_results.py", original_out, custom_out]
    print(f"Running comparison: {' '.join(cmd3)}")
    try:
        subprocess.run(cmd3, check=True)
        print("Comparison completed successfully.")
    except subprocess.CalledProcessError as e:
        print(f"Comparison failed with return code {e.returncode}.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    # os.environ['LD_PRELOAD'] = '/usr/lib/x86_64-linux-gnu/libasan.so.5'
    main()