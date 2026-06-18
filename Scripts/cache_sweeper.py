#!/usr/bin/env python3
"""
cache_sweeper.py
────────────────
Automated parameter-sweep driver for the CacheSimulator binary.

Runs the compiled C++ simulator across a grid of configurations,
parses real stdout output, and produces publication-quality charts
suitable for a GitHub README.

Usage:
    python3 cache_sweeper.py [--exe PATH] [--trace PATH] [--out DIR]
"""

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

# ── optional matplotlib ───────────────────────────────────────────────────────
try:
    import matplotlib.pyplot as plt
    import matplotlib.ticker as mticker
    HAS_PLOT = True
except ImportError:
    HAS_PLOT = False
    print("[warn] matplotlib not found — install with: pip install matplotlib\n"
          "       CSV results will still be saved.")

# ─────────────────────────────────────────────────────────────────────────────
# Data types
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class RunResult:
    l1_sets:      int
    l1_ways:      int
    l2_sets:      int
    l2_ways:      int
    l1_hit_rate:  float
    l2_hit_rate:  float
    l1_miss_rate: float
    l2_miss_rate: float


# ─────────────────────────────────────────────────────────────────────────────
# Parsing helpers
# ─────────────────────────────────────────────────────────────────────────────

_MISS_RE = re.compile(
    r"Layer\s*:\s*(\S+)\s.*?"
    r"Miss Rate\s*:\s*([\d.]+)\s*%",
    re.DOTALL,
)

_HIT_RE = re.compile(
    r"Layer\s*:\s*(\S+)\s.*?"
    r"Hit\s+Rate\s*:\s*([\d.]+)\s*%",
    re.DOTALL,
)


def parse_output(stdout: str) -> tuple[float, float, float, float]:
    """Return (l1_hit, l1_miss, l2_hit, l2_miss) from simulator stdout."""
    hits   = {m.group(1): float(m.group(2)) for m in _HIT_RE.finditer(stdout)}
    misses = {m.group(1): float(m.group(2)) for m in _MISS_RE.finditer(stdout)}

    l1_key = next((k for k in hits   if "L1" in k), None)
    l2_key = next((k for k in misses if "L2" in k), None)

    if l1_key is None or l2_key is None:
        raise ValueError(f"Could not parse simulator output:\n{stdout}")

    return (
        hits.get(l1_key, 0.0),
        misses.get(l1_key, 0.0),
        hits.get(l2_key, 0.0),
        misses.get(l2_key, 0.0),
    )


# ─────────────────────────────────────────────────────────────────────────────
# Runner
# ─────────────────────────────────────────────────────────────────────────────

def run_sim(exe: str, trace: str,
            l1_sets: int, l1_blk: int, l1_ways: int, l1_ev: str, l1_wp: str,
            l2_sets: int, l2_blk: int, l2_ways: int, l2_ev: str, l2_wp: str,
            timeout: int = 30) -> tuple[float, float, float, float]:

    cmd = [
        exe, trace,
        str(l1_sets), str(l1_blk), str(l1_ways), l1_ev, l1_wp,
        str(l2_sets), str(l2_blk), str(l2_ways), l2_ev, l2_wp,
    ]

    result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)

    if result.returncode != 0:
        raise RuntimeError(
            f"Simulator exited {result.returncode}\n"
            f"stderr: {result.stderr.strip()}"
        )

    return parse_output(result.stdout)


# ─────────────────────────────────────────────────────────────────────────────
# Sweep definitions
# ─────────────────────────────────────────────────────────────────────────────

def sweep_associativity(exe: str, trace: str) -> list[RunResult]:
    """Fix cache sizes, vary associativity from 1-way to 16-way."""
    results = []
    for ways in [1, 2, 4, 8, 16]:
        print(f"  L1={ways}-way ...", end="", flush=True)
        l1h, l1m, l2h, l2m = run_sim(
            exe, trace,
            l1_sets=64,  l1_blk=64, l1_ways=ways, l1_ev="LRU", l1_wp="WBWA",
            l2_sets=512, l2_blk=64, l2_ways=8,    l2_ev="LRU", l2_wp="WBWA",
        )
        print(f" L1 hit={l1h:.1f}%  L2 hit={l2h:.1f}%")
        results.append(RunResult(64, ways, 512, 8, l1h, l2h, l1m, l2m))
    return results


