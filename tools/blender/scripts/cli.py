from __future__ import annotations

import os
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from core.args import blender_argv, build_parser
from core.config import load_asset_config
from core.pipeline import run_asset_pipeline
from core.scene import reset_scene, setup_preview_scene
from generators.registry import get_generator, list_generators


def _should_regenerate(asset_name: str, output_root: str, source_config: str, force: bool) -> bool:
    if force:
        return True
    blend_path = Path(output_root) / f"{asset_name}.blend"
    if not blend_path.exists():
        return True
    return os.path.getmtime(source_config) > os.path.getmtime(blend_path)


def main() -> int:
    parser = build_parser()
    args = parser.parse_args(blender_argv())

    if args.command == "list":
        print("Generators:")
        for name in list_generators():
            print(f" - {name}")
        return 0

    config = load_asset_config(args.config)
    generated = 0
    skipped = 0

    for entry in config.assets:
        if args.asset and entry.name != args.asset:
            continue
        if not _should_regenerate(entry.name, config.defaults.output_root, config.source_path, args.force):
            print(f"SKIP {entry.name}: up-to-date")
            skipped += 1
            continue

        print(f"GENERATE {entry.name} [{entry.generator}]")
        reset_scene()
        setup_preview_scene()
        generator = get_generator(entry.generator)
        result = run_asset_pipeline(generator, entry, config.defaults)
        print(f"DONE {result['asset']} -> {result['glb']} ({result['device']})")
        generated += 1

    print(f"SUMMARY generated={generated} skipped={skipped}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
