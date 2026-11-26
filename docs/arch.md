# Subsystem Architecture & Platform Matrix

Detailed technical status and hardware compatibility notes moved out of the root `README.md`.

---

## Subsystem Breakdown

| Subsystem | Status | Technical Summary |
| :--- | :---: | :--- |
| **Retail Copy Scanner (`1.0.2.1`)** | ✅ Ready | Locates `MicrosoftGame.Config` and `.rpf` archives across local drives using lightweight file-size checks. |
| **RDNA2 → SPIR-V Translator** | ✅ Ready | Unpacks and translates **26,503** shaders (`2,236` VS, `12,101` PS, `2,689` CS, `9,282` mesh, `195` geometry) passing `spirv-val`. Supports native wave64 on AMD and `--wave64-emulate` on 32-wide NVIDIA/Intel GPUs. |
| **GPU Test Harness** | ⚠️ Partial | Synthetic shader suite tested across 30+ GPU/driver setups (NVIDIA Turing–Blackwell, AMD GCN3–RDNA4, Intel ANV, Apple MoltenVK, Mesa llvmpipe) ([#166](https://github.com/ItsNotRexed/Leonida/issues/166)). |
| **3D Geometry (`OBJ`)** | ✅ Ready | Recovers **382,781** meshes across scanned archives (**343,327** from `levels.rpf`) with UVs and material references ([docs/models.md](models.md)). |
| **Texture Decoder (`DDS`)** | ✅ Ready | `rpf9dump` and `tools/bcpack.py` decode BC1–BC7 texture dictionaries and world pools into standard DDS files ([docs/textures.md](textures.md)). |
| **Audio & Radio (`AWC`)** | ✅ Ready | Repackages Opus streams losslessly to Ogg, extracts PCM WAV, decodes XMA2 via FFmpeg, and parses **1,589** radio metadata records ([docs/audio.md](audio.md)). |
| **PE Loader & Input (`gta6rt`)** | ⚠️ Partial | Executes the Windows PE binary with CRT, threading, file, and SDL3 GameInput shims (rumble, DualSense triggers/lightbar) ([docs/gameinput.md](gameinput.md)). |
| **Xbox D3D12 → Vulkan** | ⚠️ Active | Maps command lists, descriptor heaps, barriers, render targets, `PresentX` queues, and upload arenas onto Vulkan ([docs/d3d12_vulkan.md](d3d12_vulkan.md)). |
| **Ray Tracing (`XDXR BVH4`)** | ⚠️ Active | GPU BLAS/TLAS builder and shader traversal (`image_bvh_intersect_ray`) verified on synthetic scenes; `*_bvh4.rpf` rehydration ongoing ([docs/bvh.md](bvh.md)). |

To generate an HTML diagnostic dashboard from your local checkout:
```bash
python -I tools/progress_dashboard.py --out ../gta2pc-report
```
Open `progress.html` in any browser ([docs/progress_dashboard.md](progress_dashboard.md)).

---

## Platform Support

| Target Platform | Runtime & Build Behavior |
| :--- | :--- |
| **Windows 10 / 11 (x64)** | Native execution for `make_game.py`, asset extractors, shader compilers, and `gta6rt`. |
| **Linux (x86-64)** | Asset tools, shader compilers, and Vulkan tests build natively (RADV / NVIDIA). The Windows runtime cross-compiles via `clang-cl` + `xwin` and runs under Wine or Proton (`./Build.sh`, [docs/linux.md](linux.md)). |
| **macOS (Apple Silicon)** | Python tools and Vulkan tests run natively via MoltenVK; `gta6rt` runs under CrossOver / Wine with FEX or Rosetta (geometry and mesh stages skipped due to MoltenVK limits, [docs/testing.md](testing.md)). |
| **AMD RDNA1–RDNA4** | Optimal GPU architecture for testing due to native 64-wide subgroup execution matching the console's RDNA2 GPU. |
