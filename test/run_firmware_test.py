#!/usr/bin/env python3
"""Run one firmware program on athrill3 and turn its output into a test result.

The test passes only when the expected line appears in athrill3's output
and no "FAIL " line appears before it. Interactive runs (-i) receive the
given debugger commands on stdin (for example "serial 0 Z" and "cont");
athrill3 is stopped as soon as the expected line appears or the wall-clock
timeout expires.
"""
import argparse
import selectors
import subprocess
import sys
import time


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--athrill", required=True)
    parser.add_argument("--memory", required=True)
    parser.add_argument("--device-config")
    parser.add_argument("--elf", required=True)
    parser.add_argument("--expect", default="SELFTEST PASS",
                        help="line that marks success")
    parser.add_argument("--clocks", type=int,
                        help="athrill3 -t limit for background runs")
    parser.add_argument("--command", action="append", default=[],
                        help="debugger command for an interactive run (repeatable)")
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="wall-clock limit in seconds")
    return parser.parse_args()


def build_command(args):
    command = [args.athrill, "-c1", "-m", args.memory]
    if args.device_config:
        command += ["-d", args.device_config]
    if args.command:
        command.append("-i")
    elif args.clocks:
        command.append(f"-t{args.clocks}")
    command.append(args.elf)
    return command


def run(args):
    process = subprocess.Popen(
        build_command(args),
        stdin=subprocess.PIPE if args.command else subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if args.command:
        process.stdin.write(("\n".join(args.command) + "\n").encode())
        process.stdin.flush()
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    output = b""
    deadline = time.monotonic() + args.timeout
    expected = args.expect.encode()
    while time.monotonic() < deadline:
        if not selector.select(timeout=0.2):
            if process.poll() is not None:
                break
            continue
        chunk = process.stdout.read1(65536)
        if not chunk:
            break
        output += chunk
        if expected in output:
            break
    if process.poll() is None:
        process.kill()
    process.wait()
    return output.decode(errors="replace")


def main():
    args = parse_args()
    output = run(args)
    sys.stdout.write(output)
    before_marker = output.split(args.expect, 1)[0]
    if args.expect not in output:
        print(f"\nTEST RESULT: FAIL (missing '{args.expect}')")
        return 1
    failed = [line for line in before_marker.splitlines() if line.startswith("FAIL ")]
    if failed:
        print(f"\nTEST RESULT: FAIL ({len(failed)} failed check(s))")
        return 1
    print("\nTEST RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
