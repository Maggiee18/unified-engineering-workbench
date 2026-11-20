"""Render labelled report plots from the raw CSVs written by workbench_cli.

Documentation tooling only: this script performs no engineering computation.
Every plotted number is read verbatim from results/csv or results/benchmarks,
which the C++ backend produced.

    python tools/make_plots.py results
"""
import csv
import glob
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

plt.rcParams.update({"font.size": 10, "axes.grid": True, "grid.alpha": 0.3, "figure.dpi": 150})
COLORS = ["#1f5fbf", "#d9480f", "#2f9e44", "#7048e8"]


def read_csv(path):
    with open(path, newline="") as f:
        rows = [line for line in f if not line.startswith("#")]
    return list(csv.DictReader(rows))


def fea_plots(results, out):
    exact = read_csv(os.path.join(results, "csv", "fea_exact_curve.csv"))
    fig, ax = plt.subplots(figsize=(7, 4))
    ax.plot([float(r["x_m"]) for r in exact], [float(r["T_exact_C"]) for r in exact], "k--", lw=1.5, label="exact $T(x)$")
    for color, n in zip(COLORS, (10, 20, 40, 80)):
        rows = read_csv(os.path.join(results, "csv", f"fea_nodal_N{n}.csv"))
        ax.plot([float(r["x_m"]) for r in rows], [float(r["T_fe_C"]) for r in rows], "o-", ms=3 if n <= 20 else 1.5,
                lw=1, color=color, label=f"FE nodes, N={n}")
    ax.set_xlabel("x [m]")
    ax.set_ylabel("temperature T [°C]")
    ax.set_title("1D steady conduction with uniform heat generation: FE vs exact")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fea_temperature_distribution.png"))
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 3.6))
    for color, n in zip(COLORS, (10, 20, 40, 80)):
        rows = read_csv(os.path.join(results, "csv", f"fea_midpoints_N{n}.csv"))
        ax.plot([float(r["x_mid_m"]) for r in rows], [float(r["abs_error_C"]) for r in rows], "o-", ms=2.5, lw=1,
                color=color, label=f"N={n}")
    ax.set_yscale("log")
    ax.set_xlabel("element midpoint x [m]")
    ax.set_ylabel("|T_exact − T_h| [°C]")
    ax.set_title("Midpoint error using FE interpolation (uniform: q h²/8k per element)")
    ax.legend(fontsize=8, ncol=4)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fea_midpoint_error.png"))
    plt.close(fig)

    conv = read_csv(os.path.join(results, "csv", "fea_convergence.csv"))
    h = [float(r["h_m"]) for r in conv]
    e = [float(r["max_midpoint_error_C"]) for r in conv]
    nodal = [max(float(r["max_nodal_error_C"]), 1e-16) for r in conv]
    fig, ax = plt.subplots(figsize=(5.5, 4))
    ax.loglog(h, e, "o-", color=COLORS[0], label="max midpoint error (FE interp.)")
    ax.loglog(h, [e[0] * (x / h[0]) ** 2 for x in h], "--", color="gray", label="slope 2 reference")
    ax.loglog(h, nodal, "s:", color=COLORS[1], label="max nodal error (round-off)")
    for x, y, r in zip(h, e, conv):
        ax.annotate(f"N={r['elements']}", (x, y), textcoords="offset points", xytext=(5, -10), fontsize=8)
    ax.set_xlabel("element size h [m]")
    ax.set_ylabel("max |error| [°C]")
    ax.set_title("Mesh convergence (observed order 2.000)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out, "fea_mesh_convergence.png"))
    plt.close(fig)


def backprop_plot(results, out):
    rows = read_csv(os.path.join(results, "csv", "backprop_loss_history.csv"))
    fig, ax = plt.subplots(figsize=(6, 3.6))
    ax.semilogy([int(r["step"]) for r in rows], [float(r["loss"]) for r in rows], color=COLORS[0])
    ax.set_xlabel("training step (0 = initial parameters)")
    ax.set_ylabel("loss J (log scale)")
    ax.set_title("Single-sample regression, η = 0.1: loss over 100 steps")
    fig.tight_layout()
    fig.savefig(os.path.join(out, "backprop_loss.png"))
    plt.close(fig)


def benchmark_plots(results, out):
    pattern = os.path.join(results, "benchmarks", "**", "benchmark_summary_*.csv")
    for path in sorted(glob.glob(pattern, recursive=True)):
        machine = os.path.basename(os.path.dirname(path))
        tag = machine + "_" + os.path.basename(path)[len("benchmark_summary_"):-4]
        with open(path) as f:
            header = [line.strip("# \n") for line in f if line.startswith("#")]
        rows = read_csv(path)
        par = [r for r in rows if r["mode"] == "parallel" and r["skipped"] == "false"]
        serial = [r for r in rows if r["mode"] == "serial"][0]
        fig, (a1, a2) = plt.subplots(1, 2, figsize=(9, 3.6))
        labels = ["serial"] + [f"p={r['workers']}" for r in par]
        times = [float(serial["median_s"])] + [float(r["median_s"]) for r in par]
        a1.bar(labels, times, color=["#8a96a6"] + [COLORS[0]] * len(par))
        a1.set_ylabel("median wall-clock [s]")
        a1.set_title("Median of 5 measured runs")
        p = [int(r["workers"]) for r in par]
        a2.plot(p, [float(r["speedup_T1_over_Tp"]) for r in par], "o-", color=COLORS[0], label="speedup T1/Tp")
        a2.plot(p, p, "--", color="gray", label="ideal")
        a2.plot(p, [float(r["efficiency"]) for r in par], "s-", color=COLORS[1], label="efficiency")
        a2.set_xlabel("workers p")
        a2.set_xticks(p)
        a2.set_title("Speedup and efficiency")
        a2.legend(fontsize=8)
        cpu = header[0].split(";")[0].replace("cpu=", "") if header else ""
        fig.suptitle(f"{tag.replace('_', ', ')} — {cpu}", fontsize=9)
        fig.tight_layout()
        fig.savefig(os.path.join(out, f"benchmark_{tag}.png"))
        plt.close(fig)


def main():
    results = sys.argv[1] if len(sys.argv) > 1 else "results"
    out = os.path.join(results, "plots")
    os.makedirs(out, exist_ok=True)
    fea_plots(results, out)
    backprop_plot(results, out)
    benchmark_plots(results, out)
    print("plots written to", out)


if __name__ == "__main__":
    main()
