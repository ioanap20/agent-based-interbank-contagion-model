#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path

import matplotlib.pyplot as plt


RESULT_FILE_PATTERN = re.compile(r"seeded_results_(\d+)_(\d+)\.json$")
DEFAULT_THREADS = [1, 2, 4, 8, 12, 16, 20, 24, 28]


def load_payload(path):
    try:
        with path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
    except (json.JSONDecodeError, OSError):
        return None

    if not isinstance(payload, dict) or not isinstance(payload.get("results"), list):
        return None
    return payload


def first_seed_rows(payload, max_seeds):
    seed_order = []
    seen_seeds = set()
    for row in payload.get("results", []):
        if not isinstance(row, dict):
            continue

        seed = row.get("seed")
        if seed not in seen_seeds:
            seen_seeds.add(seed)
            seed_order.append(seed)

    selected_seeds = set(seed_order[:max_seeds])
    return [
        row
        for row in payload.get("results", [])
        if isinstance(row, dict) and row.get("seed") in selected_seeds
    ]


def distinct_seed_count(rows, value_key):
    return len({
        row.get("seed")
        for row in rows
        if row.get("seed") is not None and row.get(value_key) is not None
    })


def load_parallel_times(results_dir, thread_numbers, max_seeds=5):
    thread_filter = set(thread_numbers)
    grouped = {thread: {} for thread in thread_numbers}

    for path in Path(results_dir).glob("seeded_results_*_*.json"):
        match = RESULT_FILE_PATTERN.match(path.name)
        if not match:
            continue

        banks = int(match.group(1))
        threads = int(match.group(2))
        if threads not in thread_filter:
            continue

        payload = load_payload(path)
        if payload is None:
            continue

        rows = first_seed_rows(payload, max_seeds)
        times = [
            float(row["parallel_time_ms"])
            for row in rows
            if row.get("parallel_time_ms") is not None
        ]
        if not times:
            continue

        grouped[threads][banks] = {
            "avg": sum(times) / len(times),
            "min": min(times),
            "max": max(times),
            "count": len(times),
            "seed_count": distinct_seed_count(rows, "parallel_time_ms"),
            "path": str(path),
        }

    return grouped


def load_sequential_baselines(results_dir, max_seeds=5):
    baselines = {}

    for path in Path(results_dir).glob("seeded_results_*_1.json"):
        match = RESULT_FILE_PATTERN.match(path.name)
        if not match:
            continue

        banks = int(match.group(1))
        payload = load_payload(path)
        if payload is None:
            continue

        rows = first_seed_rows(payload, max_seeds)
        times = [
            float(row["sequential_time_ms"])
            for row in rows
            if row.get("sequential_time_ms") is not None
        ]
        if not times:
            continue

        baselines[banks] = {
            "avg": sum(times) / len(times),
            "min": min(times),
            "max": max(times),
            "count": len(times),
            "seed_count": distinct_seed_count(rows, "sequential_time_ms"),
            "path": str(path),
        }

    return baselines


def load_shock_times(results_dir, thread_numbers, max_seeds=5):
    thread_filter = set(thread_numbers)
    grouped = {thread: {} for thread in thread_numbers}

    for path in Path(results_dir).glob("seeded_results_*_*.json"):
        match = RESULT_FILE_PATTERN.match(path.name)
        if not match:
            continue

        banks = int(match.group(1))
        threads = int(match.group(2))
        if threads not in thread_filter:
            continue

        payload = load_payload(path)
        if payload is None:
            continue

        by_shock = {}
        rows_by_shock = {}
        for row in first_seed_rows(payload, max_seeds):
            if row.get("parallel_time_ms") is None or row.get("shock_percentage") is None:
                continue

            shock = float(row["shock_percentage"])
            by_shock.setdefault(shock, []).append(float(row["parallel_time_ms"]))
            rows_by_shock.setdefault(shock, []).append(row)

        for shock, times in by_shock.items():
            if not times:
                continue

            grouped[threads].setdefault(shock, {})[banks] = {
                "avg": sum(times) / len(times),
                "min": min(times),
                "max": max(times),
                "count": len(times),
                "seed_count": distinct_seed_count(rows_by_shock[shock], "parallel_time_ms"),
                "path": str(path),
            }

    return grouped


