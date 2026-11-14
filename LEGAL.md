# Legal Notice & Copyright Disclaimer

## 1. Non-Affiliation & Trademark Notice
This project is an independent reimplementation of a game engine, written in C++20 and developed using a "clean-room" methodology for educational and digital preservation purposes. This project is **not affiliated with, endorsed by, sponsored by, or in any way connected to Mojang Studios or Microsoft Corporation**.
"Minecraft" is a registered trademark of Mojang Synergies AB. All references to "Minecraft Beta 1.7.3" are purely descriptive, intended to identify the file format, network protocol, and gameplay compatibility.

## 2. Zero Redistribution of Proprietary Assets (BYOA)
This repository **does not contain, host, or distribute** any copyrighted textures, audio files, fonts, or compiled code from Minecraft.
- At runtime, the engine locates the user-provided `b1.7.3.jar` file—obtained through legitimate means (either in the local working directory or the user's standard `.minecraft` installation directory)—in order to decode textures (`terrain.png`, `items.png`, `font/default.png`) and `.ogg` sound effects directly into memory.
- If the `b1.7.3.jar` file is not present on the host system, the engine automatically falls back on programmatically generated alternative textures, ensuring the software continues to function even in the absence of assets owned by third parties.
## 3. Interoperability & Format Compatibility
Support for the McRegion (`.mcr`) world save format, Big-Endian NBT serialization, and Beta 1.7.3 TCP Protocol (v14) is implemented from scratch solely for data interoperability with existing user-created worlds and servers.

## 4. Third-Party Libraries
This project bundles the following permissive / public-domain single-file libraries in `vendor/`:
- `miniz` (MIT / Public Domain)
- `stb_image` & `stb_vorbis` (MIT / Public Domain)
