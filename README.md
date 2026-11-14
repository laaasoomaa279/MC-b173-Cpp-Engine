# Beta 1.7.3 Engine (C++20 / OpenGL 3.3 Core)

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![OpenGL](https://img.shields.io/badge/OpenGL-3.3%20Core-green.svg)
![Platform](https://img.shields.io/badge/Platform-Windows%20(Win32)-lightgrey.svg)
![Beta 1.7.3 Parity](https://img.shields.io/badge/Beta%201.7.3%20Parity-70%25-orange.svg)
![Dependencies](https://img.shields.io/badge/External%20Frameworks-Zero-brightgreen.svg)

A lightweight voxel engine written in C++20, with no external dependencies, built from scratch using the native Win32 API and the OpenGL 3.3 Core standard. It aims for **1:1 parity** with the Java-based version of **Minecraft Beta 1.7.3**—in terms of appearance, behavior, and binary data.

> **Legal Note:** This repository contains **engine source code only**. No Mojang textures, sounds, or `.jar` files are included. See [LEGAL.md](LEGAL.md) for details.

---

## 🎯 1:1 Parity Progress Tracker (`70%` Overall)

| Subsystem | Parity | Implemented | Remaining for 1:1 |
| :--- | :---: | :--- | :--- |
| **Rendering & Meshing** | `92%` | 64-bit packed vertices, greedy meshing, smooth AO lighting, biome grass side overlays, wall-sheared torches, 3D clouds & weather | Minor entity hand-held item attachments (e.g., Skeleton bow) |
| **World Save (McRegion)** | `90%` | Binary `.mcr` region files (4KB sectors, Zlib), Gzip `level.dat` (v19132), NBT parser/writer, TileEntities | Entity list serialization & armor slot NBT persistence |
| **UI & HUD** | `88%` | Title screen with `mclogo.png` & pulsing splash text, F3 profiler graph, hearts/armor/bubbles, 2x2/3x3 crafting, furnace, chest, TMI | Sign text editing GUI (`GuiEditSign`) |
| **Blocks, Items & Recipes** | `85%` | All 97 Beta 1.7.3 block IDs, 1/16 sub-voxel hitboxes/models, full crafting & smelting tables | Complete item/block drop edge cases |
| **World Generation** | `65%` | 48-bit `java.util.Random`, 3-octave Perlin terrain, worm caves, Overworld & Nether (`DIM-1`), lakes, ores, trees | 2D Simplex climate noise, 32-bit int overflow Far Lands, Dungeons, Clay, Sugar Cane, Pumpkins |
| **Physics, Redstone & AI** | `55%` | AABB collision, edge-sneak, view bobbing, redstone wire/torches/repeaters, 12-block pistons, 8 mob types | Sub-box raycasting, fluid pathfinding & water/lava mixing, strong/weak redstone power, tool/armor durability wear, Ghast AI, Boats/Minecarts |
| **Multiplayer (Proto v14)** | `30%` | Big-Endian TCP packet framing, UTF-16BE `String16`, Zlib `0x33 Map Chunk` serializer | Non-blocking socket `recv` loop & server state synchronization |

---

## 🏗️ Architectural Highlights

- **Bare-Metal Platform Layer (`platform.hpp`):** Native Win32 windowing, manual OpenGL 3.3 Core function loading, Raw Mouse Input (`WM_INPUT`), lock-free SPSC / low-contention MPSC queues, priority worker thread pool, and a dedicated-thread 44.1 kHz 3D spatial audio mixer via `WinMM`.
- **64-Bit Vertex Compression (`chunk.vert`, `mesher_cubes.hpp`):** Every terrain vertex is packed into a single `uint64_t` (`uvec2`), encoding 1/16th-block fixed-point coordinates, face normal, 2-bit AO curve, sky/block light levels, atlas slot, and greedy quad dimensions.
- **Authentic Beta 1.7.3 Mechanics:** Non-linear brightness curve (`generateLightBrightnessTable`), two-point entity directional lighting, no hunger or sprint, instant food healing, machine-gun bow firing, and 1:8 Nether portal scaling.
- **Native C ABI Modding (`main.cpp`):** Dynamically loads native `.dll` plugins from `mods/` at startup, exposing block registration (`IDs 97..255`), world read/write, and chat hooks.

---

## 🛠️ Building & Running

### Prerequisites
- Windows 10/11
- C++20 compiler (`g++` / MinGW-w64 or MSVC)
- *(Optional)* A legitimate copy of `b1.7.3.jar` installed via the official launcher in `%APPDATA%\.minecraft\versions\b1.7.3\` or placed in the executable directory.

### Compile
```powershell
.\build.ps1