def sweep_cache_size(exe: str, trace: str) -> list[RunResult]:
    """Fix associativity=4, vary L1 num_sets (hence size)."""
    results = []
    # sets: 16→1KB, 32→2KB, 64→4KB, 128→8KB, 256→16KB, 512→32KB
    for sets in [16, 32, 64, 128, 256, 512]:
        kb = (sets * 4 * 64) // 1024
        print(f"  L1={kb}KB ...", end="", flush=True)
        l1h, l1m, l2h, l2m = run_sim(
            exe, trace,
            l1_sets=sets, l1_blk=64, l1_ways=4,  l1_ev="LRU", l1_wp="WBWA",
            l2_sets=512,  l2_blk=64, l2_ways=8,  l2_ev="LRU", l2_wp="WBWA",
        )
        print(f" L1 hit={l1h:.1f}%  L2 hit={l2h:.1f}%")
        results.append(RunResult(sets, 4, 512, 8, l1h, l2h, l1m, l2m))
    return results


def sweep_eviction_policy(exe: str, trace: str) -> dict[str, list[float]]:
    """Compare LRU vs FIFO for a range of associativities."""
    ways_list = [1, 2, 4, 8, 16]
    out: dict[str, list[float]] = {"LRU": [], "FIFO": []}

    for policy in ["LRU", "FIFO"]:
        for ways in ways_list:
            print(f"  {policy} {ways}-way ...", end="", flush=True)
            l1h, l1m, _, _ = run_sim(
                exe, trace,
                l1_sets=64,  l1_blk=64, l1_ways=ways, l1_ev=policy,  l1_wp="WBWA",
                l2_sets=512, l2_blk=64, l2_ways=8,    l2_ev="LRU",   l2_wp="WBWA",
            )
            print(f" L1 miss={l1m:.1f}%")
            out[policy].append(l1m)

    return out


# ─────────────────────────────────────────────────────────────────────────────
# Plotting
# ─────────────────────────────────────────────────────────────────────────────

def plot_associativity(results: list[RunResult], out_dir: str) -> None:
    ways   = [r.l1_ways    for r in results]
    l1_hit = [r.l1_hit_rate for r in results]
    l2_hit = [r.l2_hit_rate for r in results]

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.plot(ways, l1_hit, "o-", color="#E63946", lw=2, label="L1 Hit Rate")
    ax.plot(ways, l2_hit, "s-", color="#1D3557", lw=2, label="L2 Hit Rate")
    ax.set_title("Hit Rate vs. Associativity", fontsize=14, fontweight="bold")
    ax.set_xlabel("Associativity (ways)")
    ax.set_ylabel("Hit Rate (%)")
    ax.set_xticks(ways)
    ax.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.1f%%"))
    ax.grid(True, linestyle="--", alpha=0.5)
    ax.legend()
    fig.tight_layout()
    path = os.path.join(out_dir, "assoc_vs_hitrate.png")
    fig.savefig(path, dpi=200)
    print(f"  Saved → {path}")
    plt.close(fig)


