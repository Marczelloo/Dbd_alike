# PLAN OPTYMALIZACJI - Dbd_alike

> Data analizy: 2026-02-13
> Cel: Przygotowanie na 1000+ assetów z maksymalną wydajnością

---

## DIAGNOZA OBECNYCH PROBLEMÓW

### Blender Pipeline

| Problem | Wpływ | Aktualny stan |
|---------|-------|---------------|
| Duplikacja kodu | Wysoki | 3 osobne skrypty z powtórzoną logiką |
| Brak modułowości | Krytyczny | Każdy asset = osobny 400-600 linii |
| Brak równoległości | Krytyczny | Sekwencyjne generowanie |
| Brak cache'owania | Wysoki | Pełny rebuild przy każdej zmianie |
| Hardcoded wartości | Średni | Seed, rozdzielczość w kodzie |
| Brak GPU fallback | Średni | Tylko CPU baking |
| subdivisions=8 | Krytyczny | ~245k faces na start - za dużo |

### Runtime C++

| Kategoria | Problem | Lokalizacja | Wpływ |
|-----------|---------|-------------|-------|
| **RENDER** | Vignette 6912 draw calls | `ScreenEffects.cpp:100-138` | **75% FPS** |
| **RENDER** | StaticBatcher vector alloc | `StaticBatcher.cpp:148` | Średni |
| **RENDER** | FxSystem billboard alloc | `FxSystem.cpp:873-874` | Średni |
| **PHYSICS** | Brak spatial partitioning | `PhysicsWorld.cpp:401-459` | **Wysoki** |
| **PHYSICS** | 8x collision iterations | `PhysicsWorld.cpp:408` | Średni |
| **FX** | std::sort co klatkę | `FxSystem.cpp:682` | Średni |
| **FX** | Object allocation | `FxSystem.cpp:534-535` | Średni |
| **UI** | CollectTypedCharacters alloc | `UiSystem.cpp:401-447` | Niski |
| **GAME** | BuildHudState co klatkę | `GameplaySystems.cpp:853` | Średni |
| **THREAD** | Wszystko na 1 wątku | Cały kod | **Wysoki** |

---

## KRYTYCZNE PROBLEMY - SZCZEGÓŁY

### 1. ScreenEffects::DrawVignette - KRYTYCZNY (75% spadek FPS)

**Lokalizacja:** `game/ui/ScreenEffects.cpp:73-140`

```
PROBLEM: 96x72 = 6912 rectangle'ów renderowanych KAŻDĄ klatkę!
- std::sqrt() dla każdego z 6912 celli
- std::pow() dla każdego cella  
- m_ui->DrawRect() x 6912 wywołań
```

**Rozwiązanie:** Shader-based vignette (fullscreen quad + fragment shader)

```glsl
// Fragment shader - 1 draw call zamiast 6912
uniform float uIntensity;
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    vec2 uv = gl_FragCoord.xy / uScreenSize;
    vec2 center = vec2(0.5);
    float dist = length(uv - center);
    float vignette = smoothstep(0.3, 0.9, dist) * uIntensity;
    FragColor = vec4(uColor, vignette);
}
```

### 2. FxSystem - std::sort co klatkę

**Lokalizacja:** `engine/fx/FxSystem.cpp:682`

```cpp
std::sort(emitterRuntime.trailPoints.begin(), emitterRuntime.trailPoints.end(), ...);
```

**Problem:** Sortowanie co klatkę dla każdego trail emittera - O(n log n)

**Rozwiązanie:** Insert-sort przy dodawaniu punktu - O(n)

### 3. PhysicsWorld - Brak Spatial Partitioning

**Lokalizacja:** `engine/physics/PhysicsWorld.cpp:401-459`

```cpp
for (int iteration = 0; iteration < 8; ++iteration) {
    for (const SolidBox& box : m_solids) {  // O(n) x 8 iteracji!
```

**Problem:** Przy 1000+ obiektów = 8000+ sprawdzeń co klatkę

**Rozwiązanie:** Octree lub Grid-based spatial hashing

```cpp
class SpatialGrid {
    std::unordered_map<GridCell, std::vector<SolidBox*>> m_cells;
    float m_cellSize = 8.0f;
    
    std::vector<SolidBox*> QueryNearby(const glm::vec3& position);
    // Z O(n) do O(1) dla local queries
};
```

