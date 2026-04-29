import re
import json
import os
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from typing import List, Tuple, Dict

# Configure matplotlib for larger font sizes
plt.rcParams.update({
    'font.size': 18,           # Base font size
    'axes.labelsize': 20,      # X and Y axis labels
    'axes.titlesize': 20,      # Title font size
    'xtick.labelsize': 18,     # X-axis tick labels
    'ytick.labelsize': 18,     # Y-axis tick labels
    'legend.fontsize': 18,     # Legend font size
    'figure.titlesize': 20     # Figure title font size
})

def parse_query_line(line: str) -> Tuple[int, int, int, int]:
    """
    Parse a single query line to extract:
    - number of triples
    - number of Kleene closures ('*' or '+')
    - number of constant IRI in subject or object positions
    """
    # Remove query ID at start (e.g., "1,")
    query_pattern = r'^\d+,(.*)$'
    match = re.match(query_pattern, line.strip())
    if not match:
        raise ValueError(f"Invalid query line format: {line}")
    sparql_like = match.group(1).strip()

    # Split into triple patterns by '.'; ignore trailing empty parts
    triples_raw = [t.strip() for t in sparql_like.split(' . ') if t.strip()]
    num_triples = len(triples_raw)

    # Count Kleene closures: look for '*' or '+' that are not inside IRI <>
    # Simple heuristic: count standalone * and +
    kleene_count = 0
    for _ in re.findall(r'\*|\+', sparql_like):
        kleene_count += 1

    # Count constant IRIs in subject/object positions
    # Each triple: ?x2 <p> <iri> . or <iri> <p> ?x
    # We'll extract all tokens that look like <http://...>
    num_const_src_tgt = 0
    num_predicates_in_path = 0
    iri_pattern = r'<[^>]+>'
    # print(triples_raw)
    for tp in triples_raw:
        tp_ls = tp.split()
        # print(tp_ls)
        if len(tp_ls) < 3:
            continue
        match = re.match(iri_pattern, tp_ls[0])
        if match:
            # print(tp_ls[0])
            num_const_src_tgt += 1
        # else:
            # print("None s")
        for _ in re.findall(iri_pattern, tp_ls[1]):
            num_predicates_in_path += 1
        match = re.match(iri_pattern, tp_ls[2])
        if match:
            # print(tp_ls[2])
            num_const_src_tgt += 1
        # else:
        #     print("None t")

    return num_triples, kleene_count, num_const_src_tgt, num_predicates_in_path

def load_queries(query_file: str) -> List[Tuple[int, int, int]]:
    queries = []
    with open(query_file, 'r', encoding='utf-8') as f:
        for line in f:
            if line.strip():
                try:
                    stats = parse_query_line(line)
                    queries.append(stats)
                except Exception as e:
                    print(f"Warning: skipping line due to parse error: {e}")
    return queries

def load_runtime_stats(json_file: str) -> List[Dict]:
    with open(json_file, 'r', encoding='utf-8') as f:
        data = json.load(f)
    return data['results']

