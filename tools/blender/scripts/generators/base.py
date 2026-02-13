from __future__ import annotations

from abc import ABC, abstractmethod

import bpy


class AssetGenerator(ABC):
    name: str = "base"
    category: str = "generic"
    target_tris: tuple[int, int, int] = (6000, 3000, 1000)

    @abstractmethod
    def create_high_mesh(self, entry):
        raise NotImplementedError

    @abstractmethod
    def create_material(self, obj: bpy.types.Object, entry):
        raise NotImplementedError
