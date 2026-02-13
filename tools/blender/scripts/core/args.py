from __future__ import annotations

import argparse
import sys


def blender_argv() -> list[str]:
    if "--" not in sys.argv:
        return []
    index = sys.argv.index("--")
    return sys.argv[index + 1 :]


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Modular Blender asset generation CLI")
    sub = parser.add_subparsers(dest="command", required=True)

    gen = sub.add_parser("generate", help="Generate assets from config")
    gen.add_argument("--config", required=True, help="Path to config/assets.json")
    gen.add_argument("--asset", default="", help="Generate only selected asset id")
    gen.add_argument("--force", action="store_true", help="Force regenerate even if outputs exist")

    sub.add_parser("list", help="List available generators")
    return parser