### 4. StaticBatcher - Realokacja co klatkę

**Lokalizacja:** `engine/render/StaticBatcher.cpp:148-151`

```cpp
std::vector<GLint> firsts;   // TWORZONY CO KLATKĘ
std::vector<GLsizei> counts; // TWORZONY CO KLATKĘ
firsts.reserve(m_chunks.size());
```

**Rozwiązanie:** Cache firsts/counts jako member variables

### 5. BuildHudState - Zbudowany co klatkę

**Lokalizacja:** `game/gameplay/GameplaySystems.cpp:853-1124`

**Problem:** 270+ linii kodu wykonuje się co klatkę:
- String operations
- Hash map lookups
- Vector allocations

**Rozwiązanie:** Dirty flag + partial updates

```cpp
class HudCache {
    bool dirty = true;
    HudState cached;
    
    void MarkDirty() { dirty = true; }
    
    const HudState& Get() {
        if (dirty) {
            cached = BuildHudState();
            dirty = false;
        }
        return cached;
    }
};
```

---

## FAZA 1: REFAKTORYZACJA ARCHITEKTURY BLENDER (Priorytet: Krytyczny)

### 1.1 Utworzenie modułu bazowego `asset_core.py`

```
tools/blender/scripts/
├── core/
│   ├── __init__.py
│   ├── args.py          # Parser argumentów CLI
│   ├── scene.py         # Setup kamery, świateł, render settings
│   ├── mesh.py          # Operacje na mesh (decimate, LOD, collider)
│   ├── material.py      # Proceduralne materiały
│   ├── bake.py          # Baking z GPU/CPU auto-detect
│   ├── uv.py            # UV unwrapping
│   ├── export.py        # GLB export
│   └── validation.py    # Raport walidacji
├── generators/          # Definicje assetów
│   ├── __init__.py
│   ├── base.py          # Klasa bazowa AssetGenerator
│   ├── rock.py          # Rock generator
│   ├── crate.py         # Crate generator
│   ├── pillar.py        # Pillar generator
│   └── registry.py      # Rejestr generatorów
└── cli.py               # Główny entry point
```

### 1.2 Klasa bazowa `AssetGenerator`

```python
class AssetGenerator(ABC):
    name: str
    category: str
    target_tris: tuple = (6000, 3000, 1000)  # LOD0, LOD1, LOD2
    
    @abstractmethod
    def create_high_mesh(self) -> bpy.types.Object: ...
    
    @abstractmethod
    def create_material(self, obj: bpy.types.Object) -> bpy.types.Material: ...
    
    def generate(self, config: AssetConfig) -> AssetResult:
        # Automatyczny pipeline: HIGH -> BAKE -> LOD -> EXPORT
```

### 1.3 System konfiguracji JSON

```json
// config/assets.json
{
  "defaults": {
    "texture_size": 1024,
    "bake_samples": 16,
    "use_gpu": true
  },
  "assets": {
    "rock_moss_large": {
      "generator": "rock",
      "variant": "moss_large",
      "seed": 42,
      "scale": [1.4, 1.3, 1.2]
    }
  }
}
```

---

## FAZA 2: OPTYMALIZACJA GENEROWANIA MESH (Priorytet: Krytyczny)

### 2.1 Ograniczenie gęstości meshu HIGH

| Obecnie | Docelowo |
|---------|----------|
| `subdivisions=8` (~245k faces) | `subdivisions=5` (~20k faces) |
| Pełny displacement na wysokim meshu | Displacement na niskim, potem subdivide |

### 2.2 Pipeline generowania mesh

```
1. Base mesh (ico_sphere subdivisions=3) → ~640 verts
2. Apply displacement modifiers (large/medium/fine)
3. Apply modifiers → ~5-10k verts (wystarczająco do bake)
4. NO subdivision po displacement!
```

### 2.3 Automatyczne clamping polygonów

```python
def ensure_poly_budget(obj, target_tris, max_high_tris=50000):
    current = count_tris(obj)
    if current > max_high_tris:
        apply_decimate(obj, target=max_high_tris)
    return count_tris(obj)
```

---

## FAZA 3: OPTYMALIZACJA BAKINGU (Priorytet: Wysoki)

### 3.1 GPU auto-detection z fallback

