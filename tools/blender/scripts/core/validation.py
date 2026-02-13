from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable

import bpy

from .mesh import count_tris


@dataclass
class ValidationReport:
    asset_name: str
    lod0_tris: int
    lod1_tris: int
    lod2_tris: int
    collider_tris: int


def build_report(asset_name: str, objects: Iterable[bpy.types.Object]) -> ValidationReport:
    by_name = {obj.name: obj for obj in objects}
    return ValidationReport(
        asset_name=asset_name,
        lod0_tris=count_tris(by_name.get(f"{asset_name}_LOD0", next(iter(by_name.values())))),
        lod1_tris=count_tris(by_name.get(f"{asset_name}_LOD1", next(iter(by_name.values())))),
        lod2_tris=count_tris(by_name.get(f"{asset_name}_LOD2", next(iter(by_name.values())))),
        collider_tris=count_tris(by_name.get(f"{asset_name}_COLLIDER", next(iter(by_name.values())))),
    )


def print_report(report: ValidationReport) -> None:
    print("=" * 56)
    print(f"ASSET REPORT: {report.asset_name}")
    print(f"  LOD0: {report.lod0_tris:,} tris")
    print(f"  LOD1: {report.lod1_tris:,} tris")
    print(f"  LOD2: {report.lod2_tris:,} tris")
    print(f"  COL:  {report.collider_tris:,} tris")
    print("=" * 56)
