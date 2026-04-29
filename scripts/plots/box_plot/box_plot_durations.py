import json
import matplotlib.pyplot as plt
import argparse
import os

def load_durations_from_file(filepath):
    with open(filepath, 'r') as f:
        data = json.load(f)
    
    exec_durs = []
    opt_durs = []
    combined_durs = []
    
    for res in data.get("results", []):
        if res.get("success", False):
            exec_dur = res.get("server_execution_duration")
            opt_dur = res.get("server_optimizer_duration")
            if exec_dur is not None and opt_dur is not None:
                exec_durs.append(exec_dur * 1000.)
                opt_durs.append(opt_dur * 1000.)
                combined_durs.append((exec_dur + opt_dur) * 1000.)
    
    return exec_durs, opt_durs, combined_durs

def save_boxplot(data1, data2, labels, title, ylabel, output_path, figsize=(4, 4)):
    """Save a single boxplot with consistent figure size."""
    fig, ax = plt.subplots(figsize=figsize)
    ax.boxplot([data1, data2], labels=labels, showfliers=False)
    # ax.boxplot([data1, data2], labels=labels)
    ax.set_title(title)
    ax.set_ylabel(ylabel)
    # Optional: rotate x-axis labels if filenames are long
    # plt.xticks(rotation=0, ha='right')
    plt.xticks(rotation=0)
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    plt.close(fig)  # Free memory
    print(f"Saved: {output_path}")

def save_violinplot(data1, data2, labels, title, ylabel, output_path, figsize=(4, 4)):
    """Save a single violin plot with consistent figure size."""
    fig, ax = plt.subplots(figsize=figsize)
    parts = ax.violinplot([data1, data2], positions=[1, 2], showmeans=True, showextrema=True)

    # Customize violin plot appearance
    for pc in parts['bodies']:
        pc.set_facecolor('#8da0cb')
        pc.set_alpha(0.7)

    # Set x-axis labels
    ax.set_xticks([1, 2])
    ax.set_xticklabels(labels)
    ax.set_title(title)
    ax.set_ylabel(ylabel)
    plt.xticks(rotation=0)
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches='tight')
    plt.close(fig)  # Free memory
    print(f"Saved: {output_path}")

def get_label(filename):
    return "MillenniumDB" if "original" in filename else "Ours" if "custom" in filename else "unknown"

def main(file1, file2, output_dir, prefix="plot"):
    # Create output directory if it doesn't exist
    os.makedirs(output_dir, exist_ok=True)

    file1_label = get_label(file1)
    file2_label = get_label(file2)
    labels = [file1_label, file2_label]
    # labels = [os.path.basename(file1), os.path.basename(file2)]  # Use base names for cleaner labels

    exec1, opt1, comb1 = load_durations_from_file(file1)
    exec2, opt2, comb2 = load_durations_from_file(file2)

    figsize = (2.5, 3)  # Same size for all figures

    # Generate box plots
    save_boxplot(
        exec1, exec2, labels,
        title='Execution Time',
        ylabel='ms',
        output_path=os.path.join(output_dir, f"{prefix}_execution.png"),
        figsize=figsize
    )

    save_boxplot(
        opt1, opt2, labels,
        title='Planning Time',
        ylabel='ms',
        output_path=os.path.join(output_dir, f"{prefix}_optimizer.png"),
        figsize=figsize
    )

    save_boxplot(
        comb1, comb2, labels,
        title='E2E Query Time',
        ylabel='ms',
        output_path=os.path.join(output_dir, f"{prefix}_combined.png"),
        figsize=figsize
    )

    # Generate violin plots
    save_violinplot(
        exec1, exec2, labels,
        title='Execution Time',
        ylabel='ms',
        output_path=os.path.join(output_dir, f"{prefix}_execution_violin.png"),
        figsize=figsize
    )

    save_violinplot(
        opt1, opt2, labels,
        title='Planning Time',
        ylabel='ms',
        output_path=os.path.join(output_dir, f"{prefix}_optimizer_violin.png"),
        figsize=figsize
    )

    save_violinplot(
        comb1, comb2, labels,
        title='E2E Query Time',
        ylabel='ms',
        output_path=os.path.join(output_dir, f"{prefix}_combined_violin.png"),
        figsize=figsize
    )

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate three separate box plots and violin plots with identical size.")
    parser.add_argument("file1", help="Path to the first JSON file")
    parser.add_argument("file2", help="Path to the second JSON file")
    parser.add_argument("output_dir", help="Output directory path (will be created if it doesn't exist)")
    parser.add_argument("-p", "--prefix", default="result", help="Prefix for output filenames (default: 'result')")
    args = parser.parse_args()

    main(args.file1, args.file2, args.output_dir, args.prefix)