#### WAŻNE
Zbudować system pod wszystkie możliwe karty grafincze, na tym sprzęcie możliwe jest tylko testowanie NVIDIA.

```python
def setup_bake_engine():
    if bpy.context.preferences.addons.get('cycles'):
        prefs = bpy.context.preferences.addons['cycles'].preferences
        for device in prefs.get_devices():
            if device.type == 'CUDA':  # NVIDIA
                bpy.context.scene.cycles.device = 'GPU'
                return 'GPU_CUDA'
    bpy.context.scene.cycles.device = 'CPU'
    return 'CPU'
```

### 3.2 Zoptymalizowane ustawienia bakingu

| Parametr | Obecnie | Docelowo |
|----------|---------|----------|
| Samples | 8 | 8-16 (adaptacyjnie) |
| Denoising | True | True (OptiX na NVIDIA) |
| Margin | 8 | 4 (mniejsze przecieki) |
| Tile size | default | 256x256 (lepsze dla GPU) |

---

## FAZA 4: BATCH PROCESSING & RÓWNOLEGŁOŚĆ (Priorytet: Krytyczny)

### 4.1 Master orchestration script

```powershell
# generate_batch.ps1
.\tools\blender\run_blender.ps1 -Script .\tools\blender\scripts\cli.py `
    -Args @("generate", "--config", "config/assets.json", "--parallel", "4")
```

### 4.2 Job queue z zależnościami

```
Queue:
├── [rock_001] → pending
├── [rock_002] → pending  
├── [crate_001] → running (Worker 1)
├── [pillar_001] → running (Worker 2)
└── ...
```

### 4.3 Inkrementalne generowanie

```python
def should_regenerate(asset_name, config):
    blend_path = f"out/assets/{asset_name}.blend"
    if not os.path.exists(blend_path):
        return True
    config_mtime = os.path.getmtime(config.path)
    blend_mtime = os.path.getmtime(blend_path)
    return config_mtime > blend_mtime
```

---

## FAZA 5: SYSTEM CACHE'OWANIA (Priorytet: Wysoki)

### 5.1 Cache struktura

```
.cache/
├── meshes/
│   └── rock_moss_large_high.pkl    # Serializowane parametry mesh
├── textures/
│   └── rock_moss_large/            # Baked textures
└── manifest.json                   # Cache validity tracking
```

### 5.2 Cache invalidation

```python
@cache_result(key=lambda config: hash(config))
def generate_high_mesh(config):
    ...
```

---

## FAZA 6: WALIDACJA I MONITORING (Priorytet: Średni)

### 6.1 Automatyczne raportowanie

```
Asset: rock_moss_large_001
├── Generation: 12.3s
│   ├── Mesh creation: 2.1s
│   ├── Baking: 8.4s
│   └── Export: 1.8s
├── Polycounts:
│   ├── HIGH: 18,432 tris
│   ├── LOD0: 6,842 tris ✓
│   ├── LOD1: 3,214 tris ✓
│   ├── LOD2: 1,102 tris ✓
│   └── COLLIDER: 248 tris ✓
├── Textures: 3 × 1024px = 12MB
└── Status: OK
```

### 6.2 Batch summary

```
Batch Report: 100 assets
├── Total time: 18m 42s
├── Avg per asset: 11.2s
├── Cache hits: 23
├── Failed: 2 (log: .cache/errors.log)
└── Output size: 1.2GB
```

---

## FAZA 7: JOB SYSTEM - WĄTKI (Priorytet: Wysoki)

### 7.1 Implementacja Job System

```cpp
class JobSystem {
    std::vector<std::thread> m_workers;
    ThreadPoolQueue<std::function<void()>> m_jobs;
    
    void Submit(std::function<void()> job);
    void WaitAll();
};
```

### 7.2 Równoległe subsystemy

```cpp
// Główna pętla - równoległe wykonanie
jobSystem.Submit([&]() { m_fxSystem.Update(dt, cameraPos); });
jobSystem.Submit([&]() { m_physics.Step(dt); });
jobSystem.Submit([&]() { m_animationSystem.Update(dt); });
jobSystem.WaitAll();
```

---

## FAZA 8: OBJECT POOLS & DIRTY FLAGS (Priorytet: Średni)

### 8.1 Particle Object Pool

```cpp
template<typename T, size_t PoolSize>
class ObjectPool {
    std::array<T, PoolSize> m_pool;
    std::vector<size_t> m_freeList;
    
