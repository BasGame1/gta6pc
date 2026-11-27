# Acknowledgments

This file records the patches and mechanisms integrated into this fork to resolve story-load stalls, crashes, memory exhaustion, and input/pacing gaps after the menu and intro sequence, along with their upstream repositories and licences. All projects credited below for ported implementations are **GPL-3.0-or-later** compatible; no game files, keys, or copyrighted title data are included.

## 1. Belobug/VIctory-Belobug-AMD (`GPL-3.0-or-later`)

Source repository: <https://github.com/Belobug/VIctory-Belobug-AMD> (fork of `Marvisak/VIctory` and `fxceless/victory-fork`, building on `avatarmanz/VIctory-7900XTX`)

- **DirectStorage file-read and physical-page retries (`runtime/shims/dstorage.cpp`)**
  - *What:* Retries `ERROR_NOACCESS` on committed title memory (`touch_pages` + exponential backoff up to 8 attempts) when a file read races with `ShadowPages` re-guarding, and retries transient `kDestinationPages` transfer failures (`LEONIDA_DSTORAGE_RETRIES=0` disables).
  - *Origin:* `crates/victory-hle/src/modules/dstorage.rs` (`READ_RETRIES = 8`, `VICTORY_DSTORAGE_RETRIES`).
- **VRAM / out-of-memory speculative upload backoff (`runtime/shims/d3d12_vulkan_device.cpp`)**
  - *What:* When a title texture fill/upload fails under memory pressure, pauses speculative `refresh_title_textures` uploads for 500 ms and releases idle `TitleImageUpload` staging to prevent thrashing and device loss on 8 GB VRAM GPUs.
  - *Origin:* `crates/victory-gpu/src/residency.rs` (`upload_refused()` 500 ms backoff).
- **Frame interval override (`runtime/shims/xbox_present.cpp`)**
  - *What:* Adds opt-in `LEONIDA_FRAME_INTERVAL_MS=<ms>` (`0..1000`; `0` uncapped) to override `SetFrameIntervalX` (e.g., `16.667` for 60 Hz) without modifying the title's clock.
  - *Origin:* `crates/victory-gpu/src/frame.rs` (`VICTORY_FRAME_INTERVAL_MS`).
- **GTA V PC-style keyboard and mouse mapping (`runtime/shims/gameinput.h`, `runtime/shims/gameinput.cpp`, `runtime/test/gameinput_test.cpp`)**
  - *What:* Adds opt-in `LEONIDA_KBM_GTA=1` bindings (`Shift`=A, `Space`=X, `R`=B, `F`=Y, `Tab`=LB, `Q`=RB, `Ctrl`=L3, `MMB`/`C`=R3, `E`=D-pad Right, `V`=View, `P`=Menu), `F2`/`~` mouse-look cursor-lock toggle (unlocked on `Esc`/`P`/focus loss), `I`/`J`/`K`/`L` right-stick fallback, and `LEONIDA_MOUSE_INVERT_Y=1`.
  - *Origin:* `Belobug/VIctory-Belobug-AMD` PR #295 (`crates/victory-hle/src/modules/gameinput.rs`, `VICTORY_KBM_GTA`).

## 2. ubiquitarianism/leonida (`GPL-3.0-or-later`)

Source repository: <https://github.com/ubiquitarianism/leonida>

### Merged upstream fixes integrated from `ubiquitarianism/main` (`48dc9ee0b..63185f4a3`)

