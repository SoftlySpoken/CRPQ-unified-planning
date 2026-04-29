#!/usr/bin/env python3
import json
import sys
import argparse

def load_stats(filepath):
    with open(filepath, 'r') as f:
        data = json.load(f)
    return data

def compute_metrics(stats):
    avg_combined = stats.get('avg_server_combined_duration')
    if avg_combined is None:
        # Fallback computation if not present
        avg_exec = stats.get('avg_server_execution_duration', 0)
        avg_opt = stats.get('avg_server_optimizer_duration', 0)
        avg_combined = avg_exec + avg_opt

    timeouts = stats.get('failed_queries', 0) + stats.get('timeout_queries', 0)
    return {
        'avg_combined_time': avg_combined,
        'timeouts': timeouts
    }

def extract_speedups(file1_data, file2_data):
    """Compute per-query speedups and return list of speedup values."""
    results1 = {r['query_number']: r for r in file1_data['results'] if r['success'] and not r['timeout']}
    results2 = {r['query_number']: r for r in file2_data['results'] if r['success'] and not r['timeout']}

    common_queries = set(results1.keys()) & set(results2.keys())
    speedups = []
    for q in common_queries:
        if not results1[q]['success'] or not results2[q]['success']:
            continue
        if results1[q].get('server_execution_duration') is None \
            or results2[q].get('server_execution_duration') is None \
            or results1[q].get('server_optimizer_duration') is None \
            or results2[q].get('server_optimizer_duration') is None:
            continue
        t1 = results1[q]['server_execution_duration'] + results1[q]['server_optimizer_duration']
        t2 = results2[q]['server_execution_duration'] + results2[q]['server_optimizer_duration']
        if t2 > 0:
            speedup = t1 / t2
            speedups.append(speedup)
            if speedup < .1:
                print(results1[q], results2[q])
    return speedups

def main():
    parser = argparse.ArgumentParser(description='Generate a LaTeX table comparing two stats JSON files.')
    parser.add_argument('file1', help='First stats JSON file (baseline)')
    parser.add_argument('file2', help='Second stats JSON file (optimized)')
    parser.add_argument('output_file', help='Output LaTeX file path')
    args = parser.parse_args()

    stats1 = load_stats(args.file1)
    stats2 = load_stats(args.file2)

    metrics1 = compute_metrics(stats1)
    metrics2 = compute_metrics(stats2)

    # Compute speedups
    speedups = extract_speedups(stats1, stats2)
    if speedups:
        # avg_speedup = sum(speedups) / len(speedups)
        max_speedup = max(speedups)
        min_speedup = min(speedups)
    # else:
    #     avg_speedup = max_speedup = min_speedup = float('nan')
    avg_speedup = metrics1['avg_combined_time'] / metrics2['avg_combined_time']

    # Prepare LaTeX table rows
    def format_val(v, fmt="{:.4f}"):
        if isinstance(v, float) and (v != v):  # NaN check
            return "—"
        return fmt.format(v)

    row1 = [
        "MillenniumDB",
        format_val(metrics1['avg_combined_time']),
        "—",  # speedup not applicable for baseline row
        "—",
        "—",
        str(metrics1['timeouts'])
    ]
    row2 = [
        "Ours",
        format_val(metrics2['avg_combined_time']),
        format_val(avg_speedup),
        format_val(max_speedup),
        format_val(min_speedup),
        str(metrics2['timeouts'])
    ]

    # Write LaTeX table to file
    with open(args.output_file, 'w') as f:
        f.write(r"\begin{tabular}{lp{1cm}p{0.8cm}p{1cm}p{0.8cm}p{0.8cm}}" + "\n")
        f.write(r"\toprule" + "\n")
        f.write(r" & Avg. E2E Query Time (s) & Avg. Speedup & Max Speedup & Min Speedup & \# Timeouts \\" + "\n")
        f.write(r"\midrule" + "\n")
        f.write(" " + " & ".join(row1) + r" \\" + "\n")
        f.write(" " + " & ".join(row2) + r" \\" + "\n")
        f.write(r"\bottomrule" + "\n")
        f.write(r"\end{tabular}" + "\n")

    # Output LaTeX table
    print(r"\begin{tabular}{lp{1cm}p{0.8cm}p{1cm}p{0.8cm}p{0.8cm}}")
    print(r"\toprule")
    print(r" & Avg. E2E Query Time (s) & Avg. Speedup & Max Speedup & Min Speedup & \# Timeouts \\")
    print(r"\midrule")
    print(" " + " & ".join(row1) + r" \\")
    print(" " + " & ".join(row2) + r" \\")
    print(r"\bottomrule")
    print(r"\end{tabular}")

if __name__ == '__main__':
    main()