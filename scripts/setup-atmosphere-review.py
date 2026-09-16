#!/usr/bin/env python3
"""Seed an isolated atmosphere review profile without replacing local edits."""

import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil

from atmosphere_profiles import prepare_profiles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, help="Final campaign-specific home directory")
    parser.add_argument("campaign", choices=("ja", "jo"))
    parser.add_argument("--package", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    source = args.package / "OpenJK"
    home = args.profile / "OpenJK"
    (home / "maps").mkdir(parents=True, exist_ok=True)
    catalogue = json.loads((source / "atmosphere-review/catalogue.json").read_text())
    maps = [m for m in catalogue["maps"] if m["campaign"] == args.campaign and m["kind"] == "sp"]
    prepare_profiles(args.package, args.profile, args.campaign)
    notes = args.profile / "atmosphere-review-notes.csv"
    if not notes.exists():
        with notes.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(("campaign", "map", "palette", "review_status", "notes"))
            for entry in maps:
                writer.writerow((args.campaign, entry["map"], entry["palette"] or "stock",
                                 "approved" if entry["approved"] else "pending", ""))
    config = home / "autoexec_sp.cfg"
    if not config.exists():
        config.write_text("// Atmosphere review uses the packaged review playlist.\n")
    log = home / "qconsole.log"
    if log.exists():
        logs = home / "review-logs"
        logs.mkdir(exist_ok=True)
        shutil.copyfile(log, logs / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ.log"))
    print(f"Atmosphere profiles: {home / 'maps'}")
    print(f"Review notes: {notes}")
    print("PgDn/PgUp: next/previous map. F5: toggle. F6: reload profile. F7/F8: stock/profile captures.")
    print("F9: flag a map in the console log. F10: status. Space/Ctrl: fly up/down.")


if __name__ == "__main__":
    main()