def plot_cache_size(results: list[RunResult], out_dir: str) -> None:
    kb     = [(r.l1_sets * 4 * 64) // 1024 for r in results]
    l1_hit = [r.l1_hit_rate for r in results]
    l2_hit = [r.l2_hit_rate for r in results]

    fig, ax = plt.subplots(figsize=(9, 5))
    ax.plot(kb, l1_hit, "o-", color="#E63946", lw=2, label="L1 Hit Rate")
    ax.plot(kb, l2_hit, "s-", color="#1D3557", lw=2, label="L2 Hit Rate")
    ax.set_title("Hit Rate vs. L1 Cache Size", fontsize=14, fontweight="bold")
    ax.set_xlabel("L1 Cache Size (KB)")
    ax.set_ylabel("Hit Rate (%)")
    ax.set_xticks(kb)
    ax.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.1f%%"))
    ax.grid(True, linestyle="--", alpha=0.5)
    ax.legend()
    fig.tight_layout()
    path = os.path.join(out_dir, "size_vs_hitrate.png")
    fig.savefig(path, dpi=200)
    print(f"  Saved → {path}")
    plt.close(fig)


def plot_eviction(data: dict[str, list[float]], out_dir: str) -> None:
    ways = [1, 2, 4, 8, 16]
    fig, ax = plt.subplots(figsize=(9, 5))
    ax.plot(ways, data["LRU"],  "o-", color="#2A9D8F", lw=2, label="LRU")
    ax.plot(ways, data["FIFO"], "s-", color="#E76F51", lw=2, label="FIFO")
    ax.set_title("L1 Miss Rate: LRU vs FIFO", fontsize=14, fontweight="bold")
    ax.set_xlabel("Associativity (ways)")
    ax.set_ylabel("Miss Rate (%)")
    ax.set_xticks(ways)
    ax.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.1f%%"))
    ax.grid(True, linestyle="--", alpha=0.5)
    ax.legend()
    fig.tight_layout()
    path = os.path.join(out_dir, "lru_vs_fifo.png")
    fig.savefig(path, dpi=200)
    print(f"  Saved → {path}")
    plt.close(fig)


# ─────────────────────────────────────────────────────────────────────────────
# CSV export
# ─────────────────────────────────────────────────────────────────────────────

def save_csv(rows: list[RunResult], path: str) -> None:
    with open(path, "w") as f:
        f.write("l1_sets,l1_ways,l2_sets,l2_ways,"
                "l1_hit_rate,l1_miss_rate,l2_hit_rate,l2_miss_rate\n")
        for r in rows:
            f.write(f"{r.l1_sets},{r.l1_ways},{r.l2_sets},{r.l2_ways},"
                    f"{r.l1_hit_rate:.2f},{r.l1_miss_rate:.2f},"
                    f"{r.l2_hit_rate:.2f},{r.l2_miss_rate:.2f}\n")
    print(f"  CSV → {path}")


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(description="Cache simulator parameter sweeper")
    parser.add_argument("--exe",   default="../build/cache_sim",
                        help="Path to compiled cache_sim binary")
    parser.add_argument("--trace", default="../Traces/benchmark.trace",
                        help="Path to memory trace file")
    parser.add_argument("--out",   default=".",
                        help="Output directory for charts and CSV")
    args = parser.parse_args()

    exe   = os.path.abspath(args.exe)
    trace = os.path.abspath(args.trace)
    out   = os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)

    if not os.path.exists(exe):
        sys.exit(f"[error] Binary not found: {exe}\n"
                 "        Build with: cd build && cmake .. && cmake --build .")
    if not os.path.exists(trace):
        sys.exit(f"[error] Trace file not found: {trace}")

    print("═" * 52)
    print("  Cache Simulator — Automated Parameter Sweeper")
    print("═" * 52)

    # ── Sweep 1: associativity ────────────────────────────────────────────────
    print("\n[1/3] Sweeping associativity (1-way → 16-way):")
    assoc_results = sweep_associativity(exe, trace)
    save_csv(assoc_results, os.path.join(out, "assoc_sweep.csv"))
    if HAS_PLOT:
        plot_associativity(assoc_results, out)

    # ── Sweep 2: cache size ───────────────────────────────────────────────────
    print("\n[2/3] Sweeping L1 cache size (1 KB → 32 KB):")
    size_results = sweep_cache_size(exe, trace)
    save_csv(size_results, os.path.join(out, "size_sweep.csv"))
    if HAS_PLOT:
        plot_cache_size(size_results, out)

    # ── Sweep 3: eviction policy comparison ──────────────────────────────────
    print("\n[3/3] Comparing LRU vs FIFO:")
    evict_data = sweep_eviction_policy(exe, trace)
    if HAS_PLOT:
        plot_eviction(evict_data, out)

    print("\n✓ All sweeps complete.\n")


if __name__ == "__main__":
    main()