def build_buckets(queries: List[Tuple[int, int, int, int]], results: List[Dict]) -> Tuple[
    List[List[float]], List[List[int]], List[int],
    List[List[float]], List[List[int]], List[int],
    List[List[float]], List[List[int]], List[int],
    List[List[float]], List[List[int]], List[int]
]:
    """
    For each of the 4 metrics, build:
    - list of combined times per bucket
    - list of timeout flags (1/0) per bucket
    - bucket sizes (i.e., count per bucket)
    Returns 12 lists: (time1, timeout1, size1, time2, timeout2, size2, time3, timeout3, size3)
    """
    n = len(queries)
    assert n == len(results), f"Mismatch: {n} queries vs {len(results)} results"

    triples_list = [q[0] for q in queries]
    kleene_list = [q[1] for q in queries]
    const_list = [q[2] for q in queries]
    pred_list = [q[3] for q in queries]

    max_triples = max(triples_list) if triples_list else 0
    max_kleene = max(kleene_list) if kleene_list else 0
    max_const = max(const_list) if const_list else 0
    max_pred = max(pred_list) if pred_list else 0

    # Initialize buckets: index = metric value
    def init_buckets(max_val):
        return [[] for _ in range(max_val + 1)], [[] for _ in range(max_val + 1)]

    t_times, t_timeouts = init_buckets(max_triples)
    k_times, k_timeouts = init_buckets(max_kleene)
    c_times, c_timeouts = init_buckets(max_const)
    p_times, p_timeouts = init_buckets(max_pred)

    for i in range(n):
        # print(i, flush=True)
        t, k, c, p = queries[i]
        res = results[i]
        # print(res)

        # Determine if this is a timeout (unsuccessful query)
        is_timeout = not res.get('success', False)

        # Always record timeout status for all queries
        timeout_flag = 1 if is_timeout else 0
        t_timeouts[t].append(timeout_flag)
        k_timeouts[k].append(timeout_flag)
        c_timeouts[c].append(timeout_flag)
        p_timeouts[p].append(timeout_flag)

        # Only record execution times for successful queries
        if not is_timeout:
            if 'server_execution_duration' not in res or 'server_optimizer_duration' not in res:
                continue
            if res.get('server_execution_duration') is None or res.get('server_optimizer_duration') is None:
                continue
            combined = (res.get('server_execution_duration', 0) + res.get('server_optimizer_duration', 0)) * 1000.

            t_times[t].append(combined)
            k_times[k].append(combined)
            c_times[c].append(combined)
            p_times[p].append(combined)

    def compute_bucket_stats(times_buckets, timeout_buckets):
        avg_times = []
        timeout_percentages = []
        sizes = []

        # Ensure both lists have the same length
        max_buckets = max(len(times_buckets), len(timeout_buckets))

        for i in range(max_buckets):
            t_list = times_buckets[i] if i < len(times_buckets) else []
            to_list = timeout_buckets[i] if i < len(timeout_buckets) else []

            # Size is based on successful queries (times) + timeouts
            times_count = len(t_list)
            timeout_count = sum(to_list)
            total_queries = times_count + timeout_count

            sizes.append(total_queries)

            if times_count > 0:
                avg_times.append(sum(t_list) / times_count)
            else:
                avg_times.append(0.0)  # placeholder, will be skipped in plot

            # Calculate timeout percentage
            if total_queries > 0:
                timeout_percentage = (timeout_count / total_queries) * 100.0
            else:
                timeout_percentage = 0.0

            timeout_percentages.append(timeout_percentage)

        return avg_times, timeout_percentages, sizes

    t_avg, t_to, t_size = compute_bucket_stats(t_times, t_timeouts)
    k_avg, k_to, k_size = compute_bucket_stats(k_times, k_timeouts)
    c_avg, c_to, c_size = compute_bucket_stats(c_times, c_timeouts)
    p_avg, p_to, p_size = compute_bucket_stats(p_times, p_timeouts)

    return (t_avg, t_to, t_size, k_avg, k_to, k_size, c_avg, c_to, c_size, p_avg, p_to, p_size)

def plot_metric(x_vals, avg_times1, timeouts1, sizes1,
                avg_times2, timeouts2, sizes2,
                xlabel, title_prefix, save_path):
    # plt.figure(figsize=(8.8, 5))
    plt.figure(figsize=(16, 4.5))

    # Plot 1: Average combined time
    plt.subplot(1, 2, 1)
    x1 = [x for i, x in enumerate(x_vals) if sizes1[i] > 0 and timeouts1[i] < 100]
    # print(x1)
    y1 = [avg_times1[i] for i in range(len(sizes1)) if sizes1[i] > 0 and timeouts1[i] < 100]
    x2 = [x for i, x in enumerate(x_vals) if sizes2[i] > 0 and timeouts2[i] < 100]
    y2 = [avg_times2[i] for i in range(len(sizes2)) if sizes2[i] > 0 and timeouts2[i] < 100]

    plt.plot(x1, y1, marker='o', label='MillenniumDB')
    plt.plot(x2, y2, marker='s', label='Ours')
    plt.xlabel(xlabel)
    plt.ylabel('Avg. E2E Query Time (ms)')
    # plt.title(f'{title_prefix}: Average E2E Query Time')
    ax = plt.gca()
    ax.xaxis.set_major_locator(ticker.MultipleLocator(1)) 
    plt.legend()
    plt.grid(True)

    # Plot 2: Percentage of timeouts
    plt.subplot(1, 2, 2)
    x1_to = [x for i, x in enumerate(x_vals) if sizes1[i] > 0]
    x2_to = [x for i, x in enumerate(x_vals) if sizes2[i] > 0]
    y1_to = [timeouts1[i] for i in range(len(sizes1)) if sizes1[i] > 0]
    y2_to = [timeouts2[i] for i in range(len(sizes2)) if sizes2[i] > 0]

    plt.plot(x1_to, y1_to, marker='o', label='MillenniumDB')
    plt.plot(x2_to, y2_to, marker='s', label='Ours')
    plt.xlabel(xlabel)
    plt.ylabel('Percentage of Timeouts (%)')
    # plt.title(f'{title_prefix}: Average Number of Timeouts')
    ax = plt.gca()
    ax.xaxis.set_major_locator(ticker.MultipleLocator(1)) 
    plt.legend()
    plt.grid(True)

    plt.tight_layout()
    plt.savefig(save_path)
    plt.close()