- **PR #1867 (`d1dff7949`, `azsekazvamdimitar`)**: `ShadowPages` guards aliased title pages at every mapped address via geometry-watch cells instead of re-copying every submission (`runtime/shims/d3d12_vulkan_device.cpp`, `runtime/test/d3d12_existing_heap_test.cpp`).
- **PR #2404 (`e5a7c8942`, `OreoFR`)**: Title write-back queries the write watch first and copies back only differing bytes (`runtime/shims/d3d12_vulkan_device.cpp`).
- **PR #2418 (`04d6d2518`, `OreoFR`)**: Prevents recursive `ShadowPages::lock_` deadlock in the fault handler when a write-back faults on a guarded page (`runtime/shims/d3d12_vulkan_device.cpp`, `runtime/test/d3d12_existing_heap_test.cpp`).
- **PR #2407 (`627163c8e`, `aslan2142`)**: Tracks resolved V# records per console heap slot so releases/remaps only re-fix affected slots, eliminating multi-second texture-phase stalls during story load (`runtime/src/console_heap_slot_records.h`, `runtime/shims/d3d12_vulkan_device.cpp`).
- **PR #2406 (`20c46b646`, `aslan2142`)**: Sizes every sampled bindless descriptor dimension uniformly and reports full-array refusals (`runtime/src/vulkan_bindless.h`).
- **PR #2230 (`55e2bd9e9`, `Tyrannys`) & PR #2408 (`011d446c3`, `aslan2142`)**: Clamps indirect compute dispatch records by `maxComputeWorkGroupCount` and logs dispatch width counters, preventing GPU hangs on large dispatches (cross-checked with `Belobug/VIctory-Belobug-AMD` `crates/victory-gpu/src/indirect.rs`) (`runtime/src/indirect_clamp.comp`, `runtime/src/vulkan_indirect_clamp.h`).
- **PR #2102 (`8bc59a74c`, `DadaCap`)**: Mesh shader epilogue tail fix (`tools/rdna2spirv.py`).
- **PR #2440 (`d071531bc`, `OreoFR`)**: `LEONIDA_ASYNC_MIN_BYTES` keeps small pipelines synchronous so draws do not stall behind oversized background compiles (`runtime/src/async_pipeline_size.h`, `runtime/shims/d3d12_pipeline_compile.h`).
- **PR #2401 (`fac8b4bb9`, `aslan2142`) & PR #2432 (`63185f4a3`, `OreoFR`)**: Runtime Vulkan memory breakdown diagnostics and `LEONIDA_TRACE_XGAMESAVE` logging.
- **PR #2123 (`ef3493186`, `qhukz13`)**: Enables depth-stencil UAVs (`R16_TYPELESS` + `R8_UINT`) for console `D16S8` textures backed as `D24_UNORM_S8_UINT` on NVIDIA GPUs (`runtime/shims/d3d12_vulkan_device.cpp`, `runtime/src/vulkan_backend.h`, `runtime/src/vulkan_replay.h`, `runtime/test/xbox_depth_storage_test.cpp`).

### Open pull requests integrated from `ubiquitarianism/leonida`

