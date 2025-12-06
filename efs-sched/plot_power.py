#!/usr/bin/env python3
import re
import sys
import matplotlib.pyplot as plt

def parse_log(path):
    """
    Parse out.log and return:
      cpu_times_ns: list of cpu_time values (ns) from BPF total map
      wall_times_s: list of "Real time since poller start" values (seconds)
      powers:       list of PID power values
    Lists are aligned by sample index.
    """
    cpu_times_ns = []
    wall_times_s = []
    powers = []

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        cur_cpu = None
        cur_wall = None
        expect_power_value = False

        for raw_line in f:
            # strip ANSI clear-screen sequences
            line = raw_line.replace("\x1b[H\x1b[J", "")
            line = line.rstrip("\n")

            # Match wall time line:
            # "Real time since poller start: 93.021584 s"
            m_wall = re.search(r"Real time since poller start:\s+([\d\.]+)\s*s", line)
            if m_wall:
                cur_wall = float(m_wall.group(1))
                continue

            # Match cpu_time line:
            # "  cpu_time:  131402767668 ns"
            m_cpu = re.search(r"cpu_time:\s+(\d+)\s+ns", line)
            if m_cpu:
                cur_cpu = int(m_cpu.group(1))
                continue

            # Match header line for PID/power table
            if line.strip().startswith("PID") and "power" in line:
                expect_power_value = True
                continue

            # Next line after header should be: "83959    28"
            if expect_power_value:
                parts = line.split()
                if len(parts) >= 2 and cur_cpu is not None:
                    try:
                        power = int(parts[1])
                    except ValueError:
                        power = None

                    if power is not None:
                        cpu_times_ns.append(cur_cpu)
                        wall_times_s.append(cur_wall)  # may be None if line missing
                        powers.append(power)

                expect_power_value = False

    return cpu_times_ns, wall_times_s, powers

def rolling_mean(values, window):
    if window <= 1 or window > len(values):
        return values
    out = []
    s = 0.0
    for i, v in enumerate(values):
        s += v
        if i >= window:
            s -= values[i - window]
        if i >= window - 1:
            out.append(s / window)
        else:
            out.append(values[i])  # before full window, just use raw
    return out

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} out.log [output.png] [--wall]")
        sys.exit(1)

    log_path = sys.argv[1]
    out_path = None
    use_wall = False

    # Parse optional args
    for arg in sys.argv[2:]:
        if arg == "--wall":
            use_wall = True
        elif not arg.startswith("-") and out_path is None:
            out_path = arg
        else:
            print(f"Warning: unknown or extra arg '{arg}', ignoring.")

    cpu_times_ns, wall_times_s, powers = parse_log(log_path)
    if not powers:
        print("No samples found in log.")
        sys.exit(1)

    # Choose x-axis: wall-time or cpu-time
    if use_wall and any(w is not None for w in wall_times_s):
        # Use wall time as-is, but normalize so it starts at 0
        # (Real time since poller start is already relative, but we normalize anyway)
        first_valid = next(i for i, w in enumerate(wall_times_s) if w is not None)
        t0 = wall_times_s[first_valid]
        times = [(w - t0) if w is not None else 0.0 for w in wall_times_s]
        x_label = "Wall time since poller start (s)"
    else:
        # Fallback / default: CPU time from total map
        t0 = cpu_times_ns[0]
        times = [(t - t0) / 1e9 for t in cpu_times_ns]
        x_label = "CPU time (s, from BPF total.cpu_time)"

    # Smoothing window ~ 30 points across the plot
    smooth_window = max(1, len(powers) // 30)
    smoothed = rolling_mean(powers, smooth_window)

    plt.figure(figsize=(10, 6))
    plt.plot(times, powers, linewidth=0.7, label="Power (raw)")
    if smooth_window > 1:
        plt.plot(times, smoothed, linewidth=2.0,
                 label=f"Rolling mean (window={smooth_window})")

    plt.xlabel(x_label)
    plt.ylabel("Power (EMA)")
    plt.title("PID Power vs Time from out.log")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    if out_path:
        plt.savefig(out_path, dpi=150, bbox_inches="tight")
        print(f"Saved plot to {out_path}")
    else:
        plt.show()

if __name__ == "__main__":
    main()
