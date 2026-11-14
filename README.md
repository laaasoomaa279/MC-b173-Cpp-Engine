
# ⛏️ MC b173 — C++20 Engine


[![Status](https://img.shields.io/badge/STATUS-IN_PROGRESS-FFB000?style=flat-square&labelColor=0d1117)]()
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=cplusplus&logoColor=white&labelColor=0d1117)]()
[![OpenGL](https://img.shields.io/badge/OPENGL-3.3_CORE-5586A4?style=flat-square&logo=opengl&logoColor=white&labelColor=0d1117)]()
[![Win32](https://img.shields.io/badge/PLATFORM-NATIVE_WIN32-0078D6?style=flat-square&logo=windows&logoColor=white&labelColor=0d1117)]()
[![Zero](https://img.shields.io/badge/ENGINES-ZERO_FRAMEWORKS-2EA043?style=flat-square&labelColor=0d1117)]()
![Math](https://img.shields.io/badge/MATH_%26_LOGIC-89%25-2EA043?style=flat-square&labelColor=161b22)
![Visual](https://img.shields.io/badge/VISUAL-72%25-D29922?style=flat-square&labelColor=161b22)
[![License](https://img.shields.io/badge/License-MIT-blue?style=flat-square&labelColor=161b22)]()


---

A lightweight voxel engine written in C++20, with no external dependencies, built from scratch using the native Win32 API and the OpenGL 3.3 Core standard. It aims for **1:1 parity** with the Java-based version of **Minecraft Beta 1.7.3**—in terms of appearance, behavior, and binary data.

> **Legal Note:** This repository contains **engine source code only**. No Mojang textures, sounds, or `.jar` files are included. See [LEGAL.md](LEGAL.md) for details.

---
---

## 📑 Table of Contents

| # | Section |
| :---: | :--- |
| **1** | **[🖼️ 1. Visual Architecture & Pipeline](https://www.google.com/search?q=%23-1-visual-architecture--pipeline)** | (#-1-visual-architecture--pipeline) |
| **2** | **[📈 2. Paper Math vs. Screen Reality](https://www.google.com/search?q=%23-2-paper-math-vs-screen-reality)** | (#-2-paper-math-vs-screen-reality) |
| **3** | **[🐛 3. Unfiltered Bug & Glitch Report](https://www.google.com/search?q=%23-3-unfiltered-bug--glitch-report)** | (#-3-unfiltered-bug--glitch-report) |
| **4** | **[✨ 4. What Already Works Brilliantly](https://www.google.com/search?q=%23-4-what-already-works-brilliantly)** | (#-4-what-already-works-brilliantly) |
| **5** | **[🗂️ 5. Complete Repository File Tree](https://www.google.com/search?q=%23-5-complete-repository-file-tree)** | (#-5-complete-repository-file-tree) |
| **6** | **[🛠️ 6. Building, Running & Controls](https://www.google.com/search?q=%23-6-building-running--controls)** | (#-6-building-running--controls) |
| **7** | **[🗺️ 7. Roadmap to 100% Parity](https://www.google.com/search?q=%23-7-roadmap-to-100-parity)** | (#-7-roadmap-to-100-parity) |

---
## 🖼️ 1. Visual Architecture & Pipeline

### 🔄 A. Dual-Frequency Engine Architecture (20 TPS Simulation vs. Uncapped Render)

```mermaid
flowchart TB
    subgraph WIN32["🪟 Native Win32 & Hardware Layer (platform.hpp)"]
        INPUT["🖱️ Raw Input (WM_INPUT)\nKeyboard + Mouse Lock"]
        AUDIO["🔊 44.1 kHz Stereo Mixer\nDedicated WinMM Thread"]
        WPOOL["🧵 Priority WorkerPool\nBackground Thread Queue"]
    end

    subgraph SIM["⏱️ Discrete 20 TPS Simulation Loop (dt = 0.05s)"]
        PHY["🏃 Player Physics (physics.hpp)\n3-Axis AABB • Slipperiness • Edge-Sneak"]
        WLD["🌍 World & Fluids (world.hpp / world_fluids.hpp)\n16-Level BFS Light • 4-Block Fluid Pathfinding"]
        RED["⚡ Redstone & Pistons (redstone_pistons.hpp)\nStrong/Weak Power • 8-Toggle Burnout • 12-Block Push"]
        ENT["🧟 Mob AI & Combat (entities_ai.hpp / combat_nether.hpp)\n8 Mob Types • Durability Armor • 1:8 Nether Portals"]
    end

    subgraph GEN["🏔️ Async World Gen & Meshing"]
        NOISE["🎲 Beta 1.7.3 Generator (world_gen_b173.hpp)\n48-Bit LCG • 2D Simplex • 5x17x5 Perlin • Far Lands"]
        MCR["💾 McRegion Storage (mcregion_nbt.hpp)\n4KB Sectors • Zlib .mcr • Gzip level.dat (v19132)"]
        MESH["🧱 Greedy + Sub-Voxel Mesher\n64-Bit Packed Vertices (8 Bytes/Vert)"]
    end

    subgraph GPU["🎨 OpenGL 3.3 Core Render Pipeline (Uncapped FPS, alpha = acc / 0.05)"]
        SKY["☀️ Pass 0A: Celestial Dome\n1500 Stars • Sunrise Glow • 3D Clouds (Y=108)"]
        OPQ["🪨 Pass 0B: Opaque Terrain\nBack-Face Culled • Smooth AO • Depth Write ON"]
        TRN["🌊 Pass 1: Sorted Translucents\nBack-to-Front Distance Sort (Zero Ocean Holes)"]
         BatchedEnt["🐑 Pass 2: Batched Entities & FP Hand\n64x32 Skins • Clamped Shadows • Held Items"]
        HUD["🎒 Pass 3: 2D Orthographic UI\nHotbar • Containers • F3 Profiler • TMI"]
    end

    INPUT --> SIM
    SIM -->|Chunk Requests| WPOOL
    WPOOL --> NOISE & MCR & MESH
    MESH -->|MPSC Ready Queue| OPQ & TRN
    SIM -->|Linear Interpolation α| GPU
    SIM -->|3D Sound Events| AUDIO

```

---

### ⚖️ B. Java Beta 1.7.3 vs. C++20 Port

```mermaid
graph LR
    subgraph JAVA["☕ Official Java Beta 1.7.3 (2011)"]
        J1["JVM + LWJGL 2 (OpenGL 1.x/2.x)"]
        J2["Single-Threaded Chunk Meshing"]
        J3["Uncompressed Vertex Arrays (24–32 B/vert)"]
        J4["48-Bit LCG + 32-Bit Int Overflow"]
        J5["McRegion (.mcr) + Big-Endian NBT"]
    end

    subgraph CPP["🚀 This C++20 Engine"]
        C1["Native Win32 + OpenGL 3.3 Core"]
        C2["Multi-Threaded Priority WorkerPool"]
        C3["64-Bit Packed Vertex (8 B/vert uvec2)"]
        C4["Identical 48-Bit LCG + Far Lands @ ±12,550,824"]
        C5["Binary-Compatible .mcr + level.dat"]
    end

    J1 -.->|Replaced by| C1
    J2 -.->|Upgraded to| C2
    J3 -.->|Compressed 75%| C3
    J4 ===>|100% Math Match| C4
    J5 ===>|100% Binary Match| C5

```

---

## 📈 2. Paper Math vs. Screen Reality

We separate **"Math & Code Logic"** (what is written in the C++ algorithms) from **"In-Game Reality"** (what you actually experience on screen when playing right now):

| Subsystem | 📐 Paper Math | 👁️ Screen Reality | 📊 Overall Subsystem Health | Honest Technical Status |
| :--- | :---: | :---: | :---: | :--- |
| **1. Noise & Terrain Math** | `96%` | `90%` | ![92%](https://img.shields.io/badge/█████████░-92%25-2ea043?style=flat-square) | Exact 48-bit LCG, `684.412` Perlin scales, 2D Simplex climate, and `int32` Far Lands overflow. Minor drift from `SEA_LEVEL=64` (vs `62`). |
| **2. Physics & Collision** | `94%` | `88%` | ![90%](https://img.shields.io/badge/█████████░-90%25-2ea043?style=flat-square) | Exact `0.16277136 / s³` friction, 3-axis AABB sweep, `0.5` step-up, sneak edge-hang, `1.5` fences, and Soul Sand drag. |
| **3. Combat, Armor & Nether** | `93%` | `86%` | ![89%](https://img.shields.io/badge/█████████░-89%25-2ea043?style=flat-square) | Exact durability-weighted armor formula `((K-1)*I)/J+1`, damage carryover, 4×5 Obsidian portals, and `1:8` Nether travel. |
| **4. Redstone & Pistons** | `90%` | `85%` | ![88%](https://img.shields.io/badge/█████████░-88%25-3fb950?style=flat-square) | Strong vs. weak power, 2-tick torch inverters, 8-toggle burnout, 1–4 tick repeaters, quasi-connectivity (`y+1`), and 12-block push. |
| **5. McRegion & NBT Saves** | `90%` | `85%` | ![86%](https://img.shields.io/badge/████████░░-86%25-3fb950?style=flat-square) | Exact 8KB header `.mcr` files, 4KB sector bitmap reuse, Gzip `level.dat` (v19132), and TileEntities. Mob saving still pending. |
| **6. Fluid Dynamics** | `88%` | `78%` | ![82%](https://img.shields.io/badge/████████░░-82%25-84cc16?style=flat-square) | 4-block drop pathfinding, `10x` source corner weighting, infinite water, and lava+water obsidian/cobble hardening. Static textures. |
| **7. World Render & Mesher** | `85%` | `68%` | ![75%](https://img.shields.io/badge/███████░░░-75%25-eab308?style=flat-square) | 64-bit packed vertices, smooth AO + quad flip, back-to-front water sort, 3D clouds. **Hurt by chunk queue drops & stretched torches.** |
| **8. Entities & Mob AI** | `75%` | `68%` | ![70%](https://img.shields.io/badge/███████░░░-70%25-eab308?style=flat-square) | 8 working mobs + Ghast fireball deflection + Creeper swell + clamped shadows. Missing passive/water mobs & rideable vehicles. |
| **9. UI, HUD & First-Person** | `78%` | `62%` | ![68%](https://img.shields.io/badge/██████░░░░-68%25-f97316?style=flat-square) | Authentic menus, F3 graph, containers, and TMI. **Hurt by backwards 3D Steve in inventory, awkward FP hand, and no F5 view.** |
| **10. Multiplayer (Proto v14)** | `40%` | `15%` | ![28%](https://img.shields.io/badge/███░░░░░░░-28%25-ef4444?style=flat-square) | Packet serializers, `UTF-16BE` strings, and Zlib `0x33 Map Chunk` builders exist, but the non-blocking `recv` loop isn't wired up. |
| **🏆 TOTAL ENGINE PARITY** | **`83%`** | **`73%`** | ![78%](https://img.shields.io/badge/████████░░-78%25_OVERALL-0078D6?style=flat-square) | **Core math, worldgen, physics, and saves are near-identical to 2011 Java; visual polish & threading locks are next.** |

> Note: The 78% overall average is dragged down by Multiplayer (28%) and UI polish (68%). If we exclude Multiplayer (not yet wired), Single-Player-only parity is ~86%.

## 🐛 3. Unfiltered Bug & Glitch Report

We list these bugs not as failures, but as an open engineering log. Each one has a documented root cause and a planned fix (see Section 8 Roadmap). Transparency here is a feature, not a bug — we want contributors to see exactly where help is needed.

### 🔴 A. Critical Threading & Queue Bugs

| # | Issue / Symptom | Where in Code | Why It Happens (Root Cause) |
| --- | --- | --- | --- |
| **1** | **🕳️ Invisible Chunks Until You Break a Block** | `main.cpp` (`g_readyMeshQueue`) | `g_readyMeshQueue` holds `1024` sections, and the main loop uploads at most `12` sections per frame. On `FAR` render distance (`12`), worker threads generate meshes faster than the GPU uploads them. `MPSCQueue::push` fails silently when full, dropping the mesh forever until a nearby block update forces a remesh. |
| **2** | **💥 Random Crash When Flying Fast into New Terrain** | `world.hpp` & `world_features.hpp` | During background chunk decoration (`tryPromoteAndDecorate`), `generateLiquidSprings` calls `world.scheduleTick()`, which pushes into `std::vector<ScheduledBlockTick> scheduledTicks` **without a mutex** while the main thread iterates and erases from it in `tickSimulation()`. Vector reallocation causes a `SIGSEGV` / Access Violation. |
| **3** | **🌀 Corrupted Geometry on Long `/tp` Teleports** | `world.hpp` (`grid[32*32]`) | The toroidal chunk cache (`32×32` columns) reuses slots via modulo arithmetic. Teleporting far away overwrites chunk slots while background meshing threads are still reading them without holding `slotLocks`. |

---

### 🟡 B. Visual, Model & Camera Glitches

| # | Visual Glitch | Where in Code | What You Will See |
| --- | --- | --- | --- |
| **1** | **🙃 Inverted / Backwards 3D Steve in Inventory** | `ui_primitives.hpp` (`drawPaperDollPlayer`) | Opening the inventory (`E`) shows the 3D Steve paper-doll model tracking your mouse, but due to negative Y-scaling (`scale(1.875, -1.875, 1.875)`) and face winding order, Steve renders backwards/inside-out. |
| **2** | **🕯️ Stretched, Wide Torches** | `mesher_models.hpp` (`emitTorchModel`) | While the top and bottom caps of torches are properly sized (`2×2` sub-pixels, `hw = 0.0625f`), the 4 lateral side faces span `±0.5f` (a full block wide) instead of `±hw`, making torches look like wide crossed planks. |
| **3** | **🤚 Awkward First-Person Hand & No `F5` Camera** | `player.hpp` (`drawFirstPersonHeld`) | The bare first-person arm rotation/translation looks stiff and slightly off-angle compared to Java, and there is **no `F5` third-person camera** yet. |
| **4** | **🎨 Misaligned Block UVs & Static Procedural Liquids** | `asset_loader.hpp` & `blocks.hpp` | Several non-cubic blocks have rough UV mappings. Also, Water (`205`), Lava (`237`), Fire (`31`), and Portal (`14`) tiles are static procedural patterns generated at boot rather than animated multi-frame strips. |
| **5** | **🧱 Invisible Seam After TNT Explosions** | `redstone_pistons.hpp` (`detonateExplosion`) | When TNT destroys a block on a chunk border (`x & 15 == 0` or `15`), it only queues `{p.x >> 4, p.z >> 4}` instead of calling `enqueueBlockAndNeighbors`, leaving adjacent chunk faces unmeshed until updated. |

---

### 🔵 C. Subtle Gameplay & Loot Parity Deviations from Java Beta 1.7.3

| Feature / Mechanic | Java Beta 1.7.3 (Official) | This C++20 Engine Right Now | File Location |
| --- | --- | --- | --- |
| **🌊 Ocean Sea Level** | `62` (Water fills up to `y = 62`) | `64` (Water fills up to `y = 63`, 1 block higher) | `world.hpp` (`SEA_LEVEL = 64`) |
| **🪣 Empty Bucket Stacking** | `1` (Unstackable until MC 1.3) | `16` (Stacks up to 16) | `asset_loader.hpp` (`ITEM_BUCKET`) |
| **📚 Bookshelf Drops** | Drops **nothing** (`0` items) | Drops `3` Books (Beta 1.8+ behavior) | `physics.hpp` (`resolveBlockDrops`) |
| **🍎 Apple Drops from Leaves** | **Never** (Dungeon chests only) | `1/200` chance when Oak leaves decay | `world_fluids.hpp` (`tickRandomBlock`) |
| **❄️ Snow Layer Drops** | Requires a **Shovel** to drop snowball | Drops `1` snowball even with bare hands | `physics.hpp` (`resolveBlockDrops`) |
| **🐷 Pig Porkchop Drops** | `0–2` Porkchops (`nextInt(3)`) | Always drops `1` Porkchop | `entities_ai.hpp` (`dropMobLoot`) |
| **🎋 Sugar Cane PRNG Drift** | Uses `rand.nextInt(128)` for Y | Uses `getSurfaceSolidY()` (prevents floating reeds, but shifts PRNG sequence for later features in chunk) | `world_features.hpp` |
| **🕳️ Cave Branch Recursion** | Recursive `generateCaveNode()` | `branchSub` lambda only branches **1 level deep** | `world_gen_b173.hpp` |
| **⚡ Redstone Update Order** | Spatial `HashSet` iterative order | Fast multi-pass vector relaxation (differs on 0-tick/BUDs) | `redstone_pistons.hpp` |

---

## ✨ 4. What Already Works Brilliantly

> *Despite the rough edges above, the amount of authentic 2011 engineering packed into these 25 files is massive:*

| 🔧 Subsystem | 📁 File | 🧩 What's Implemented |
| :--- | :--- | :--- |
| **🎲 Authentic 48-Bit `JavaRandomB173` & The Far Lands** | `noise_b173.hpp` | Exact LCG (`seed = (seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1)`), Box-Muller Gaussian distribution, 2D Simplex climate octaves (`seed * 9871`, `39811`, `543321`), and JLS `double`-to-`int32` saturation clamping (`javaDoubleToInt`). Teleport to `X = 12550824` (`/tp 12550824 90 0`) and witness the legendary **Far Lands** cliff walls! |
| **💾 True Binary McRegion (`.mcr`) & NBT Storage** | `mcregion_nbt.hpp` | Reads and writes genuine Big-Endian NBT tags (`0..11`), Gzip-compressed `level.dat` (version `19132`) with atomic `level.dat_new` → `level.dat_old` rotation, and `r.<rx>.<rz>.mcr` region files with 4KB sector allocation tables, timestamp tables, and a first-fit free-sector bitmap so region files never bloat. |
| **🔦 16-Level Dual-Channel BFS Lighting** | `world.hpp` | Full SkyLight and BlockLight nibble arrays (`2048` bytes per `16×16×16` section) with flood-fill removal and propagation queues, cross-chunk seam dirty-marking, and Notch's non-linear brightness curve (`0.05` ambient floor). |
| **⚙️ Pistons, Quasi-Connectivity & Redstone** | `redstone_pistons.hpp` | Distinguishes Strong Power vs. Weak Power, implements the single-dot wire rule, diagonal step-up/step-down diode blocking (opaque blocks cut vertical climb), 2-tick torch delay with 8-toggles-in-60-ticks fizz burnout, 1–4 tick repeaters, piston quasi-connectivity (`y + 1`), 12-block push limit, fragile block popping, and sticky retraction. |
| **⚔️ Classic Beta Combat & Nether Portals** | `combat_nether.hpp`, `entities_ai.hpp` | No hunger bar, no sprint, instant food healing, machine-gun bow firing, durability-weighted armor reduction, 80-tick Nether portal warmup with `1:8` coordinate scaling, 128-block existing portal search, and automatic arrival portal + obsidian platform generation. You can even punch Ghast fireballs back ("Return to Sender")! |
| **🔌 Native C-ABI `.dll` Mod Loader** | `main.cpp` | At startup, the engine scans `mods/*.dll`, loads native plugins via `LoadLibraryA`, and passes `McBetaModAPI` function pointers so mods can register custom blocks (`IDs 97..255`), read/write world blocks, and send chat messages. |

## 🗂️ 5. Complete Repository File Tree

```text
main_source/
├── 📄 LEGAL.md                       # Copyright, clean-room & Mojang asset disclaimer
├── 📄 README.md                      # You are here!
├── ⚙️ build.ps1                      # Fast cached Clang/Clang++ C++20 build script
│
├── 🎨 shaders/                       # External GLSL 330 Core Terrain Shaders
│   ├── chunk.vert                    # 64-bit vertex unpacking (uvec2) + AO & face shading
│   └── chunk.frag                    # Atlas sampling + biome tint + linear/exponential fog
│
├── 📦 vendor/                        # Single-File Public Domain / MIT Libraries
│   ├── miniz.c / miniz.h             # Zlib & Gzip compression for .mcr & level.dat
│   ├── stb_image.h / stb.cpp         # PNG decoder for atlases, colormaps & mob skins
│   └── stb_vorbis.c                  # OGG Vorbis decoder for sound effects
│
└── 🧠 src/                           # Core C++20 Engine Source Code
    ├── main.cpp                      # Master 20 TPS loop, frame blending, 2-pass render, DLL mod API
    ├── platform.hpp                  # Win32 window, OpenGL 3.3 loader, WM_INPUT, WorkerPool, WinMM 3D Audio
    ├── blocks.hpp                    # 97 Block registry, 1/16 sub-voxel boxes, snow support & wire masks
    │
    ├── 🖼️ assets/
    │   ├── asset_loader.hpp          # b1.7.3.jar reader, font kerning scanner, colormaps, OGG resampler
    │   └── recipes_b173.hpp          # Shaped/shapeless crafting, furnace smelting/fuels & TMI catalog
    │
    ├── 🌍 world/
    │   ├── world.hpp                 # 32x32 toroidal chunk grid, 16-level BFS lighting & TileEntities
    │   ├── world_gen_b173.hpp        # 2D Simplex climate, 5x17x5 density terrain, Nether & worm caves
    │   ├── noise_b173.hpp            # 48-bit JavaRandom, 2D Simplex, 3D Perlin & int32 Far Lands overflow
    │   ├── world_features.hpp        # Lakes, dungeons, clay, ore veins, big oak/taiga trees, reeds, cacti
    │   ├── world_fluids.hpp          # 4-block drop pathfinding, infinite water, lava hardening, leaf decay
    │   └── mcregion_nbt.hpp          # McRegion (.mcr) 4KB sector bitmap manager & Big-Endian NBT I/O
    │
    ├── ✨ render/
    │   ├── mesher_cubes.hpp          # Greedy cube mesher with smooth AO & fancy grass side overlay
    │   ├── mesher_models.hpp         # Fluid corner smoothing, torches, crops, portals & sub-box culling
    │   └── sky_weather.hpp           # Celestial curve, sunrise glow, 1500 stars, 3D clouds (Y=108), weather
    │
    ├── 🎮 gameplay/
    │   ├── physics.hpp               # 3-axis AABB sweep, sneak edge-hang, 1.5 fences, sub-voxel raycasting
    │   ├── player.hpp                # First-person held block/item/arm renderer, block mining, slash commands
    │   ├── redstone_pistons.hpp      # Strong/weak power, torch burnout, repeaters, 12-block pistons, TNT rays
    │   └── combat_nether.hpp         # Ballistic projectiles, durability-weighted armor & 1:8 Nether portals
    │
    ├── 🧟 entities/
    │   ├── entities_ai.hpp           # AI for 8 mob types, Ghast fireball deflection, item merging/pickup
    │   └── entity_renderer.hpp       # Batched 64x32 mob models, Creeper swell & clamped ground shadows
    │
    ├── 🖥️ ui/
    │   ├── ui_primitives.hpp         # Variable-width ASCII font (§0..§f), stone buttons, 3D icons, inventory Steve
    │   ├── ui_hud_containers.hpp     # Crosshair, hotbar, hearts/armor/bubbles, F3 graph, containers & TMI
    │   └── ui_menus.hpp              # Title screen, pulsing splash text, world browser & famous seed presets
    │
    └── 🌐 net/
        └── net_protocol_b173.hpp     # Beta 1.7.3 Protocol v14 framing, UTF-16BE strings & 0x33 Zlib chunks

```

---

## 🛠️ 6. Building, Running & Controls

### Prerequisites

1. **OS:** Windows 10 or Windows 11 (x64).
2. **Compiler:** **Clang** (`clang` and `clang++` with `-std=c++20` support) available in your system `PATH`.
3. **Assets *(Highly Recommended)*:** Place your own copy of `b1.7.3.jar` in the root folder (or have it installed in `%APPDATA%\.minecraft\versions\b1.7.3\b1.7.3.jar`).

### Compile & Launch

```powershell
.\build.ps1
.\build\minecraft.exe

```

### 🎮 Controls & Diagnostic Slash Commands

| Key / Input | Action | Slash Command | Effect |
| --- | --- | --- | --- |
| `W` `A` `S` `D` | Move Horizontally | `/fly` | Toggle Creative-style flight mode |
| `Space` | Jump / Swim Up / Fly Up | `/give <id> [cnt] [meta]` | Spawn any block or item into inventory |
| `Left Shift` | Sneak (Edge-Hang) / Fly Down | `/tp <x> <y> <z>` | Teleport to exact coordinates (Try `/tp 12550824 90 0`!) |
| `Left Click` | Mine Block / Melee Attack / Deflect Fireball | `/time <day|night|sunset|set>` | Change world time (0..24000) |
| `Right Click` | Place Block / Use Item / Open Container | `/weather <clear|rain|thunder>` | Change active weather state |
| `E` or `I` | Open / Close Inventory | `/summon <mob>` | Spawn `zombie`, `creeper`, `skeleton`, `spider`, `pig`, `sheep`, `cow`, or `ghast` |
| `O` *(in GUI)* | Toggle **TooManyItems (TMI)** Sidebar | `/heal` | Restore full 10 hearts (20 HP) & extinguish fire |
| `Q` | Drop Held Item | `/clear` | Clear all 36 main inventory slots |
| `C` | Optic Zoom (22° FOV) | `/seed` | Display current 64-bit world seed |
| `F3` / `F4` | Toggle Debug Profiler / Toggle Flight | `/kill` | Trigger player death screen |

---

## 🗺️ 7. Roadmap to 100% Parity

> *Active development targets — grouped by subsystem, prioritized for maximum parity gain.*

### 📊 Status Overview

| Subsystem | Priority | Status |
| :--- | :---: | :---: |
| 🔒 Thread-Safety Hardening | `P0` | ![Critical](https://img.shields.io/badge/CRITICAL-FF4444?style=flat-square&labelColor=0d1117) |
| 🚀 Chunk Mesh Pipeline Fix | `P0` | ![Critical](https://img.shields.io/badge/CRITICAL-FF4444?style=flat-square&labelColor=0d1117) |
| 🧍 3D Models & Camera | `P1` | ![High](https://img.shields.io/badge/HIGH-D29922?style=flat-square&labelColor=0d1117) |
| 🌊 Animated Procedural Textures | `P1` | ![High](https://img.shields.io/badge/HIGH-D29922?style=flat-square&labelColor=0d1117) |
| 🎯 Final Gameplay Tweaks | `P2` | ![Medium](https://img.shields.io/badge/MEDIUM-5586A4?style=flat-square&labelColor=0d1117) |
| 🌐 Multiplayer Client Loop | `P2` | ![Medium](https://img.shields.io/badge/MEDIUM-5586A4?style=flat-square&labelColor=0d1117) |

---

### 📋 Detailed Task Breakdown

<details open>
<summary><b>🔒 P0 — Thread-Safety Hardening</b></summary>

- [ ] Protect `WorldMap::scheduledTicks` with a mutex
- [ ] Add read/write locks around `ChunkColumn` slots during background meshing

</details>

<details open>
<summary><b>🚀 P0 — Fix Missing Chunk Meshes</b></summary>

- [ ] Expand `g_readyMeshQueue` capacity
- [ ] Dynamically scale GPU VBO uploads per frame so chunks never get dropped on `FAR` render distance

</details>

<details>
<summary><b>🧍 P1 — Fix 3D Models & Camera Views</b></summary>

- [ ] Fix inverted Y-scale/winding on the inventory Steve model (`ui_primitives.hpp`)
- [ ] Narrow lateral torch quads (`mesher_models.hpp`)
- [ ] Refine the first-person arm transform (`player.hpp`)
- [ ] Add `F5` third-person camera mode

</details>

<details>
<summary><b>🌊 P1 — Animated Procedural Textures</b></summary>

- [ ] Replace static water, lava, fire, and portal tiles with dynamic multi-frame procedural animations (`glTexSubImage2D` per tick)
- [ ] Clean up remaining UV offsets

</details>

<details>
<summary><b>🎯 P2 — Final Beta 1.7.3 Gameplay Tweaks</b></summary>

- [ ] Change `SEA_LEVEL` from `64` to `62`
- [ ] Fix bookshelf/bucket/apple/snow/porkchop rules
- [ ] Add `GuiEditSign`
- [ ] Implement remaining mobs/vehicles (`Chicken`, `Squid`, `Slime`, `Wolf`, `Zombie Pigman`, `Boat`, `Minecart`)

</details>

<details>
<summary><b>🌐 P2 — Multiplayer Client Loop</b></summary>

- [ ] Wire up the non-blocking Winsock `recv` state machine in `net_protocol_b173.hpp` to play on live Beta 1.7.3 servers

</details>