- **PR #2454 (`Tyrannys` / `KushGene`)**: Fixes `erase_overlapping` overflow in `ConsoleHeapKeys` and preserves newer slot flags on rollback (`runtime/src/console_heap_gpu.h`, `runtime/test/vk_console_heap_gpu_test.cpp`).
- **PR #2361 (`qhukz13`)**: Serializes `PipelineCompiler` jobs per PSO owner so a released PSO is not deleted while another build is active (`runtime/shims/d3d12_pipeline_compile.h`, `runtime/test/d3d12_compile_owner_test.cpp`).
- **PR #2374 (`Tauwni`)**: Keeps the rethrown `std::exception_ptr` alive for Wine 11.0's `std::rethrow_exception` in `PipelineWorkers::run()` (`runtime/src/pipeline_workers.h`).
- **PR #2352 (`ecox982745`)**: Preserves GPU write-backs to adjacent placed buffers in either address order (`runtime/shims/d3d12_vulkan_device.cpp`, `runtime/test/d3d12_title_va_test.cpp`).
- **PR #2402 (`wavehs`)**: Guards `retire_tickets_` reads under their own lock to prevent submit-lock contention (`runtime/src/vulkan_backend.h`, `runtime/shims/d3d12_vulkan_device.cpp`, `runtime/test/retire_ticket_lock_test.cpp`).
- **PR #2365 (`HasiKe`)**: Treats an unexpected `UINT64_MAX` timeline fence counter as device loss without stalling under `vkDeviceWaitIdle` (`runtime/src/vulkan_backend.h`, `runtime/shims/d3d12_com.cpp`, `runtime/src/queue_holds.h`).
- **PR #2405 (`pepefroge`)**: Checks sparse buffer `memory_types` before sharing the tile arena (`runtime/shims/d3d12_vulkan_device.cpp`).
- **PR #1878 (`lixonik`)**: Preserves CPU-backed source pages in `CopyPageMappingsBatchX` (queue slot 31) remaps (`runtime/shims/xmem.cpp`, `runtime/test/xmem_test.cpp`).
- **PR #1715 (`kawaii-shakal`)**: Rebuilds BC6H/BC7 DirectStorage requests within the lost-block limit instead of failing the request (`runtime/shims/dstorage.cpp`, `runtime/src/bcpack.cpp`, `runtime/src/bcpack.h`, `runtime/test/dstorage_test.cpp`).
- **PR #2234 (`star41905453-coder`)**: Propagates `depth_bounds` and back-stencil state on pending PSO streams (`runtime/shims/d3d12_vulkan_device.cpp`, `runtime/test/d3d12_mesh_test.cpp`).
- **PR #2436 (`kawaii-shakal`)**: Preserves GPU driver shader cache across `run_game_wine.sh` runs (`scripts/run_game_wine.sh`, `tests/test_run_game_wine.py`).
- **PR #2438 (`timbot103`)**: Checks high 32 bits for buffer offset fit in `Gen.buffer_address` to prevent NVIDIA NVVM pipeline creation crashes (`tools/rdna2spirv.py`, `tests/semantic/test_buffer_offset_wrap.py`).
- **PR #2397 (`DadaCap`)**: Clamps mesh shader primitive indices below vertex count (`tools/rdna2spirv.py`, `tests/semantic/test_ms_prim_export.py`).
- **PR #2416 (`ninjahawk`)**: Lowers pixel shader `quad_perm` DPP reads through Vulkan quad operations instead of inactive-helper-lane shuffles, matching the shadow quad-hole fix also documented in `Belobug/VIctory-Belobug-AMD` (`tools/rdna2spirv.py`, `tests/semantic/test_dpp_quad_ops_ps.py`).
- **PR #2439 (`OreoFR`)**: Decodes only written typed-load components and emits `fconst` as `OpConstant` (`tools/rdna2spirv.py`, `tests/semantic/test_buffer_format_decode_one.py`, `tests/semantic/test_fconst_constant.py`).
- **PR #2434 (`z3r0rc`)**: Pins BCPack BC7 rank 152 to measured mode 3 (`tools/bcpack_tables_bptc.py`, `tests/test_bcpack.py`, `tests/test_bcpack_tables_bptc.py`).
- **PR #2117 (`azsekazvamdimitar`)**: Memoizes console heap V# slot resolutions per pass so repeated V# words are resolved once (`runtime/shims/d3d12_vulkan_device.cpp`, `runtime/test/d3d12_unmapped_vsharp_test.cpp`).

## 3. PortWatch VI Reference Survey (`docs-only` / corroboration)

Tracked via <https://github.com/4lwayz/portwatch-vi>:

- **ThatRecompDev/five-star-recomp (`GPL-3.0-or-later`)**: Corroborated first-run scripted gamepad flow (`LEONIDA_INPUT_SCRIPT`) and `TitleTexture` registry design (`docs/d3d12x-shim.md`).
- **luizwidmer/palmetto (`GPL-3.0-or-later`)**: Corroborated physical-page DirectStorage write-watch invalidation and non-blocking pipeline compilation requirements (`docs/peer_port_streaming.md`, `docs/peer_port_first_ui.md`).
- **Steinkoloss/port-of-the-americas (`no licence`)**: Referenced only for high-level Linux/Wine diagnostic notes (e.g., driver shader cache retention and low-address arena guard corroboration); no code copied.
