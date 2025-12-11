#!/usr/bin/env python3
"""
Parse an energy-fair scheduler log and plot System totals energy/uncore_energy
against wall time, along with their time derivatives (power), with a rolling
average applied to the power for smoothing.

Two figures are created:
  1) energy (J) vs time + power (W, smoothed)
  2) uncore energy (J) vs time + uncore power (W, smoothed)

Usage:
    python plot_energy_log.py /path/to/log.txt
    # optionally save instead of showing:
    python plot_energy_log.py /path/to/log.txt --prefix energy_plots
    # adjust smoothing window (in samples) for power:
    python plot_energy_log.py /path/to/log.txt --power-window 10
"""

import argparse
import re
from typing import List, Tuple, Optional

import matplotlib.pyplot as plt


TIME_RE = re.compile(r"Real time since poller start:\s*([0-9.]+)\s*s")
ENERGY_RE = re.compile(r"^\s*energy:\s*([0-9]+)")
UNCORE_ENERGY_RE = re.compile(r"^\s*uncore_energy:\s*([0-9]+)")


def parse_log(
    path: str,
) -> Tuple[List[float], List[Optional[int]], List[Optional[int]]]:
    """
    Parse the log file to extract wall time, energy, and uncore_energy
    for each 'System totals (total)' block.

    Returns:
        times: list of wall times in seconds
        energies: list of energy values in Joules (or None if missing)
        uncore_energies: list of uncore_energy values in Joules (or None if missing)
    """
    times: List[float] = []
    energies: List[Optional[int]] = []
    uncore_energies: List[Optional[int]] = []

    current_time: Optional[float] = None
    in_totals_block = False
    current_energy: Optional[int] = None
    current_uncore: Optional[int] = None

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            # Track the current wall time
            m_time = TIME_RE.search(line)
            if m_time:
                current_time = float(m_time.group(1))

            # Detect start of system totals block
            if line.strip() == "=== System totals (total) ===":
                in_totals_block = True
                current_energy = None
                current_uncore = None
                continue

            if in_totals_block:
                # Parse energy line
                m_energy = ENERGY_RE.search(line)
                if m_energy:
                    current_energy = int(m_energy.group(1))

                # Parse uncore_energy line
                m_uncore = UNCORE_ENERGY_RE.search(line)
                if m_uncore:
                    current_uncore = int(m_uncore.group(1))
                    # In the original log, uncore_energy is last in this block,
                    # so we can flush the record now.
                    if current_time is not None:
                        times.append(current_time)
                        energies.append(current_energy)
                        uncore_energies.append(current_uncore)
                    # Finished this totals block
                    in_totals_block = False

    return times, energies, uncore_energies


def compute_derivative(
    times: List[float],
    values: List[Optional[int]],
) -> Tuple[List[float], List[float]]:
    """
    Compute numerical derivative (power) given times and values.

    - Skips entries where value is None.
    - Uses forward differences on valid consecutive points.
    - Only uses pairs where t[i] > t[i-1].

    Returns:
        deriv_times: times associated with the derivative (right endpoint)
        deriv_values: derivative values (e.g., Watts)
    """
    # Filter out None values first
    filtered_pairs = [(t, v) for t, v in zip(times, values) if v is not None]

    if len(filtered_pairs) < 2:
        return [], []

    filtered_t = [p[0] for p in filtered_pairs]
    filtered_v = [p[1] for p in filtered_pairs]

    deriv_times: List[float] = []
    deriv_values: List[float] = []

    for i in range(1, len(filtered_t)):
        dt = filtered_t[i] - filtered_t[i - 1]
        if dt <= 0:
            # Skip non-positive time differences
            continue
        dv = filtered_v[i] - filtered_v[i - 1]
        deriv_times.append(filtered_t[i])  # associate with the later time
        deriv_values.append(dv / dt)

    return deriv_times, deriv_values


def moving_average(
    values: List[float],
    window: int,
) -> List[float]:
    """
    Compute a trailing moving average over 'values' with the given window size.

    For index i, we average over values[max(0, i-window+1): i+1], so the
    smoothed series has the same length as 'values'.

    If window <= 1 or len(values) < 2, the original values are returned.
    """
    if window <= 1 or len(values) < 2:
        return values[:]

    smoothed: List[float] = []
    for i in range(len(values)):
        start = max(0, i - window + 1)
        window_vals = values[start : i + 1]
        smoothed.append(sum(window_vals) / len(window_vals))
    return smoothed