def main(query_file: str, stats_file1: str, stats_file2: str, output_dir: str = "plots"):
    os.makedirs(output_dir, exist_ok=True)

    # Step 1: Parse queries
    queries = load_queries(query_file)

    # Step 2: Load runtime stats
    results1 = load_runtime_stats(stats_file1)
    results2 = load_runtime_stats(stats_file2)

    # Step 3: Build buckets
    (t_avg1, t_to1, t_size1,
     k_avg1, k_to1, k_size1,
     c_avg1, c_to1, c_size1,
     p_avg1, p_to1, p_size1,
     ) = build_buckets(queries, results1)

    (t_avg2, t_to2, t_size2,
     k_avg2, k_to2, k_size2,
     c_avg2, c_to2, c_size2,
     p_avg2, p_to2, p_size2
     ) = build_buckets(queries, results2)

    # Determine x-axis ranges (use union of both files' max)
    max_triples = max(len(t_avg1), len(t_avg2)) - 1
    max_kleene = max(len(k_avg1), len(k_avg2)) - 1
    max_const = max(len(c_avg1), len(c_avg2)) - 1
    max_pred = max(len(p_avg1), len(p_avg2)) - 1

    triples_x = list(range(max_triples + 1))
    kleene_x = list(range(max_kleene + 1))
    const_x = list(range(max_const + 1))
    pred_x = list(range(max_pred + 1))

    # Ensure lists are same length (pad with zeros/empty if needed)
    def pad_to_length(lst, target_len):
        return lst + [0.0] * (target_len - len(lst))

    t_avg1_p = pad_to_length(t_avg1, max_triples + 1)
    t_avg2_p = pad_to_length(t_avg2, max_triples + 1)
    t_to1_p = pad_to_length(t_to1, max_triples + 1)
    t_to2_p = pad_to_length(t_to2, max_triples + 1)
    t_size1_p = pad_to_length(t_size1, max_triples + 1)
    t_size2_p = pad_to_length(t_size2, max_triples + 1)

    k_avg1_p = pad_to_length(k_avg1, max_kleene + 1)
    k_avg2_p = pad_to_length(k_avg2, max_kleene + 1)
    k_to1_p = pad_to_length(k_to1, max_kleene + 1)
    k_to2_p = pad_to_length(k_to2, max_kleene + 1)
    k_size1_p = pad_to_length(k_size1, max_kleene + 1)
    k_size2_p = pad_to_length(k_size2, max_kleene + 1)

    c_avg1_p = pad_to_length(c_avg1, max_const + 1)
    c_avg2_p = pad_to_length(c_avg2, max_const + 1)
    c_to1_p = pad_to_length(c_to1, max_const + 1)
    c_to2_p = pad_to_length(c_to2, max_const + 1)
    c_size1_p = pad_to_length(c_size1, max_const + 1)
    c_size2_p = pad_to_length(c_size2, max_const + 1)

    p_avg1_p = pad_to_length(p_avg1, max_pred + 1)
    p_avg2_p = pad_to_length(p_avg2, max_pred + 1)
    p_to1_p = pad_to_length(p_to1, max_pred + 1)
    p_to2_p = pad_to_length(p_to2, max_pred + 1)
    p_size1_p = pad_to_length(p_size1, max_pred + 1)
    p_size2_p = pad_to_length(p_size2, max_pred + 1)

    # Step 4: Plot
    plot_metric(triples_x, t_avg1_p, t_to1_p, t_size1_p,
                t_avg2_p, t_to2_p, t_size2_p,
                'Number of RPQs', 'Triples', os.path.join(output_dir, 'triples.png'))

    plot_metric(kleene_x, k_avg1_p, k_to1_p, k_size1_p,
                k_avg2_p, k_to2_p, k_size2_p,
                'Number of Kleene Closures', 'Kleene Closures', os.path.join(output_dir, 'kleene.png'))

    plot_metric(const_x, c_avg1_p, c_to1_p, c_size1_p,
                c_avg2_p, c_to2_p, c_size2_p,
                'Number of Constant Vertices', 'Constants', os.path.join(output_dir, 'constants.png'))
    
    plot_metric(pred_x, p_avg1_p, p_to1_p, p_size1_p,
                p_avg2_p, p_to2_p, p_size2_p,
                'Number of Edge Labels', 'Predicates', os.path.join(output_dir, 'predicates.png'))

    print(f"Plots saved to {output_dir}/")

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Analyze query performance and plot results.")
    parser.add_argument("query_file", help="Path to query file (one query per line)")
    parser.add_argument("stats_file1", help="Path to first runtime stats JSON file")
    parser.add_argument("stats_file2", help="Path to second runtime stats JSON file")
    parser.add_argument("--output", default="plots", help="Output directory for plots")
    args = parser.parse_args()

    main(args.query_file, args.stats_file1, args.stats_file2, args.output)