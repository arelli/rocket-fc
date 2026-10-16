#!/usr/bin/env python3
"""rocket-fc ground station: records and displays live telemetry.

Usage
  python tools/ground_station.py COM5              # Windows (see Device Manager)
  python tools/ground_station.py /dev/ttyACM0      # Linux / macOS
  python tools/ground_station.py COM6 --dump       # download the FLIGHT computer's onboard log

Live mode: connect the GROUND kit by USB. Every line it prints is saved
unchanged to logs/ground_<date>_<time>.csv; a one-line summary is shown and
events are printed as they arrive.

Dump mode: connect the FLIGHT computer by USB after recovery (without
switching it off: the onboard log is in RAM). Saves logs/onboard_<...>.csv.

Requires: pip install pyserial
CSV columns: see lib/fc_core/src/fc/csv.h and docs/TELEMETRY.md.
"""

import argparse
import datetime as dt
import os
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial is missing: pip install pyserial")

TLM_FIELDS = ("seq t_ms state alt_m vel_mps ax_g ay_g az_g gx_dps gy_dps gz_dps "
              "mx_uT my_uT mz_uT p_pa temp_c batt_v flags").split()
EVT_FIELDS = "seq t_ms state event arg".split()
FLAG_NAMES = ["CONT", "ARMED", "FIRED", "IMU", "MAG", "BARO", "BATT_LOW", "RESUMED"]


def flags_text(value):
    v = int(value, 16)
    return " ".join(n for i, n in enumerate(FLAG_NAMES) if v & (1 << i)) or "-"


def log_path(prefix):
    os.makedirs("logs", exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join("logs", f"{prefix}_{stamp}.csv")


def parse(line):
    """Returns ('T'|'E', dict) or (None, None) for comments / unknown lines."""
    parts = line.split(",")
    if parts[0] == "T" and len(parts) >= 1 + len(TLM_FIELDS):
        d = dict(zip(TLM_FIELDS, parts[1:]))
        extra = parts[1 + len(TLM_FIELDS):]
    elif parts[0] == "E" and len(parts) >= 1 + len(EVT_FIELDS):
        d = dict(zip(EVT_FIELDS, parts[1:]))
        extra = parts[1 + len(EVT_FIELDS):]
    else:
        return None, None
    if len(extra) >= 2:
        d["rssi"], d["snr"] = extra[0], extra[1]
    return parts[0], d


def live(port, baud):
    path = log_path("ground")
    print(f"recording to {path}  (Ctrl+C to stop)")
    max_alt = 0.0
    last_seq = None
    received = lost = 0
    with serial.Serial(port, baud, timeout=0.5) as ser, open(path, "w", buffering=1) as out:
        ser.write(b"\n")  # ask the ground station to re-print its CSV headers
        while True:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if not line:
                continue
            out.write(f"{line}\n")
            kind, d = parse(line)
            if kind is None:
                if line.startswith("#"):
                    print(f"\n{line}")
                continue
            seq = int(d["seq"])
            if last_seq is not None:
                gap = (seq - last_seq - 1) % 65536
                if gap < 1000:  # larger jumps = flight computer rebooted
                    lost += gap
            last_seq = seq
            received += 1
            loss = 100.0 * lost / max(1, received + lost)
            if kind == "E":
                print(f"\n*** {d['event']}  arg={d['arg']}  state={d['state']}  "
                      f"t={int(d['t_ms']) / 1000:.2f}s")
                continue
            alt = float(d["alt_m"])
            max_alt = max(max_alt, alt)
            sys.stdout.write(
                f"\r{d['state']:<8} alt {alt:8.1f} m  vel {float(d['vel_mps']):7.1f} m/s  "
                f"max {max_alt:7.1f} m  batt {float(d['batt_v']):.2f} V  "
                f"rssi {d.get('rssi', '?')} dBm  loss {loss:4.1f}%  [{flags_text(d['flags'])}]   ")
            sys.stdout.flush()


def dump(port, baud):
    path = log_path("onboard")
    with serial.Serial(port, baud, timeout=2) as ser, open(path, "w") as out:
        ser.reset_input_buffer()
        ser.write(b"dump\n")
        n = 0
        started = False
        deadline = time.time() + 600
        while time.time() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if line.startswith("# onboard log"):
                started = True
            if not started:
                if line.startswith("refused"):
                    sys.exit("the flight computer refused: it thinks it is in flight")
                continue
            out.write(f"{line}\n")
            if line.startswith(("T,", "E,")):
                n += 1
                if n % 1000 == 0:
                    print(f"\r{n} frames", end="")
            if line == "# end of log":
                print(f"\r{n} frames saved to {path}")
                return
    sys.exit("timed out waiting for the end of the log")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--dump", action="store_true",
                    help="download the onboard log from the flight computer")
    a = ap.parse_args()
    try:
        dump(a.port, a.baud) if a.dump else live(a.port, a.baud)
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
