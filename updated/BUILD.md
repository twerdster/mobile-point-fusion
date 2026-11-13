# Build and validation record

The maintained executable is a Linux EGL/GLES3 adapter, not an iOS application replacement. See [README](README.md) for its scope and standard commands.

## Sanitizer procedure

```sh
cmake -S updated -B build/asan -DCMAKE_BUILD_TYPE=Debug -DSANITIZERS=ON
cmake --build build/asan --parallel 2
export LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless LP_NUM_THREADS=2
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 build/asan/points_fusion --cpu-test --gpu-test --demo --frames 48 --output fusion-asan
```

This instruments the C++ process. It does not instrument Mesa libraries or shader machine code or check leaks. UBSan recovery is disabled in the sanitizer build, halt_on_error is enabled for the recorded run, and the completed log was checked for reports. The original-fault extractions below intentionally fail; each must report an AddressSanitizer heap-buffer-overflow, not be counted as a successful run:

```sh
ASAN_OPTIONS=detect_leaks=0 build/asan/original_faults insert
ASAN_OPTIONS=detect_leaks=0 build/asan/original_faults removal
```

The extraction files identify the original expressions. The original app itself is not compiled with these diagnostics or run by these commands.

## Original Xcode check

The 2026-11-14 check uses Xcode 16.1 / iOS 18.1 SDK, an isolated native-filesystem copy, generic iOS destination, arm64, and `CODE_SIGNING_ALLOWED=NO`. The as-recovered project fails with exit 65 because `zero.obj`, `monkey.obj`, `capsule.obj`, `Porsche_911_GT2.obj` and `default.png` are absent. Nothing under `source/original` is changed.

To prepare a private copy with known new resources after running the offline demo:

```sh
python3 updated/tools/prepare_xcode_fixture.py fusion-output/generated-input.obj /absolute/new/private/PointsFusion
LC_ALL=C LC_CTYPE=C LANG=C xcodebuild \
  -project /absolute/new/private/PointsFusion/PointsFusion.xcodeproj \
  -scheme PointsFusion -sdk iphoneos -configuration Debug \
  -destination generic/platform=iOS \
  -derivedDataPath /absolute/new/private/derived \
  CODE_SIGNING_ALLOWED=NO ARCHS=arm64 ONLY_ACTIVE_ARCH=YES \
  IPHONEOS_DEPLOYMENT_TARGET=12.0 build
```

The generated point OBJ has ordered v/vn records and no faces; the historical loader assigns vertex normals inside face handling. These replacements satisfy resource presence for building only, not validated input compatibility with that loader. The four OBJ aliases all contain the same generated ellipsoid assembly; they are not recovered monkey, capsule or Porsche models. `default.png` is a plain new placeholder. Locale `C` avoids a Mac Perl/copypng failure observed with the inherited `C.UTF-8` environment. Keeping deployment target 7.1 subsequently fails at missing `libarclite_iphoneos.a`; the command-line 12.0 target is an explicitly updated build setting. These steps do not repair or validate the historical app's GPU/CPU runtime behavior.

With the generated resources, locale `C` and command-line target 12.0, the recorded unsigned arm64 build **completed successfully**. No source/project file was edited to obtain that build, and no iOS app was launched. [Original failure excerpt](validation/xcode-original-excerpt.log), [old-target failure](validation/xcode-ios71-excerpt.log) and [updated build result](validation/xcode-ios12-excerpt.log) preserve that distinction.

## Implementation and retained behavior

The Linux code is a portable C++ reimplementation of the fusion loop in ViewController.m; the original Objective-C loop is not executed here. The original indexMap shader pair executes unchanged. The other two algorithm pairs, Fusion and PointRemoval, are adapted under updated/shaders. The template Shaders/Shader pair is not part of this pipeline.

The CPU retains the 12-float record layout and Gaussian confidence weight with sigma 0.6 in normalized-device coordinates. CPU position/normal averages follow the historical equations. Stored averaged normals are not renormalized there, but the unchanged index-map vertex shader normalizes them before they enter correspondence/removal textures; the query shaders normalize their normals too. A GPU fixture checks this normalization. Stability threshold 10 and the low-confidence six-second age rule are retained. The demo supplies synthetic timestamps at view_index/6, with age checks every 25 views; they are not wall-clock performance measurements. The 48-view run reaches one age check at view 25 (index 24, timestamp 4.0); no point is six seconds old then. Age expiry never triggers in that demo, whose 4,996 deletions are all overlap removal. The CPU boundary tests separately exercise expiration.