def plot_seeded_results(results_dir="results",
                        output_path="results/seeded_parallel_times.png",
                        thread_numbers=None,
                        max_seeds=5,
                        mode="time"):
    if thread_numbers is None:
        thread_numbers = DEFAULT_THREADS

    grouped = load_parallel_times(results_dir, thread_numbers, max_seeds=max_seeds)
    baselines = load_sequential_baselines(results_dir, max_seeds=max_seeds) if mode == "speedup" else {}

    plt.figure(figsize=(12, 7))
    plotted = 0

    for threads in thread_numbers:
        points = grouped.get(threads, {})
        if not points:
            continue

        if mode == "speedup":
            banks = sorted(bank for bank in points if bank in baselines)
            if not banks:
                continue

            averages = [baselines[bank]["avg"] / points[bank]["avg"] for bank in banks]
            minimums = [baselines[bank]["avg"] / points[bank]["max"] for bank in banks]
            maximums = [baselines[bank]["avg"] / points[bank]["min"] for bank in banks]
            show_spread = [
                points[bank]["seed_count"] > 1 and baselines[bank]["seed_count"] > 1
                for bank in banks
            ]
        elif mode == "time-per-bank":
            banks = sorted(points)
            averages = [points[bank]["avg"] / bank for bank in banks]
            minimums = [points[bank]["min"] / bank for bank in banks]
            maximums = [points[bank]["max"] / bank for bank in banks]
            show_spread = [points[bank]["seed_count"] > 1 for bank in banks]
        else:
            banks = sorted(points)
            averages = [points[bank]["avg"] for bank in banks]
            minimums = [points[bank]["min"] for bank in banks]
            maximums = [points[bank]["max"] for bank in banks]
            show_spread = [points[bank]["seed_count"] > 1 for bank in banks]

        line, = plt.plot(banks, averages, marker="o", linewidth=2, label=f"{threads} threads")
        if any(show_spread):
            plt.fill_between(banks, minimums, maximums, where=show_spread, color=line.get_color(), alpha=0.18)
        plotted += 1

    if plotted == 0:
        raise SystemExit(f"No matching seeded result files found in {results_dir}")

    plt.xlabel("Number of banks")
    if mode == "speedup":
        plt.axhline(1.0, color="black", linewidth=1, alpha=0.35)
        plt.ylabel("Speedup vs sequential")
        plt.title("Seeded contagion speedup by bank count and thread count")
    elif mode == "time-per-bank":
        plt.ylabel("Parallel time per bank (ms)")
        plt.title("Seeded contagion runtime per bank by thread count")
    else:
        plt.ylabel("Parallel time (ms)")
        plt.title("Seeded contagion runtime by bank count and thread count")
    plt.grid(True, alpha=0.25)
    plt.legend(title="Thread count")
    plt.tight_layout()

    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output, dpi=200)
    print(f"Wrote plot to {output}")


def shock_output_path(output_path, threads):
    output = Path(output_path)
    if output.suffix.lower() == ".png":
        return output.with_name(f"{output.stem}_threads_{threads}{output.suffix}")
    return output / f"seeded_shock_times_threads_{threads}.png"


def plot_shock_comparison(results_dir="results",
                          output_path="results/shock_time_plots",
                          thread_numbers=None,
                          max_seeds=5):
    if thread_numbers is None:
        thread_numbers = DEFAULT_THREADS

    grouped = load_shock_times(results_dir, thread_numbers, max_seeds=max_seeds)
    wrote_any = False

    for threads in thread_numbers:
        shock_points = grouped.get(threads, {})
        if not shock_points:
            continue

        plt.figure(figsize=(12, 7))
        plotted = 0

        for shock in sorted(shock_points):
            points = shock_points[shock]
            banks = sorted(points)
            if not banks:
                continue

            averages = [points[bank]["avg"] for bank in banks]
            minimums = [points[bank]["min"] for bank in banks]
            maximums = [points[bank]["max"] for bank in banks]
            show_spread = [points[bank]["seed_count"] > 1 for bank in banks]

            line, = plt.plot(
                banks,
                averages,
                marker="o",
                linewidth=2,
                label=f"shock {shock:.2f}",
            )
            if any(show_spread):
                plt.fill_between(banks, minimums, maximums, where=show_spread, color=line.get_color(), alpha=0.18)
            plotted += 1

        if plotted == 0:
            plt.close()
            continue

        plt.xlabel("Number of banks")
        plt.ylabel("Parallel time (ms)")
        plt.title(f"Seeded contagion runtime by shock value ({threads} threads)")
        plt.grid(True, alpha=0.25)
        plt.legend(title="Shock")
        plt.tight_layout()

        output = shock_output_path(output_path, threads)
        output.parent.mkdir(parents=True, exist_ok=True)
        plt.savefig(output, dpi=200)
        plt.close()
        wrote_any = True
        print(f"Wrote plot to {output}")

    if not wrote_any:
        raise SystemExit(f"No matching shock result files found in {results_dir}")


def parse_thread_numbers(value):
    return [int(part.strip()) for part in value.split(",") if part.strip()]


def main():
    parser = argparse.ArgumentParser(
        description="Plot seeded parallel runtimes from results/seeded_results_<banks>_<threads>.json files."
    )
    parser.add_argument("--results-dir", default="results")
    parser.add_argument("--output", default="results/seeded_parallel_times.png")
    parser.add_argument("--mode", choices=["time", "time-per-bank", "speedup", "shock-time"], default="time")
    parser.add_argument(
        "--threads",
        default=",".join(str(thread) for thread in DEFAULT_THREADS),
        help="Comma-separated thread counts to plot.",
    )
    parser.add_argument("--max-seeds", type=int, default=5)
    args = parser.parse_args()

    if args.mode == "shock-time":
        plot_shock_comparison(
            results_dir=args.results_dir,
            output_path=args.output,
            thread_numbers=parse_thread_numbers(args.threads),
            max_seeds=args.max_seeds,
        )
    else:
        plot_seeded_results(
            results_dir=args.results_dir,
            output_path=args.output,
            thread_numbers=parse_thread_numbers(args.threads),
            max_seeds=args.max_seeds,
            mode=args.mode,
        )


if __name__ == "__main__":
    main()
