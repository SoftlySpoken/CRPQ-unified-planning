import json
import sys
import os

def load_results(filepath):
    if not os.path.exists(filepath):
        print(f"Error: File '{filepath}' does not exist.")
        sys.exit(1)

    try:
        with open(filepath, 'r') as f:
            data = json.load(f)
        return data.get("results", [])
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON in file '{filepath}': {e}")
        sys.exit(1)
    except IOError as e:
        print(f"Error: Cannot read file '{filepath}': {e}")
        sys.exit(1)

def compare_result_counts(file1, file2):
    results1 = load_results(file1)
    results2 = load_results(file2)

    # Create a dict mapping query_number to result_count for fast lookup
    map1 = {r["query_number"]: r["result_count"] for r in results1}
    map2 = {r["query_number"]: r["result_count"] for r in results2}

    differing_queries = []

    # Iterate over all query numbers present in either file
    all_query_numbers = set(map1.keys()) | set(map2.keys())
    for qid in sorted(all_query_numbers):
        rc1 = map1.get(qid)
        rc2 = map2.get(qid)

        # Only compare if both are non-null
        if rc1 is not None and rc2 is not None:
            if rc1 != rc2:
                differing_queries.append(qid)

    return differing_queries

def main():
    if len(sys.argv) != 3:
        print("Usage: python compare_results.py <file1.json> <file2.json>")
        sys.exit(1)

    file1, file2 = sys.argv[1], sys.argv[2]
    differing = compare_result_counts(file1, file2)

    if differing:
        print("Query IDs with differing non-null result_count:")
        for qid in differing:
            print(qid)
    else:
        print("No differing non-null result_count found.")

if __name__ == "__main__":
    main()