Adapters validate IDs before pointer access or integer conversion, branch on the -1 new-point sentinel before any model read, initialize point records, size index buffers from their data, maintain reusable slots and prevent duplicate frees. Per-program uniforms replace the historical shared location array. Integer framebuffer clears and depth attachments are explicit. Fusion samples index textures from a separate framebuffer, avoiding attached-texture feedback.

The historical host allocates the fusion texture as GL_RGBA16F while the shader writes identifiers into float components. The adapter uses RGBA32F to retain larger integer IDs; this is a representation change, not evidence that an original iOS run exhibited rounding. Position/normal samplers are high precision. Neighborhoods align with the current low-resolution pixel, bound their texel accesses and preserve the confidence-first/ray-distance tie-break rule.

Correspondence adds a 0.055 model-unit Euclidean gate where the historical shader disables its proposed geometric test with if(true). It is a fixture parameter, not a calibrated sensor threshold. Both correspondence and removal use a 20° normal-angle threshold, passed in radians (20*pi/180) despite the historical maxDegree uniform name. New fixture radius is 0.012 model units; the historical constant 6 has no recovered metric calibration, so these constants are not comparable physical radii.

Removal compares actual camera-space point coordinates, excludes self and emits at most three unique IDs. A query can delete only lower-confidence neighbors, or a higher-ID neighbor at equal confidence. This strict order prevents overlapping pairs from deleting each other. Both index and removal passes draw the same slot-indexed model buffer with slot IDs as element indices, so gl_VertexID equals model slot ID; this is required for self-exclusion and tie ordering. The GPU fixtures exercise this invariant. The query depth buffer still selects one fragment per low-resolution pixel, so colliding queries may not all produce records; the three-ID payload also limits removals per fragment. The original foreground-removal heuristic is omitted. This is explicitly changed deletion behavior.

The full demo compares each rasterized correspondence decision with a CPU selection reference. GPU fixtures separately check index transforms/depth, normal rejection, geometric rejection, bounded removal IDs and survivor ordering. CPU tests cover sentinel insertion, weighted averages, finite/ranged IDs, removal counts, duplicate deletion, reuse and age/stability boundaries. Passing these checks does not establish correctness for every model, view or driver.

## Saved outputs and optional tools

The default demo creates 21,824 samples on an ellipsoid assembly, writes generated-input.obj and reloads the file. Observations are already registered in world coordinates. A supplied orbit determines visibility; it does not estimate registration or recover depth. --obj accepts ordered one-normal-per-vertex point fixtures with optional RGB values on vertex records. Faces and independent face/normal indices are not imported.

After installing NumPy/Pillow in a native-filesystem environment and making FFmpeg available:

    python3 updated/tools/analyze_demo.py fusion-output
    python3 updated/tools/render_demo.py fusion-output

Analysis compares saved clouds with finite reference samples in arbitrary model units. It includes the first input and the union of all raw input observations as baselines, so adding views is not confused with a fusion improvement. It reports nearest-reference RMSE/median/p95/maximum and coverage within 0.02 units. The reference includes hidden primitive intersections; it is not a continuous exterior mesh. Fused error exceeds the bounded input noise, and no accuracy pass threshold is assigned.

The renderer only reprojects saved PLY positions/normals/colours into a point-splat view. It does not reconstruct or create new observations. Metrics captions are read from metrics.json. It emits PNG, GIF, MP4 and a source-file/hash record, enforcing GIF <2,000,000 and MP4 <10,000,000 bytes. AppleDouble sidecars on exFAT are excluded from numbered PLY inputs. Playback is six frames per second; its duration does not measure reconstruction speed.

The checked Mesa driver advertises GL_EXT_color_buffer_float, allowing RGBA32F color attachments; framebuffer completeness is also checked at runtime. A separate two-view --obj replay of the generated ordered fixture completed with exit 0. This checks that fixture path, not general OBJ compatibility.
