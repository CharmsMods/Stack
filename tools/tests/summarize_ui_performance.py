"""Summarize a bounded pair of native UI captures without external packages."""
import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)] if values else None


def load_capture(folder):
    frames = defaultdict(list)
    with (folder / "frames.csv").open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            frames[row.pop("scenario")].append({key: float(value) for key, value in row.items()})
    timings = {}
    for scenario, rows in frames.items():
        timings[scenario] = {"samples": len(rows)}
        for key in rows[0]:
            values = [row[key] for row in rows if row[key] >= 0]
            timings[scenario][key] = {
                "median": statistics.median(values) if values else None,
                "p95": percentile(values, .95), "max": max(values) if values else None,
                "samples": len(values),
            }
        for hz in (60, 120, 144):
            timings[scenario][f"over_{hz}hz"] = sum(row["frame_ms"] > 1000 / hz for row in rows)
    latency = defaultdict(list)
    misses = []
    with (folder / "interactions.csv").open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if row["status"] == "presented":
                latency[(row["scenario"], row["action"], row["dispatch"])].append(float(row["response_ms"]))
            else:
                misses.append(row)
    responses = {"|".join(key): {"samples": len(values), "median": statistics.median(values),
                 "p95": percentile(values, .95), "max": max(values)} for key, values in latency.items()}
    return {"frames": timings, "responses": responses, "unresolved": misses,
            "environment": (folder / "environment.txt").read_text(encoding="utf-8")}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    before, after = load_capture(args.before), load_capture(args.after)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "comparison.json").write_text(json.dumps({"before": before, "after": after}, indent=2), encoding="utf-8")
    rows = ["# Native UI timing comparison", "", "Times are milliseconds; CPU frame time excludes the diagnostic pacing sleep.",
            "GPU timing covers main-window drawing/compositing. Hidden window, swap interval zero, 60 Hz input cadence.", "",
            "| Scenario | CPU median before -> after | CPU p95 before -> after | UI p95 before -> after | GPU p95 before -> after |",
            "| --- | ---: | ---: | ---: | ---: |"]
    for scenario, previous in before["frames"].items():
        current = after["frames"][scenario]
        def pair(metric, field):
            a, b = previous[metric][field], current[metric][field]
            return f"{a:.3f} -> {b:.3f}" if a is not None and b is not None else "unavailable"
        rows.append(f"| {scenario} | {pair('frame_ms', 'median')} | {pair('frame_ms', 'p95')} | "
                    f"{pair('interface_ms', 'p95')} | {pair('gpu_draw_ms', 'p95')} |")
    rows += ["", "## Response timing", "", "Latency starts at ImGui event enqueue or the named UI-command dispatch and ends at the first changed frame's swap return.",
             "It excludes hardware input, OS queueing and monitor scanout. Image responses require an accepted authored revision at least as new as the dispatched edit.", "",
             "| Scenario/action/dispatch | Samples before/after | Median before -> after | Slowest before -> after |",
             "| --- | ---: | ---: | ---: |"]
    for key in sorted(before["responses"].keys() | after["responses"].keys()):
        a, b = before["responses"].get(key), after["responses"].get(key)
        label = key.replace("|", " / ")
        if a and b:
            rows.append(f"| {label} | {a['samples']}/{b['samples']} | {a['median']:.3f} -> {b['median']:.3f} | {a['max']:.3f} -> {b['max']:.3f} |")
        else:
            rows.append(f"| {label} | {a['samples'] if a else 0}/{b['samples'] if b else 0} | incomplete | incomplete |")
    rows += ["", f"Unresolved events: before={len(before['unresolved'])}, after={len(after['unresolved'])}.",
             "", "One bounded capture per version establishes observed differences for this fixture; it is not a cross-hardware or statistical performance guarantee."]
    (args.output / "comparison.md").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print("\n".join(rows))


if __name__ == "__main__":
    main()
