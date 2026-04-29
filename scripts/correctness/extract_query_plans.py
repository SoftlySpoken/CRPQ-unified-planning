#!/usr/bin/env python3
"""
Extract query IDs whose plans contain IndexScan or LeapfrogBptIter operations.

This script parses the output of performance runs and identifies queries that use
specific execution plan operations across multiple runs.
"""

import sys
import re

def extract_query_ids_with_plans(input_file):
    """
    Extract query IDs that have IndexScan or LeapfrogBptIter in their execution plans.

    Args:
        input_file: Path to the performance run output file

    Returns:
        Tuple of (run1_query_ids, run2_query_ids)
    """
    try:
        with open(input_file, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
    except FileNotFoundError:
        print(f"Error: File '{input_file}' not found.")
        sys.exit(1)
    except Exception as e:
        print(f"Error reading file: {e}")
        sys.exit(1)

    # Parse the file to identify query segments
    query_segments = []
    current_segment = []
    current_query_id = None

    for line in lines:
        line_stripped = line.strip()

        # Check if this is an "Executing query" line
        match = re.search(r'Executing query (\d+)', line_stripped)
        if match:
            # If we have a current segment, save it
            if current_query_id is not None and current_segment:
                query_segments.append((current_query_id, current_segment))

            # Start new segment
            current_query_id = int(match.group(1))
            current_segment = [line]
        else:
            if current_segment is not None:
                current_segment.append(line)

    # Don't forget the last segment
    if current_query_id is not None and current_segment:
        query_segments.append((current_query_id, current_segment))

    # Group segments by runs (first occurrence vs second occurrence)
    seen_queries = set()
    run1_segments = []
    run2_segments = []

    for query_id, segment in query_segments:
        if query_id in seen_queries:
            # This is the second run
            run2_segments.append((query_id, segment))
        else:
            # This is the first run
            seen_queries.add(query_id)
            run1_segments.append((query_id, segment))

    def has_target_operations(segment):
        """Check if segment contains IndexScan or LeapfrogBptIter operations."""
        text = ' '.join(segment)
        return 'IndexScan' in text or 'LeapfrogBptIter' in text

    # Find queries with target operations in each run
    run1_target_queries = [query_id for query_id, segment in run1_segments
                          if has_target_operations(segment)]
    run2_target_queries = [query_id for query_id, segment in run2_segments
                          if has_target_operations(segment)]

    return sorted(run1_target_queries), sorted(run2_target_queries)

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 extract_query_plans.py <input_file>")
        print("Example: python3 extract_query_plans.py run_performance.sh.out.20251221")
        sys.exit(1)

    input_file = sys.argv[1]
    run1_ids, run2_ids = extract_query_ids_with_plans(input_file)

    print("Run 1 - Query IDs with IndexScan or LeapfrogBptIter:")
    if run1_ids:
        print(run1_ids)
    else:
        print("None")

    print("\nRun 2 - Query IDs with IndexScan or LeapfrogBptIter:")
    if run2_ids:
        print(run2_ids)
    else:
        print("None")

    # Summary statistics
    print(f"\nSummary:")
    print(f"Run 1: {len(run1_ids)} queries with target operations")
    print(f"Run 2: {len(run2_ids)} queries with target operations")

if __name__ == "__main__":
    main()