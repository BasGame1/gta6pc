<h1 align="center">
  <br>
  <a href="https://github.com/BasGame1/gta2pc">
    <img src="assets/gta2pc.png" alt="gta2pc logo" width="500">
  </a>
</h1>

<h2 align="center">
  <br>
  ![microsoft](https://www.readmecodegen.com/api/social-icon?name=microsoft&size=32)
  ![python](https://www.readmecodegen.com/api/social-icon?name=python&size=32&color=%2310b981)
  ![linux](https://www.readmecodegen.com/api/social-icon?name=linux&size=32)
  ![xbox](https://www.readmecodegen.com/api/social-icon?name=xbox&size=32)
  ![rockstargames](https://www.readmecodegen.com/api/social-icon?name=rockstargames&size=32)
</h2>

<h4 align="center">
  Grand Theft Auto VI (Xbox Series) running natively on Windows and Linux via static recompilation — compiled locally from your own retail files.
</h4>

<p align="center">
  <a href="docs/i18n/README.md">Other languages</a> |
  <a href="#how-to-build">How to build</a> |
  <a href="#architecture-overview">Architecture</a> |
  <a href="#project-status">Project status</a> |
  <a href="#platforms">Platforms</a> |
  <a href="docs/status.md">Detailed Status</a> |
  <a href="docs/objectives.md">Objectives</a> |
  <a href="https://discord.gg/ETf4qj5acN">Discord</a> |
  <a href="https://t.me/PixelifyNext">Telegram</a>
</p>

---

> [!IMPORTANT]
> **No copyrighted game files, encryption keys, or precompiled binaries are included in this repository.**
> You must supply your own legally acquired Xbox Series package (`1.0.2.1`) already unpacked on disk. This toolchain does not bypass DRM or package encryption. All extracted textures, translated SPIR-V shaders, and runtime binaries are generated locally on your machine and must never be redistributed.

> [!NOTE]
> **Upstream Lineage:** `gta2pc` is an active continuation and experimental fork building upon [NotRexed's original Leonida static recompilation framework](https://github.com/ItsNotRexed/Leonida) and [ubiquitarianism's Leonida Continued](https://github.com/ubiquitarianism/leonida), focused on Linux/Proton stability, build pipeline refinements, and runtime compatibility fixes.

---

## Architecture Overview

**gta2pc** translates the Xbox Series edition of *Grand Theft Auto VI* into a native desktop runtime without full-system CPU emulation:

* **Native x86-64 Execution:** Because the Xbox Series console shares the x86-64 instruction set with modern PCs, the game's compiled machine code is loaded directly through a custom PE loader (`gta6rt`) paired with user-mode shims for Xbox GDK and OS kernel APIs.
* **RDNA2 to SPIR-V Shader Translation:** Precompiled console GPU microcode is unpacked from the game archives, disassembled, and translated into standard Vulkan SPIR-V modules—including wave64 subgroup emulation for 32-wide NVIDIA and Intel GPUs.
* **Xbox D3D12-to-Vulkan Translation Layer:** Console-specific Direct3D 12 extensions, command-list layouts, `PresentX` swapchain queues, and XDXR ray-tracing acceleration structures (BVH4) are mapped onto standard desktop Vulkan APIs.
* **Local Asset Extraction Pipeline:** RPF9 archives, BCPack-compressed texture dictionaries, AWC audio banks, and 3D world geometry are decoded locally from your game directory during the build step.

---

## Project Status

**Current State: Bootable through initial UI and story load; in-game 3D world rendering is still under active development.**

On native Windows and Linux (via Proton), `gta2pc` launches the game binary from your local package, plays the startup sequence, navigates the initial setup screens, and completes the story-mode loading phase before reaching the world initialization stage. Automated UI progression (`LEONIDA_INPUT_SCRIPT`) is also functional on Windows for unattended testing.

| Subsystem | Current Progress | Technical Notes |
| :--- | :---: | :--- |
| **Game Boot & Intro Sequence** | ✅ Working | Boots cleanly on Windows and Linux/Proton through intro movies, legal prompts, and story loading ([Linux report](docs/linux_proton.md)). |
| **Gameplay & World Rendering** | ⚠️ In Progress | Reaches the end of the story load sequence; first playable 3D world frame is not yet verified. |
| **Package Detection (`v1.0.2.1`)** | ✅ Working | Automatically locates unpacked Xbox Series `MicrosoftGame.Config` and `.rpf` directories on local drives. |
| **RDNA2 Shader Disassembly & SPIR-V** | ✅ Complete | **26,503 / 26,503** shaders carved, translated to SPIR-V, and validated with `spirv-val` (supports native 64-wide AMD subgroups and emulated 32-wide NVIDIA/Intel subgroups). |
| **3D Models & World Geometry** | ✅ Working | **382,781** models recovered across scanned archives (including `levels.rpf`) with UVs and material references intact ([notes](docs/models.md)). |
| **Textures (BCPack → DDS)** | ✅ Working | Clean-room decoder (`tools/bcpack.py`) reconstructs BC1–BC7 textures into standard DDS pools ([notes](docs/textures.md)). |
| **Audio & Radio Streams (AWC)** | ✅ Working | Lossless Ogg Opus repackaging, PCM WAV extraction, XMA2 decoding via FFmpeg, and **1,589** radio metadata records parsed ([notes](docs/audio.md)). |
| **PE Loader & GameInput Shim** | ⚠️ Partial | Runs `gta6rt` with CRT/threading/file shims and SDL3 controller support (rumble, DualSense triggers/lightbar) ([notes](docs/gameinput.md)). |
| **Xbox D3D12 → Vulkan Proxy** | ⚠️ In Progress | Core command lists, descriptors, barriers, render targets, and `PresentX` queues work; console list tail and descriptor-table edge cases remain ([plan](docs/d3d12_vulkan.md)). |
| **Hardware Ray Tracing (XDXR BVH4)** | ⚠️ In Progress | GPU-side BLAS/TLAS builder and shader traversal (`image_bvh_intersect_ray`) verified on synthetic scenes; `*_bvh4.rpf` rehydration under investigation ([notes](docs/bvh.md)). |

---

## Platforms

| Operating System / GPU | Compatibility & Runtime Setup |
| :--- | :--- |
| **Windows 10 / 11 (x64)** | Primary target. Builds and runs `make_game.py`, asset extractors, and the native `gta6rt` D3D12-to-Vulkan runtime directly. |
| **Linux (x86-64)** | Asset tools, shader translators, and Vulkan test suites compile natively (tested on Mesa RADV and NVIDIA proprietary drivers). The Windows runtime cross-compiles via `clang-cl` + `xwin` and runs under Wine/Proton (`./Build.sh`, see [docs/linux.md](docs/linux.md)). |
| **macOS (Apple Silicon)** | Python asset tools and Vulkan tests run natively via MoltenVK; the Windows runtime executes under CrossOver / Wine with FEX or Rosetta (geometry/mesh shaders skipped due to MoltenVK limits, see [docs/testing.md](docs/testing.md)). |
| **AMD RDNA1 / RDNA2 / RDNA3+** | Recommended GPU architecture for testing due to native 64-wide wave execution matching the Xbox Series RDNA2 GPU without subgroup emulation overhead. |

---

## How to Build

Everything is compiled and extracted directly on your PC from your existing game installation:

1. **Clone the repository with submodules:**
   ```bash
   git clone --recursive [https://github.com/BasGame1/gta2pc.git](https://github.com/BasGame1/gta2pc.git)
   cd gta2pc
