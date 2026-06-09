#!/usr/bin/env python3

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def parse_benchmark_table(path: Path) -> list[dict]:
    rows: list[dict] = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("-") or line.startswith("Banks"):
            continue
        parts = line.split()
        if len(parts) < 10:
            continue
        rows.append(
            {
                "banks": int(parts[0]),
                "loans": int(parts[1]),
                "shock": float(parts[2]),
                "threads": int(parts[3]),
                "seq_defaults": int(parts[4]),
                "par_defaults": int(parts[5]),
                "seq_time_ms": float(parts[6]),
                "par_time_ms": float(parts[7]),
                "speedup": float(parts[8]),
                "check": parts[9],
            }
        )
    return rows


def aggregate(rows: list[dict]) -> dict[tuple[int, int], dict[str, np.ndarray]]:
    grouped: dict[tuple[int, int], list[dict]] = defaultdict(list)
    for row in rows:
        grouped[(row["banks"], row["threads"])].append(row)

    stats: dict[tuple[int, int], dict[str, np.ndarray]] = {}
    for key, items in grouped.items():
        speedups = np.array([item["speedup"] for item in items], dtype=float)
        par_times = np.array([item["par_time_ms"] for item in items], dtype=float)
        seq_times = np.array([item["seq_time_ms"] for item in items], dtype=float)
        stats[key] = {
            "speedup_mean": speedups.mean(),
            "speedup_std": speedups.std(ddof=0),
            "speedup_min": speedups.min(),
            "speedup_max": speedups.max(),
            "par_time_mean": par_times.mean(),
            "par_time_std": par_times.std(ddof=0),
            "par_time_min": par_times.min(),
            "par_time_max": par_times.max(),
            "seq_time_mean": seq_times.mean(),
            "seq_time_std": seq_times.std(ddof=0),
            "seq_time_min": seq_times.min(),
            "seq_time_max": seq_times.max(),
        }
    return stats


def series_for_threads(
    stats: dict[tuple[int, int], dict[str, np.ndarray]],
    banks: list[int],
    threads: int,
    metric: str,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    means = []
    lows = []
    highs = []
    for bank_count in banks:
        entry = stats[(bank_count, threads)]
        means.append(entry[f"{metric}_mean"])
        lows.append(entry[f"{metric}_min"])
        highs.append(entry[f"{metric}_max"])
    return np.array(means), np.array(lows), np.array(highs)


def plot_speedup(
    stats: dict[tuple[int, int], dict[str, np.ndarray]],
    banks: list[int],
    thread_counts: list[int],
    output_path: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))

    for threads in thread_counts:
        means, lows, highs = series_for_threads(stats, banks, threads, "speedup")
        x = np.array(banks, dtype=float)
        ax.plot(x, means, marker="o", linewidth=2, label=f"{threads} threads")
        ax.fill_between(x, lows, highs, alpha=0.15)

    ax.axhline(1.0, color="black", linestyle="--", linewidth=1, alpha=0.5, label="No speedup")
    ax.set_xlabel("Number of banks")
    ax.set_ylabel("Speedup (sequential time / parallel time)")
    ax.set_title("Figure A: Speedup vs. number of banks")
    ax.set_xticks(banks)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=9)
    fig.tight_layout()
    fig.savefig(output_path, dpi=200)
    plt.close(fig)


def plot_execution_time(
    stats: dict[tuple[int, int], dict[str, np.ndarray]],
    banks: list[int],
    thread_counts: list[int],
    output_path: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))

    seq_means, seq_lows, seq_highs = series_for_threads(stats, banks, thread_counts[0], "seq_time")
    x = np.array(banks, dtype=float)
    ax.plot(
        x,
        seq_means,
        color="black",
        marker="s",
        linewidth=2,
        linestyle="--",
        label="Sequential (mean)",
    )
    ax.fill_between(x, seq_lows, seq_highs, color="black", alpha=0.08)

    for threads in thread_counts:
        means, lows, highs = series_for_threads(stats, banks, threads, "par_time")
        ax.plot(x, means, marker="o", linewidth=2, label=f"{threads} threads")
        ax.fill_between(x, lows, highs, alpha=0.15)

    ax.set_xlabel("Number of banks")
    ax.set_ylabel("Execution time (ms, 64 repeats)")
    ax.set_title("Figure B: Execution time vs. number of banks")
    ax.set_xticks(banks)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=8)
    fig.tight_layout()
    fig.savefig(output_path, dpi=200)
    plt.close(fig)


def plot_time_per_bank(
    stats: dict[tuple[int, int], dict[str, np.ndarray]],
    banks: list[int],
    thread_counts: list[int],
    output_path: Path,
) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))
    x = np.array(banks, dtype=float)

    seq_means, seq_lows, seq_highs = series_for_threads(stats, banks, thread_counts[0], "seq_time")
    ax.plot(
        x,
        seq_means / x,
        color="black",
        marker="s",
        linewidth=2,
        linestyle="--",
        label="Sequential (mean)",
    )
    ax.fill_between(x, seq_lows / x, seq_highs / x, color="black", alpha=0.08)

    for threads in thread_counts:
        means, lows, highs = series_for_threads(stats, banks, threads, "par_time")
        ax.plot(x, means / x, marker="o", linewidth=2, label=f"{threads} threads")
        ax.fill_between(x, lows / x, highs / x, alpha=0.15)

    ax.set_xlabel("Number of banks")
    ax.set_ylabel("Execution time per bank (ms / bank, 64 repeats)")
    ax.set_title("Figure B': Time per bank vs. number of banks")
    ax.set_xticks(banks)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=8)
    fig.tight_layout()
    fig.savefig(output_path, dpi=200)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("results/benchmark_output.txt"),
        help="Benchmark table text file",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("figures"),
        help="Directory for generated figures",
    )
    args = parser.parse_args()

    rows = parse_benchmark_table(args.input)
    if not rows:
        raise SystemExit(f"No benchmark rows parsed from {args.input}")

    checks = {row["check"] for row in rows}
    if checks != {"OK"}:
        raise SystemExit(f"Unexpected correctness checks found: {checks}")

    stats = aggregate(rows)
    banks = sorted({row["banks"] for row in rows})
    thread_counts = sorted({row["threads"] for row in rows})

    args.output_dir.mkdir(parents=True, exist_ok=True)

    plot_speedup(
        stats,
        banks,
        thread_counts,
        args.output_dir / "figure_a_speedup_vs_banks.png",
    )
    plot_execution_time(
        stats,
        banks,
        thread_counts,
        args.output_dir / "figure_b_execution_time_vs_banks.png",
    )
    plot_time_per_bank(
        stats,
        banks,
        thread_counts,
        args.output_dir / "figure_b_prime_time_per_bank_vs_banks.png",
    )

    print(f"Wrote figures to {args.output_dir.resolve()}")
    print(f"Parsed {len(rows)} rows; banks={banks}; threads={thread_counts}")
    print("Correctness: 100% OK")


if __name__ == "__main__":
    main()
