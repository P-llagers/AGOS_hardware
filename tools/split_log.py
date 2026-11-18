#!/usr/bin/env python3
"""Split AGOS serial-monitor logs into one CSV file per row type.

Usage:
    python tools/split_log.py logs/device-monitor-261118-101500.log [more.log ...] [-o out_dir]

Input is the file written by PlatformIO's log2file filter (see platformio.ini).
Each line may start with the time filter prefix "HH:MM:SS.mmm > ". Firmware
rows are CSV whose first field is a row type (CFG, RX, TX, SUM, GPS, ...), and
the firmware prints "HDR,<type>,<columns>" lines that name the columns.

For every row type the script writes <out_dir>/<log name>_<TYPE>.csv with three
extra leading columns: log_file, pc_date (from the log file name) and pc_time
(from the time prefix). Values are copied as text exactly as logged; nothing is
converted, filled in, rounded or dropped except human-readable non-CSV lines.
Rows seen before their HDR line, or with a column count that does not match it,
are written to <log name>_UNMATCHED.csv so they can be inspected, not lost.
"""
import argparse
import csv
import os
import re
import sys

PREFIX = re.compile(r"^(\d{2}:\d{2}:\d{2}\.\d{3}) > (.*)$")
NAME_DATE = re.compile(r"device-monitor-(\d{2})(\d{2})(\d{2})-\d{6}")


def split_file(path, out_dir):
    base = os.path.splitext(os.path.basename(path))[0]
    match = NAME_DATE.search(base)
    pc_date = "20%s-%s-%s" % match.groups() if match else ""
    headers = {}   # row type -> list of column names (latest HDR wins)
    writers = {}   # (row type, columns) -> (file, csv.writer)
    counts = {}
    unmatched = []

    def writer_for(row_type, columns):
        key = (row_type, tuple(columns))
        if key not in writers:
            suffix = "" if not any(k[0] == row_type for k in writers) else "_%d" % (
                1 + sum(1 for k in writers if k[0] == row_type))
            name = os.path.join(out_dir, "%s_%s%s.csv" % (base, row_type, suffix))
            handle = open(name, "w", newline="", encoding="utf-8")
            w = csv.writer(handle)
            w.writerow(["log_file", "pc_date", "pc_time"] + list(columns))
            writers[key] = (handle, w)
        return writers[key][1]

    with open(path, encoding="utf-8", errors="replace") as handle:
        for line_number, raw in enumerate(handle, 1):
            raw = raw.rstrip("\r\n")
            m = PREFIX.match(raw)
            pc_time, text = (m.group(1), m.group(2)) if m else ("", raw)
            if not re.match(r"^[A-Z][A-Z0-9]*,", text):
                continue  # human-readable line
            fields = next(csv.reader([text]))
            row_type = fields[0]
            if row_type == "HDR":
                if len(fields) >= 3:
                    headers[fields[1]] = fields[2:]
                continue
            columns = headers.get(row_type)
            if columns is None or len(fields) - 1 != len(columns):
                unmatched.append([os.path.basename(path), pc_date, pc_time, line_number, text])
                continue
            writer_for(row_type, columns).writerow(
                [os.path.basename(path), pc_date, pc_time] + fields[1:])
            counts[row_type] = counts.get(row_type, 0) + 1

    for handle, _ in writers.values():
        handle.close()
    if unmatched:
        name = os.path.join(out_dir, "%s_UNMATCHED.csv" % base)
        with open(name, "w", newline="", encoding="utf-8") as handle:
            w = csv.writer(handle)
            w.writerow(["log_file", "pc_date", "pc_time", "line", "text"])
            w.writerows(unmatched)
    summary = ", ".join("%s=%d" % kv for kv in sorted(counts.items())) or "no rows"
    print("%s: %s%s" % (path, summary, ", UNMATCHED=%d" % len(unmatched) if unmatched else ""))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("logs", nargs="+", help="log files from logs/")
    parser.add_argument("-o", "--out", default="logs/csv", help="output folder (default logs/csv)")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for path in args.logs:
        if not os.path.isfile(path):
            sys.exit("Not a file: %s" % path)
        split_file(path, args.out)


if __name__ == "__main__":
    main()
