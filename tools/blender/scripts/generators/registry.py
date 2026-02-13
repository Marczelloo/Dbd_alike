from __future__ import annotations

from .crate import CrateGenerator
from .pillar import PillarGenerator
from .rock import RockGenerator


def _all_generators():
    return {
        "crate": CrateGenerator,
        "pillar": PillarGenerator,
        "rock": RockGenerator,
    }


def get_generator(name: str):
    gen_cls = _all_generators().get(name)
    if gen_cls is None:
        raise KeyError(f"Unknown generator: {name}")
    return gen_cls()


def list_generators() -> list[str]:
    return sorted(_all_generators().keys())
