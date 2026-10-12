# Public launch materials

This work supports **A.03/F.03/F.05**. Runtime `016F / 0.9.17` remains unchanged.
Windows offers Studio; Linux offers the headless CLI/MCP/native worker.

The Windows CI environment fix was verified in the hosted
[PR #2 run](https://github.com/CenturyMesh/Ark/actions/runs/38310539114): all four
platform jobs passed before merge on 2026-10-12. This is separate from local
validation and from any real-user onboarding or manufacturing acceptance.

- [50-second guided demo](https://github.com/CenturyMesh/Ark/releases/download/v0.9.17/ark-guided-demo-016F.mp4):
  an edited native-capture sequence, with a synthetic parameter edit and its BOM.
  [Reproduction and evidence](demo.md) distinguish it from a live AI recording.

- [Announcement drafts](announcements.md): Portuguese/English LinkedIn copy and
  a Show HN submission/first-comment draft. The owner has not selected a posting
  account in this work, so these are prepared texts, not published social posts.
- [First-use walkthrough](../FIRST_TRY.md): a voluntary synthetic first-part task
  and a public, structured feedback form. Private invitations are paused at the
  owner's request; no contact list or outreach has been created.
- [Repository traffic](https://github.com/CenturyMesh/Ark/graphs/traffic): inspect
  views, clones and top referrers in GitHub's owner-visible interface.

## Save traffic and download snapshots

Install Python 3.8+ and GitHub CLI, then authenticate `gh` with access to the
repository's traffic endpoints. From the repository root run:

```sh
python scripts/collect-repo-metrics.py
```

The command reads GitHub aggregates and public release download counters. It
writes timestamped JSON and a readable Markdown report under ignored
`out/repo-metrics/`. It sends no messages, uploads no reports, schedules no jobs,
and adds no tracking code to ARK. Use `--repo owner/name` or `--out PATH` to select
another authorized repository or private destination. A missing/denied endpoint
is reported as unavailable and returns a nonzero exit code, never as zero usage.

GitHub traffic covers the last 14 days; take a snapshot regularly if you want
local history beyond that window. Windows overlap, so never sum their totals.
Distinct visitors within different windows cannot be added to obtain distinct
people. Release asset counters are cumulative for each asset ID and can include
repeat downloads and automated verification; they are not installed or active
user counts. Replacing an asset changes its identity and comparison basis.
See [GitHub traffic API](https://docs.github.com/en/rest/metrics/traffic).

## Before announcing broadly

Verify anonymous package downloads, the current branch's CI and the first-use
steps. Keep the prerelease and Linux-headless qualifications in the announcement.
Use the owner's chosen screenshot; do not replace it with a generated CAD image.
Publish only to explicitly selected accounts/channels and answer actual reports.
No first-time user success rate, adoption lift or benchmark improvement has been
measured by preparing these materials.
