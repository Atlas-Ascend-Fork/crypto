#!/usr/bin/env python3
"""重复运行 SHAKE 设备代表用例。"""

import argparse
from datetime import datetime
from pathlib import Path
import subprocess


def positive(value):
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/device"))
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--runs", type=positive, default=100)
    parser.add_argument("--timeout", type=positive, default=600)
    args = parser.parse_args()
    if args.device < 0:
        parser.error("device must be nonnegative")
    executable = args.build_dir.resolve() / "tests/shake_device_test"
    if not executable.is_file():
        parser.error("device test executable not found: {}".format(executable))
    results = args.build_dir.resolve() / "results/stress" / datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    results.mkdir(parents=True, exist_ok=True)
    for index in range(1, args.runs + 1):
        log = results / "run_{:04d}.log".format(index)
        try:
            with log.open("w", encoding="utf-8") as stream:
                subprocess.run([str(executable), str(args.device), "--stress-case"],
                               stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=args.timeout)
        except (OSError, subprocess.SubprocessError) as error:
            parser.exit(1, "stress failed: {}; log={}\n".format(error, log))
    print("STRESS_RESULT={}/{}".format(args.runs, args.runs))


if __name__ == "__main__":
    main()