def plot_series_with_derivative(
    times: List[float],
    values: List[Optional[int]],
    value_ylabel: str,
    power_ylabel: str,
    title: str,
    power_window: int,
    output_path: Optional[str] = None,
) -> None:
    """
    Plot a value series vs wall time (left y-axis) and its derivative (power)
    vs wall time (right y-axis).

    - Skips None values for the main series.
    - Computes derivative and applies a rolling average (window = power_window).
    """
    # Filter out missing values for main series
    filtered_t = [t for t, v in zip(times, values) if v is not None]
    filtered_v = [v for v in values if v is not None]

    if not filtered_t:
        print(f"No valid data points for plot '{title}', skipping.")
        return

    # Compute derivative = power
    deriv_t, deriv_v = compute_derivative(times, values)

    # Smooth the power with a moving average
    deriv_v_smoothed = moving_average(deriv_v, power_window) if deriv_v else []

    fig, ax1 = plt.subplots()

    # Main series: energy / uncore energy
    line1, = ax1.plot(filtered_t, filtered_v, marker="o", label=value_ylabel)
    ax1.set_xlabel("Wall time since poller start (s)")
    ax1.set_ylabel(value_ylabel)
    ax1.tick_params(axis="y")

    # Power series on secondary axis
    ax2 = ax1.twinx()
    line2 = None
    if deriv_t and deriv_v_smoothed:
        line2, = ax2.plot(
            deriv_t,
            deriv_v_smoothed,
            marker="x",
            linestyle="--",
            label=f"{power_ylabel} (rolling avg, window={power_window})",
        )
    ax2.set_ylabel(power_ylabel)
    ax2.tick_params(axis="y")

    ax1.set_title(title)

    # Build combined legend
    lines = [line1]
    labels = [value_ylabel]
    if line2 is not None:
        lines.append(line2)
        labels.append(f"{power_ylabel} (rolling avg, window={power_window})")
    fig.legend(lines, labels, loc="upper left", bbox_to_anchor=(0.1, 0.95))

    fig.tight_layout()

    if output_path:
        plt.savefig(output_path, bbox_inches="tight")
        print(f"Saved plot to {output_path}")
        plt.close(fig)
    else:
        plt.show()


def plot_energy_and_uncore(
    times: List[float],
    energies: List[Optional[int]],
    uncore_energies: List[Optional[int]],
    prefix: Optional[str],
    power_window: int,
) -> None:
    """
    Create two graphs:
      1) energy (J) + smoothed power (W) vs wall time
      2) uncore energy (J) + smoothed uncore power (W) vs wall time

    If prefix is provided, save as:
        {prefix}_energy.png
        {prefix}_uncore_energy.png
    Otherwise, show interactively.
    """
    if not times:
        raise ValueError("No data points parsed from log (times list is empty).")

    # Energy + smoothed power plot
    energy_out = f"{prefix}_energy.png" if prefix else None
    plot_series_with_derivative(
        times,
        energies,
        value_ylabel="Energy (J)",
        power_ylabel="Power (W)",
        title="System totals: energy and power vs wall time",
        power_window=power_window,
        output_path=energy_out,
    )

    # Uncore energy + smoothed uncore power plot
    uncore_out = f"{prefix}_uncore_energy.png" if prefix else None
    plot_series_with_derivative(
        times,
        uncore_energies,
        value_ylabel="Uncore energy (J)",
        power_ylabel="Uncore power (W)",
        title="System totals: uncore energy and power vs wall time",
        power_window=power_window,
        output_path=uncore_out,
    )


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Parse energy-fair scheduler logs and plot System totals "
            "energy/uncore_energy and their derivatives (power) vs wall time, "
            "with a rolling average applied to the power."
        )
    )
    parser.add_argument("logfile", help="Path to the log file")
    parser.add_argument(
        "--prefix",
        "-p",
        help=(
            "Filename prefix for saving plots (e.g. 'energy_plots'). "
            "If omitted, plots are shown interactively instead of saved."
        ),
    )
    parser.add_argument(
        "--power-window",
        type=int,
        default=5,
        help=(
            "Rolling window size (in samples) for smoothing power "
            "(default: 5)."
        ),
    )
    args = parser.parse_args()

    times, energies, uncore_energies = parse_log(args.logfile)
    print(f"Parsed {len(times)} System totals samples from {args.logfile}")
    plot_energy_and_uncore(
        times,
        energies,
        uncore_energies,
        prefix=args.prefix,
        power_window=args.power_window,
    )


if __name__ == "__main__":
    main()
