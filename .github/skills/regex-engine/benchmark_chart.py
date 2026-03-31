#!/usr/bin/env python3
"""Run the Trieste regex benchmark N times, take medians, and produce a chart.

Combines multi-pass benchmarking (warmup + N runs → median per case) with
chart rendering and a markdown report.  Intended for generating a
representative chart suitable for inclusion in a PR.

Prerequisites:
  - The benchmark binary must exist.  Build with:
      cmake -S . -B build-prof -DTRIESTE_ENABLE_TESTING=ON \
            -DTRIESTE_BUILD_REGEX_BENCHMARK=ON
      cmake --build build-prof --target trieste_regex_engine_benchmark
  - Python dependencies: matplotlib, numpy
      .venv/bin/python -m pip install matplotlib numpy

Usage:
  .venv/bin/python .github/skills/regex-engine/benchmark_chart.py
  .venv/bin/python .github/skills/regex-engine/benchmark_chart.py --runs 7 --build-dir build-prof
  .venv/bin/python .github/skills/regex-engine/benchmark_chart.py --input .copilot/baseline/phase3_baseline.json
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path


# ---------------------------------------------------------------------------
# Benchmark execution & parsing
# ---------------------------------------------------------------------------

def run_benchmark(build_dir: Path) -> str:
    binary = build_dir / "test" / "trieste_regex_engine_benchmark"
    proc = subprocess.run(
        [str(binary)],
        cwd=str(build_dir),
        text=True,
        capture_output=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise RuntimeError(
            f"trieste_regex_engine_benchmark failed (rc={proc.returncode})"
        )
    return proc.stdout


def parse_case_lines(stdout: str) -> dict[str, dict[str, float]]:
    cases: dict[str, dict[str, float]] = {}
    for m in re.finditer(r"^BENCH case=(\S+)\s+(.+)$", stdout, flags=re.M):
        name = m.group(1)
        vals: dict[str, float] = {}
        for token in m.group(2).split():
            if "=" in token:
                k, v = token.split("=", 1)
                try:
                    vals[k] = float(v)
                except ValueError:
                    pass
        cases[name] = vals
    return cases


def parse_summary_line(stdout: str) -> dict[str, float]:
    m = re.search(r"^BENCH summary\s+(.+)$", stdout, flags=re.M)
    if not m:
        return {}
    vals: dict[str, float] = {}
    for token in m.group(1).split():
        if "=" in token:
            k, v = token.split("=", 1)
            try:
                vals[k] = float(v)
            except ValueError:
                pass
    return vals


def median(values: list[float]) -> float:
    return float(statistics.median(values)) if values else float("nan")


def mad(values: list[float]) -> float:
    if len(values) < 2:
        return 0.0
    m = statistics.median(values)
    return float(statistics.median([abs(v - m) for v in values]))


# ---------------------------------------------------------------------------
# Multi-pass collection → median
# ---------------------------------------------------------------------------

METRICS_KEYS = [
    "m_ratio", "cm_ratio",
    "trieste_m_ns", "re2_m_ns",
    "trieste_cm_ns", "re2_cm_ns",
]
SUMMARY_KEYS = ["m_ratio", "cm_ratio"]


def collect_medians(
    build_dir: Path, runs: int
) -> tuple[dict[str, dict[str, float]], dict[str, float], list[dict]]:
    """Run the benchmark `runs` times (with a warmup) and return medians."""
    per_case: dict[str, dict[str, list[float]]] = {}
    summary_values: dict[str, list[float]] = {k: [] for k in SUMMARY_KEYS}
    raw_runs: list[dict] = []

    print("Warmup run...", flush=True)
    run_benchmark(build_dir)

    for i in range(1, runs + 1):
        print(f"Run {i}/{runs}...", flush=True)
        stdout = run_benchmark(build_dir)
        cases = parse_case_lines(stdout)
        summary = parse_summary_line(stdout)
        raw_runs.append({"cases": cases, "summary": summary})

        for case_name, vals in cases.items():
            if case_name not in per_case:
                per_case[case_name] = {k: [] for k in METRICS_KEYS}
            for k in METRICS_KEYS:
                if k in vals:
                    per_case[case_name][k].append(vals[k])

        for k in SUMMARY_KEYS:
            if k in summary:
                summary_values[k].append(summary[k])

    medians: dict[str, dict[str, float]] = {}
    for case_name, metrics in sorted(per_case.items()):
        medians[case_name] = {}
        for k, values in metrics.items():
            medians[case_name][k] = median(values)
            medians[case_name][f"{k}_mad"] = mad(values)

    summary_medians: dict[str, float] = {}
    for k, values in summary_values.items():
        summary_medians[k] = median(values)
        summary_medians[f"{k}_mad"] = mad(values)

    return medians, summary_medians, raw_runs


def print_table(
    medians: dict[str, dict[str, float]],
    summary_medians: dict[str, float],
) -> None:
    print()
    hdr = (
        f"{'Case':<32} {'m_ratio':>8} {'±MAD':>6} "
        f"{'cm_ratio':>9} {'tri_m_ns':>9} {'re2_m_ns':>9}"
    )
    print(hdr)
    print("-" * 80)
    sorted_cases = sorted(
        medians.items(), key=lambda x: x[1].get("m_ratio", 0), reverse=True
    )
    for case_name, m in sorted_cases:
        print(
            f"{case_name:<32} {m.get('m_ratio', 0):>8.2f} "
            f"{m.get('m_ratio_mad', 0):>6.2f} "
            f"{m.get('cm_ratio', 0):>9.2f} "
            f"{m.get('trieste_m_ns', 0):>9.1f} "
            f"{m.get('re2_m_ns', 0):>9.1f}"
        )
    print("-" * 80)
    print(
        f"{'SUMMARY':<32} "
        f"{summary_medians.get('m_ratio', 0):>8.2f} "
        f"{summary_medians.get('m_ratio_mad', 0):>6.2f} "
        f"{summary_medians.get('cm_ratio', 0):>9.2f}"
    )
    print()


# ---------------------------------------------------------------------------
# Chart rendering
# ---------------------------------------------------------------------------

def render_png(
    medians: dict[str, dict[str, float]],
    summary_medians: dict[str, float],
    output_path: Path,
) -> None:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError as exc:
        raise RuntimeError(
            "matplotlib is required.  Install with:\n"
            "  .venv/bin/python -m pip install matplotlib numpy"
        ) from exc

    def geometric_mean(values: "np.ndarray") -> float:
        return float(np.exp(np.mean(np.log(values))))

    cases = list(medians.keys())
    tri_cm = np.array([medians[c].get("trieste_cm_ns", 0) for c in cases])
    re2_cm = np.array([medians[c].get("re2_cm_ns", 0) for c in cases])
    tri_m = np.array([medians[c].get("trieste_m_ns", 0) for c in cases])
    re2_m = np.array([medians[c].get("re2_m_ns", 0) for c in cases])

    cases.append("overall_gm")
    tri_cm = np.append(tri_cm, geometric_mean(tri_cm))
    re2_cm = np.append(re2_cm, geometric_mean(re2_cm))
    tri_m = np.append(tri_m, geometric_mean(tri_m))
    re2_m = np.append(re2_m, geometric_mean(re2_m))

    x = np.arange(len(cases))
    width = 0.36

    fig, axes = plt.subplots(1, 2, figsize=(14, 5), constrained_layout=True)

    def draw_panel(
        ax: "plt.Axes",
        tri: "np.ndarray",
        re2: "np.ndarray",
        title: str,
    ) -> None:
        ax.bar(x - width / 2, tri, width, label="Trieste", color="#1f77b4")
        ax.bar(x + width / 2, re2, width, label="RE2", color="#ff7f0e")
        ax.set_title(title)
        ax.set_xticks(x)
        ax.set_xticklabels(cases, rotation=30, ha="right")
        ax.set_ylabel("ns")
        ax.grid(axis="y", alpha=0.25)
        ax.legend()

        ratios = tri / re2
        y_max = float(max(np.max(tri), np.max(re2)))
        for i, ratio in enumerate(ratios):
            top = max(float(tri[i]), float(re2[i]))
            y = top + 0.03 * y_max
            ax.text(
                i, y, f"{ratio:.2f}x", ha="center", va="bottom", fontsize=8
            )
        ax.set_ylim(0, y_max * 1.18)

    draw_panel(axes[0], tri_cm, re2_cm, "Compile + Match (ns)")
    draw_panel(axes[1], tri_m, re2_m, "Match Only (ns)")

    cm = summary_medians.get("cm_ratio", float("nan"))
    mr = summary_medians.get("m_ratio", float("nan"))
    subtitle = f"median of 5 runs — cm_ratio={cm:.2f}  m_ratio={mr:.2f}"
    fig.suptitle(
        f"Trieste Regex Benchmark vs RE2\n{subtitle}", fontsize=13
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=180)
    plt.close(fig)


# ---------------------------------------------------------------------------
# Markdown report
# ---------------------------------------------------------------------------

def write_report(
    medians: dict[str, dict[str, float]],
    summary_medians: dict[str, float],
    report_path: Path,
    runs: int,
) -> None:
    rows = [
        {"case": c, "tri_cm": m["trieste_cm_ns"], "re2_cm": m["re2_cm_ns"],
         "tri_m": m["trieste_m_ns"], "re2_m": m["re2_m_ns"]}
        for c, m in medians.items()
    ]
    cm_sorted = sorted(rows, key=lambda r: r["tri_cm"] / r["re2_cm"], reverse=True)
    m_sorted = sorted(rows, key=lambda r: r["tri_m"] / r["re2_m"], reverse=True)

    lines: list[str] = [
        "# Regex Benchmark Report",
        "",
        f"Median of {runs} benchmark passes (with 1 warmup pass discarded).",
        "",
        "## Summary",
    ]
    for key in ("cm_ratio", "m_ratio"):
        if key in summary_medians:
            v = summary_medians[key]
            m = summary_medians.get(f"{key}_mad", 0)
            lines.append(f"- {key}: {v:.2f} (±{m:.2f} MAD)")

    lines += ["", "## Slowest Compile+Match Ratios (Trieste/RE2)"]
    for row in cm_sorted[:5]:
        ratio = row["tri_cm"] / row["re2_cm"]
        lines.append(f"- {row['case']}: {ratio:.2f}x")

    lines += ["", "## Slowest Match-Only Ratios (Trieste/RE2)"]
    for row in m_sorted[:5]:
        ratio = row["tri_m"] / row["re2_m"]
        lines.append(f"- {row['case']}: {ratio:.2f}x")

    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines) + "\n")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Run regex benchmark N times, compute medians, render chart.",
    )
    p.add_argument(
        "--runs", type=int, default=5,
        help="Number of benchmark passes after warmup (default: 5).",
    )
    p.add_argument(
        "--build-dir", type=Path, default=Path("build-prof"),
        help="Build directory containing test/trieste_regex_engine_benchmark.",
    )
    p.add_argument(
        "--input", type=Path, default=None,
        help="Existing phase3_baseline.json; skip running benchmarks.",
    )
    p.add_argument(
        "--chart", type=Path,
        default=Path("benchmarks/regex_benchmark_vs_re2.png"),
        help="Output PNG path (relative to repo root).",
    )
    p.add_argument(
        "--report", type=Path,
        default=Path("benchmarks/regex_benchmark_report.md"),
        help="Output markdown report path (relative to repo root).",
    )
    p.add_argument(
        "--json", type=Path,
        default=Path("benchmarks/regex_benchmark_data.json"),
        help="Output JSON path for raw data (relative to repo root).",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[3]  # .github/skills/regex-engine → repo
    build_dir = (repo_root / args.build_dir).resolve()

    if args.input is not None:
        data = json.loads(args.input.read_text())
        medians = data["medians_per_case"]
        summary_medians = data["summary_medians"]
        runs = data.get("runs", 5)
        print(f"Loaded {len(medians)} cases from {args.input}")
    else:
        binary = build_dir / "test" / "trieste_regex_engine_benchmark"
        if not binary.exists():
            print(
                f"ERROR: benchmark binary not found at {binary}\n"
                "Build with:\n"
                "  cmake -S . -B build-prof "
                "-DTRIESTE_ENABLE_TESTING=ON "
                "-DTRIESTE_BUILD_REGEX_BENCHMARK=ON\n"
                "  cmake --build build-prof "
                "--target trieste_regex_engine_benchmark",
                file=sys.stderr,
            )
            return 1

        runs = args.runs
        medians, summary_medians, raw_runs = collect_medians(build_dir, runs)

        json_path = (repo_root / args.json).resolve()
        json_path.parent.mkdir(parents=True, exist_ok=True)
        artifact = {
            "timestamp_epoch_s": time.time(),
            "runs": runs,
            "build_dir": str(build_dir),
            "medians_per_case": medians,
            "summary_medians": summary_medians,
            "raw_runs": raw_runs,
        }
        json_path.write_text(
            json.dumps(artifact, indent=2, sort_keys=True) + "\n"
        )
        print(f"Wrote JSON: {json_path}")

    print_table(medians, summary_medians)

    chart_path = (repo_root / args.chart).resolve()
    render_png(medians, summary_medians, chart_path)
    print(f"Wrote chart: {chart_path}")

    report_path = (repo_root / args.report).resolve()
    write_report(medians, summary_medians, report_path, runs)
    print(f"Wrote report: {report_path}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\nInterrupted", file=sys.stderr)
        raise SystemExit(130)
