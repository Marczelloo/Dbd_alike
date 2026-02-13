from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Any


@dataclass
class AssetDefaults:
    texture_size: int = 1024
    bake_samples: int = 8
    use_gpu: bool = True
    output_root: str = "out/assets"


@dataclass
class AssetEntry:
    name: str
    generator: str
    variant: str = "default"
    seed: int = 42
    scale: tuple[float, float, float] = (1.0, 1.0, 1.0)
    texture_size: int | None = None
    bake_samples: int | None = None
    use_gpu: bool | None = None


@dataclass
class AssetConfig:
    defaults: AssetDefaults = field(default_factory=AssetDefaults)
    assets: list[AssetEntry] = field(default_factory=list)
    source_path: str = ""


def _to_scale(value: Any) -> tuple[float, float, float]:
    if isinstance(value, list) and len(value) == 3:
        return float(value[0]), float(value[1]), float(value[2])
    return 1.0, 1.0, 1.0


def load_asset_config(config_path: str) -> AssetConfig:
    path = Path(config_path)
    data = json.loads(path.read_text(encoding="utf-8"))

    raw_defaults = data.get("defaults", {})
    defaults = AssetDefaults(
        texture_size=int(raw_defaults.get("texture_size", 1024)),
        bake_samples=int(raw_defaults.get("bake_samples", 8)),
        use_gpu=bool(raw_defaults.get("use_gpu", True)),
        output_root=str(raw_defaults.get("output_root", "out/assets")),
    )

    entries: list[AssetEntry] = []
    raw_assets = data.get("assets", {})
    for name, raw in raw_assets.items():
        entries.append(
            AssetEntry(
                name=name,
                generator=str(raw.get("generator", "crate")),
                variant=str(raw.get("variant", "default")),
                seed=int(raw.get("seed", 42)),
                scale=_to_scale(raw.get("scale", [1.0, 1.0, 1.0])),
                texture_size=int(raw["texture_size"]) if "texture_size" in raw else None,
                bake_samples=int(raw["bake_samples"]) if "bake_samples" in raw else None,
                use_gpu=bool(raw["use_gpu"]) if "use_gpu" in raw else None,
            )
        )

    return AssetConfig(defaults=defaults, assets=entries, source_path=str(path.resolve()))