    T* Allocate();
    void Free(T* ptr);
};

// Zamiast runtime.particles.reserve(256);
// Użyj: ObjectPool<Particle, 4096> g_particlePool;
```

### 8.2 HUD Dirty Flags

```cpp
class HudCache {
    bool dirty = true;
    HudState cached;
    
    void MarkDirty() { dirty = true; }
    
    const HudState& Get() {
        if (dirty) {
            cached = BuildHudState();
            dirty = false;
        }
        return cached;
    }
};
```

---

## FAZA 9: DODATKOWE OPTYMALIZACJE

### 9.1 Texture atlasing (opcjonalnie)

Dla małych assetów - łączenie tekstur do atlasów.

### 9.2 Format tekstur

| Format | Rozmiar | Jakość |
|--------|---------|--------|
| PNG | 100% | Lossless |
| WebP | ~60% | Near-lossless |
| KTX2 | ~40% | GPU-optimized |

---

## HARMONOGRAM WDROŻENIA

| Faza | Opis | Czas | Priorytet |
|------|------|------|-----------|
| **0** | **Vignette shader fix** | 0.5 dnia | **KRYTYCZNY** |
| 1 | Architektura modułowa Blender | 2-3 dni | Krytyczny |
| 2 | Optymalizacja mesh Blender | 1 dzień | Krytyczny |
| 3 | GPU baking + optymalizacja | 1 dzień | Wysoki |
| 4 | Batch processing Blender | 1-2 dni | Krytyczny |
| **5** | **Spatial partitioning Physics** | 1 dzień | **Wysoki** |
| **6** | **Job System (wątki)** | 2 dni | **Wysoki** |
| 7 | Object pools & dirty flags | 1 dzień | Średni |
| 8 | Cache'owanie Blender | 1 dzień | Wysoki |
| 9 | Walidacja & monitoring | 0.5 dnia | Średni |

**Całkowity czas:** 10-13 dni

---

## OCZEKIWANE REZULTATY

| Metryka | Obecnie | Po optymalizacji |
|---------|---------|------------------|
| Czas/asset Blender | ~45-60s | ~8-12s |
| 1000 assetów Blender | ~15h | ~3h |
| **FPS (z chase vignette)** | **~30** | **~120+** |
| Physics (1000 obiektów) | ~5-8ms | ~0.5-1ms |
| FxSystem update | ~1-2ms | ~0.3-0.5ms |
| Cache hit Blender | 0% | ~30-50% |
| Wykorzystanie CPU | ~15% (1 core) | ~60% (multi) |
| Wykorzystanie GPU baking | 0% | ~80% |

---

## KOLEJNOŚĆ WDRAŻANIA (REKOMENDOWANA)

1. **Faza 0: Vignette shader** - Najszybszy fix, ~75% FPS gain
2. **Faza 5: Spatial partitioning** - Kluczowe dla dużej ilości obiektów
3. **Faza 6: Job System** - Włącza wielowątkowość
4. **Faza 1-4: Blender pipeline** - Przygotowanie do 1000+ assetów
5. **Faza 7-9: Finishing touches** - Object pools, cache, walidacja

---

## PLIKI DO MODYFIKACJI

### Blender
- `tools/blender/scripts/make_rock_moss_large.py` - subdivisions fix
- `tools/blender/scripts/make_mossy_rock.py` - merge do core
- `tools/blender/scripts/make_asset.py` - merge do core
- `tools/blender/scripts/export_glb.py` - dodanie LOD export

### Runtime C++
- `game/ui/ScreenEffects.cpp` - shader-based vignette
- `engine/physics/PhysicsWorld.cpp` - spatial partitioning
- `engine/render/StaticBatcher.cpp` - member cache
- `engine/fx/FxSystem.cpp` - object pool, insert-sort
- `engine/ui/UiSystem.cpp` - static buffer dla typed chars
- `game/gameplay/GameplaySystems.cpp` - HUD dirty flags

---

## NOTATKI

- Blender: GPU baking działa na NVIDIA 3060ti, ale musi fallback do CPU
- Domyślna rozdzielczość tekstur: 1024 (z opcją 2048)
- Target: LOD0 6k-8k tris, LOD1 3k-4k tris, LOD2 1k-1.5k tris
- Collider: <500 tris
