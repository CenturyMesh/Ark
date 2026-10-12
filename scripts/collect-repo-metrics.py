#!/usr/bin/env python3
"""Save owner-visible GitHub aggregates locally; never upload or track app users."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess


def fetch(endpoint, paginate=False):
    command = ["gh", "api", endpoint]
    if paginate:
        command += ["--paginate", "--slurp"]
    try:
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", timeout=60)
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"available": False, "reason": type(error).__name__}
    if result.returncode:
        # Do not persist command diagnostics or authentication material.
        return {"available": False, "reason": "GitHub CLI request failed",
                "exit_code": result.returncode}
    try:
        data = json.loads(result.stdout)
        if paginate:
            data = [item for page in data for item in page]
        return {"available": True, "data": data}
    except (ValueError, TypeError):
        return {"available": False, "reason": "Unexpected JSON response"}


def total_text(metric):
    if not metric["available"]:
        return "unavailable (not zero)"
    data = metric["data"]
    return "{} events; {} unique within this 14-day window".format(
        data.get("count", "unknown"), data.get("uniques", "unknown"))


def collect(repository, output):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Repository must be owner/name")
    prefix = "repos/" + repository
    now = datetime.now(timezone.utc)
    record = {"schema": 1, "repository": repository,
              "captured_at_utc": now.isoformat(),
              "traffic_window_days": 14,
              "views": fetch(prefix + "/traffic/views?per=day"),
              "clones": fetch(prefix + "/traffic/clones?per=day"),
              "referrers": fetch(prefix + "/traffic/popular/referrers"),
              "popular_paths": fetch(prefix + "/traffic/popular/paths")}
    repo = fetch(prefix)
    record["repository_totals"] = {"available": repo["available"]}
    if repo["available"]:
        record["repository_totals"]["data"] = {
            key: repo["data"].get(key) for key in
            ("stargazers_count", "forks_count", "subscribers_count")}
    releases = fetch(prefix + "/releases?per_page=100", paginate=True)
    record["release_assets"] = {"available": releases["available"]}
    if releases["available"]:
        record["release_assets"]["data"] = [
            {"release_id": release["id"], "tag": release["tag_name"],
             "prerelease": release["prerelease"], "asset_id": asset["id"],
             "name": asset["name"], "download_count": asset["download_count"]}
            for release in releases["data"] if not release["draft"]
            for asset in release["assets"]]
    output.mkdir(parents=True, exist_ok=True)
    stem = now.strftime("%Y%m%dT%H%M%S%fZ")
    snapshot = output / (stem + ".json")
    with snapshot.open("x", encoding="utf-8") as stream:
        json.dump(record, stream, indent=2)
        stream.write("\n")
    lines = ["# ARK repository metrics", "", "Captured: " + now.isoformat(),
             "Repository: " + repository, "",
             "- Views: " + total_text(record["views"]),
             "- Clones: " + total_text(record["clones"]), "",
             "## Release asset download counters", ""]
    if record["release_assets"]["available"]:
        lines += ["| Tag | Asset | Counter |", "|---|---|---:|"]
        for asset in record["release_assets"]["data"]:
            # Asset metadata is external input; keep the Markdown table intact.
            clean = lambda text: str(text).replace("|", "\\|").replace("\n", " ")
            lines.append("| {} | {} | {} |".format(
                clean(asset["tag"]), clean(asset["name"]), asset["download_count"]))
    else:
        lines.append("Unavailable; no zero count is inferred.")
    lines += ["", "Traffic windows overlap. Do not add snapshot totals or daily",
              "unique counts to estimate distinct people. Download counters are",
              "not installations or active users; automation, repeats and release",
              "verification can contribute. No ARK application telemetry is collected.",
              "", "Raw aggregate snapshot: " + snapshot.name, ""]
    report = output / (stem + ".md")
    report.write_text("\n".join(lines), encoding="utf-8")
    print("Snapshot: " + str(snapshot))
    print("Report: " + str(report))
    print("Views: " + total_text(record["views"]))
    print("Clones: " + total_text(record["clones"]))
    unavailable = [key for key, value in record.items()
                   if isinstance(value, dict) and value.get("available") is False]
    if unavailable:
        print("Unavailable sections: " + ", ".join(unavailable))
    return 1 if unavailable else 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default="CenturyMesh/Ark")
    parser.add_argument("--out", type=Path,
                        default=Path(__file__).resolve().parents[1] / "out/repo-metrics")
    options = parser.parse_args()
    raise SystemExit(collect(options.repo, options.out))
