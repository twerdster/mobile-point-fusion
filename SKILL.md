# Maintaining Mobile Point Fusion

Read SUMMARY.md first, then this file, root README, updated/README.md, updated/BUILD.md and updated/VALIDATION.json. The current revival keeps root README as its project reference and puts executable instructions/new results under updated/.

## Preservation

Keep all 28 published original allowlist hashes matching source/PROVENANCE.json, including the already recorded comment privacy edit. Do not overwrite source/original, add an inferred blanket license or imply the unrecovered final October 2015 release exists. Preserve D. Ben-Hoda, G. Kamar, Aaron Wetzler, Y. Honen, Ron Kimmel credits and Daniel & Gal notices. Generated fixtures/replacement resources are new work, not recovered historical models.

## Build and verification

Build updated/CMakeLists.txt on Linux with C++17, CMake≥3.20, pkg-config EGL/GLES and Mesa ES3 integer/RGBA32F framebuffer support (checked GL_EXT_color_buffer_float). The checked software renderer is llvmpipe; use LIBGL_ALWAYS_SOFTWARE=1, EGL_PLATFORM=surfaceless and the verified Mesa GLVND vendor JSON path to avoid selecting an unrelated hardware GPU. LP_NUM_THREADS=2 bounds software-render worker count. Use MESA_EGL_VENDOR_FILE to override the CTest vendor path. Check printed renderer/version rather than assuming a driver.

Run both CTest entries and the affected multi-view demo for runtime changes. They cover targeted CPU state/weights and ES3 transform/depth/candidate/removal fixtures, not general algorithm correctness. Demo correspondence records compare to CPU selection; report rasterized-record versus input-point counts separately. Validate supplied-pose/generated-input labels, units, finite-reference coverage and distance errors together. Compare with the union of all raw views as well as the first view: coverage gain largely reflects extra views, while final error tails exceed input noise. Current nearest-reference RMSE grows; do not claim denoising. Reference primitive samples include hidden intersections; no real-sensor/continuous-mesh truth is available.

For memory changes, run the ASan/UBSan build described in updated/BUILD.md, including corrected tests and 48 views. Use UBSAN_OPTIONS=halt_on_error=1, check logs for reports, and retain nonrecovering UBSan compile flags. Leak detection is disabled for the Mesa process; Mesa libraries and shaders are not ASan-instrumented; retain those limits. Original-fault executables intentionally demonstrate two extracted reads and must not be described as original app runs. Keep failing evidence and corrected control outcomes.

The adapter changes more than platform glue: camera-space matching, aligned/bounded neighborhoods, a 0.055 model-unit distance gate, radius 0.012, confidence/ID-ordered bounded overlap-only removal and omission of the unsafe foreground heuristic. Preserve explicit qualifications when comparing original behavior. Optional OBJ import requires ordered one-normal-per-vertex fixtures; no general face-index parsing, depth import, pose registration or live capture exists.

## Media, iOS and handoff

render_demo.py only reprojects saved per-view PLY; no new fusion occurs. Keep generated-input/supplied-pose/no-camera labels, saved-playback timing, original credits and actual PLY/metrics hashes. GIF must remain below 2,000,000 bytes and MP4 below 10,000,000 bytes; fully decode/probe them and inspect actual stills/contact sheets. Keep point samples distinct from meshes and hardware execution distinct from software Mesa.

Original iOS 7.1 build fails missing historical resources. prepare_xcode_fixture.py creates a new private copy with a generated model under four required resource names and a plain launch placeholder. Recorded locale C + command-line iOS 12 target yields an unsigned arm64 build; app never launched. Do not change original source/settings or infer mobile runtime correctness from build success.

Native work/build/env paths and SSD data locations are in the private task handoff. Recheck external SSD mount identifiers/free space/task write access before large writes; keep builds/environments on native filesystems.

Before every commit, review/synchronize SUMMARY.md and this file for commands, behavior, outputs, validation, storage and limitations. Verify original hashes, root README identity, Markdown/media links and diff scope. There is no packaging step for these documentation files. Request an actual Claude review when asked, check suggestions against source/results and only record completed reviews. No commits/pushes before coordinating agent review in current task.

The 48-view demo does not trigger age expiry (only index24/t4.0 is checked); its removals are overlap removals. CPU boundary tests exercise six-second expiration. Original/generated Xcode fixture resources satisfy building only, not the original face-index OBJ loader. Two actual tools-disabled Claude reviews completed 2026-11-14; recorded feedback was checked/applied and is not runtime validation.
