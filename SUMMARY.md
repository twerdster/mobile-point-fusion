# mobile-point-fusion: maintenance context

Read this file first, then SKILL.md and README.md. Updated 2026-11-14. The root README is the preserved project reference for the current revival task; current executable setup/results are in updated/README.md.

## Historical source and attribution

January 2015 PointsFusion v2.3 Objective-C/OpenGL ES correspondence-shader and CPU-fusion snapshot; earlier than the October 2015 release reported by the preserved root README, which remains unrecovered/unverified. Original project credits: D. Ben-Hoda, G. Kamar, Aaron Wetzler, Y. Honen, Ron Kimmel. Source notices retain Daniel & Gal copyright.

All 28 allowlisted published files under source/original match source/PROVENANCE.json: 27 recovered byte-for-byte, one existing comment privacy edit recorded there. No historical source/project/settings or root README bytes changed in this revival. Missing historical OBJ models/launch bitmap remain missing from the archive. No blanket source license is inferred.

## Updated offline adapter

updated/ is explicitly new 2026 work: C++17 + EGL/GLES3, tested on Linux software Mesa llvmpipe. It executes unchanged original indexMap shaders, adapted Fusion/PointRemoval shaders and a portable C++ reimplementation of CPU Gaussian weighted point averages with the historical 12-float layout, sigma 0.6 in normalized-device coordinates, stability threshold 10 and six-second low-confidence age rule. It does not capture depth, estimate poses or restore a mobile live-camera pipeline.

Fixes/adaptations validate IDs before dereference/conversion, branch on -1 before a model read, correct/free/deduplicate slots, use per-program uniform locations, initialized correctly sized buffers, integer framebuffer clears, depth buffers and separated sampling/render attachments. RGBA32F preserves IDs; camera-space comparisons, high-precision samples and aligned/bounded neighborhoods are explicit adaptations. A 0.055 model-unit Euclidean match gate is new behavior; the original shader disables its proposed check. Bounded overlap removal repairs point-coordinate distances, excludes self, retains higher confidence/lower-ID survivors and emits at most three unique IDs; the original foreground-removal heuristic is omitted. Fixture point radius is 0.012 model units instead of historical constant 6.

From repository root, with Linux EGL/GLES development packages and Mesa installed:

    cmake -S updated -B build/offline -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build/offline --parallel 2
    export LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless LP_NUM_THREADS=2
    export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
    ctest --test-dir build/offline --output-on-failure
    build/offline/points_fusion --demo --frames 48 --output fusion-output

The vendor file is the checked Ubuntu path, not universal; MESA_EGL_VENDOR_FILE overrides it for CTest. Mesa EGL selection and LIBGL_ALWAYS_SOFTWARE software selection are separate. GL_EXT_color_buffer_float/RGBA32F renderability was checked. The demo writes/reloads a generated ellipsoid OBJ, selects visible samples at 192×144 from 48 supplied orbit poses and adds deterministic sinusoidal offsets up to ±0.003 model units along surface normals. Optional --obj accepts ordered v/vn point fixtures, not general face-index OBJ import. Outputs: per-view input/model PLY, reference/fused PLY and metrics JSON. Python 3.10+/NumPy/Pillow/FFmpeg tools analyze/reproject saved data; they do not execute fusion. Media GIF/MP4/PNG and actual final PLY are in updated/assets and updated/examples; labels separate generated input and known poses from real capture/tracking.

## Verified results and scope

Ubuntu 22.04/GCC 11.4/CMake 3.22.1, EGL 1.5/GLES 3.2, Mesa 23.2.1 llvmpipe LLVM 15; the documented workflow requires CMake 3.20+. No global package changes or hardware GPU/training job used. Both CTest entries pass: 25 targeted CPU/GLES assertions, including compilation/linking of the indexMap, Fusion and PointRemoval algorithm shader pairs, depth/transform checks, correspondence selection and overlap-removal IDs against CPU references. The 48-view demo compares all 95,791 rasterized correspondence records to its CPU selection reference (104,043 visible input points; projection collisions reduce rasterized count).

Demo: 81,563 merges, 14,228 insertions (including 4,352 reused slots), 4,996 removals, 9,232 final points. Nearest finite-reference RMSE rises from 0.00212 for all raw observations to 0.00392 model units; coverage within 0.02 rises 31.34%→88.60%. This is broader coverage, not demonstrated denoising. All 48 raw views cover 92.09% within the same 0.02 radius, so the first-view gain is consistent with added views rather than fusion superiority; fused coverage is about 3.5 percentage points below raw union. Final finite-reference p95/max 0.00769/0.01955 exceed the 0.003 input-noise bound. The generated reference includes hidden primitive intersections. No continuous-surface/real-sensor accuracy threshold or performance benchmark exists.

All 4,996 demo deletions are overlap removals; the synthetic age check at index24/t4.0 cannot trigger expiration. Only CPU boundary tests exercise that age rule.

ASan+UBSan with halt-on-error pass corrected CPU/GLES fixtures and all 48 views; leak detection disabled for the Mesa process, Mesa libraries/shader code not instrumented. Separate original-expression C++ diagnostics report ASan heap-buffer-overflows for -1 confidence access and more-than-three removal payload reads; these are extractions, not original iOS app executions. updated/VALIDATION.json and validation/ retain exact scopes, hashes and logs.

Original Xcode 16.1 unsigned arm64 build fails missing four OBJ resources/default.png. In a private native copy, newly generated replacements plus process locale C and command-line deployment target 12.0 produce BUILD SUCCEEDED. Keeping 7.1 fails missing libarclite; inherited C.UTF-8 also causes a copypng Perl failure. Original source/project/settings bytes remain unchanged. The generated no-face OBJ aliases only satisfy build resource presence; compatibility with the original face-index normal loader is not validated. No iOS app launch, UIKit/GLKit runtime or mobile GPU correctness is verified. updated/BUILD.md and prepare_xcode_fixture.py describe the generated replacement build separately.

## Storage and maintenance

Actual native Mac/Linux build paths and verified SSD data/media paths are recorded in the private task handoff. No environments/build trees were placed on exFAT. Recheck the SSD mount/space/task write access before large future writes.

Preserve attribution/notices and original published hashes. Keep new work under updated/, synchronize this file/SKILL before commits, check source hashes, root README identity, Markdown/media links and real outputs. The 2026-11-12 prose revision had two actual Claude reviews. Two further tools-disabled Claude editorial reviews completed 2026-11-14; useful feedback was checked against code/results and applied, including the survivor-order regression. Reviews add no runtime evidence.
