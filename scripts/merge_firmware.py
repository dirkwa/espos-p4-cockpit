#!/usr/bin/env python3
"""Merge an ESP-IDF build's flash images into one binary.

Reads the offset/file map ESP-IDF's build already worked out
(flasher_args.json in the build dir) and hands it to esptool's
merge_bin, so the offsets here can never drift from what the
partition table + bootloader actually need.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path,
                         help="e.g. build")
    parser.add_argument("--chip", required=True, help="e.g. esp32p4")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument(
        "--require-model", action="store_true",
        help="fail if the esp-sr model image can't be found (use when the "
             "build has CONFIG_MODEL_IN_FLASH=y and on-device wake enabled)")
    args = parser.parse_args()

    flasher_args_path = args.build_dir / "flasher_args.json"
    if not flasher_args_path.is_file():
        print(f"error: {flasher_args_path} not found — build the firmware first",
              file=sys.stderr)
        return 1

    flasher_args = json.loads(flasher_args_path.read_text())
    flash_settings = flasher_args["flash_settings"]
    flash_files = flasher_args["flash_files"]

    all_files = [p for p in args.build_dir.rglob("*") if p.is_file()]
    print(f"files under {args.build_dir}:")
    for p in sorted(all_files):
        print(" ", p.relative_to(args.build_dir))

    # flasher_args.json's paths mirror idf.py's build/bootloader/,
    # build/partition_table/ layout; resolve by role as well so a renamed
    # output still lands. Skipping otadata is harmless (all-0xFF = boot the
    # first OTA slot). Skipping the model is NOT harmless once on-device
    # WakeNet is back (phase 2): an empty "model" partition means a dead
    # wake word that OTA cannot repair, hence --require-model.
    by_basename = {p.name: p for p in all_files}

    mandatory_roles = {
        "bootloader": "bootloader.bin",
        "partition": "partitions.bin",
    }
    optional_roles = {
        "ota_data": "ota_data_initial.bin",
        "srmodels": "srmodels.bin",
        "model": "srmodels.bin",
    }

    def resolve(rel_path: str) -> Path | None:
        direct = args.build_dir / rel_path
        if direct.is_file():
            return direct
        found = by_basename.get(Path(rel_path).name)
        if found is not None:
            return found
        lower = rel_path.lower()
        for keyword, override_name in mandatory_roles.items():
            if keyword in lower:
                if override_name in by_basename:
                    return by_basename[override_name]
                print(f"error: could not locate '{rel_path}' (role: {keyword}) "
                      f"anywhere under {args.build_dir}", file=sys.stderr)
                sys.exit(1)
        for keyword, override_name in optional_roles.items():
            if keyword in lower:
                return by_basename.get(override_name)
        # Anything left unmatched (no bootloader/partition/ota_data/
        # srmodels keyword) is the main app image — mandatory.
        if "firmware.bin" in by_basename:
            return by_basename["firmware.bin"]
        print(f"error: could not locate '{rel_path}' (app image) "
              f"anywhere under {args.build_dir}", file=sys.stderr)
        sys.exit(1)

    cmd = [
        "esptool.py", "--chip", args.chip, "merge_bin",
        "-o", str(args.out),
        "--flash_mode", flash_settings["flash_mode"],
        "--flash_freq", flash_settings["flash_freq"],
        "--flash_size", flash_settings["flash_size"],
    ]
    for offset, rel_path in sorted(flash_files.items(), key=lambda kv: int(kv[0], 16)):
        resolved = resolve(rel_path)
        if resolved is None:
            is_model = "srmodels" in rel_path.lower() or "model" in rel_path.lower()
            if is_model and args.require_model:
                print(f"error: --require-model was given but the esp-sr model "
                      f"image for '{rel_path}' was found neither under "
                      f"{args.build_dir} nor in the installed framework "
                      f"packages. Merging without it would produce a binary "
                      f"whose on-device wake word is dead.", file=sys.stderr)
                return 1
            print(f"skipping {offset} ({rel_path}): not produced by this build")
            continue
        cmd += [offset, str(resolved)]

    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
