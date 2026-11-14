# Native frontends

These are new 2026 frontends around the updated fusion adapter, not restored historical mobile applications. Each runs correspondence shaders, C++ weighted averages and bounded overlap-removal shaders while rendering the current observations and accumulated model. The app does not play a precomputed cloud or video.

Input remains the generated 21,824-point ellipsoid fixture: observations are selected from supplied world-space poses and receive the same bounded sinusoidal normal offsets as the offline demo. No platform captures RGB-D, estimates poses or validates a live sensor pipeline. The displayed orbit can be changed without changing the supplied acquisition poses.

## Controls and output

The app automatically processes 48 views: 47 distinct acquisition poses, followed by a revisit of the first pose with a new deterministic noise seed. Pause/Resume stops and resumes processing of supplied views; Reset and run clears the model and recomputes the sequence. Drag either view to change the display orbit. Linux also accepts Space, R and Escape. The nominal minimum interval between processing views is 0.15 seconds; actual processing and recording delays vary, so playback is not a performance benchmark.

Every fusion-pass correspondence selection is compared with a CPU selection computed from the read-back GPU index map before application; a mismatch throws. Index-map rasterization and removal-shader output are not independently checked in native runs. `native-state.json` records actual counters, renderer and GL version after each view. `fused.ply` is written after view 48. These targeted comparisons do not establish correctness for arbitrary inputs, views or drivers. The existing finite-reference error/coverage limitations still apply; broader coverage is not demonstrated denoising.

Output locations:

- macOS: `~/Library/Application Support/MobilePointFusionNative/`.
- iOS: the application's `Documents/MobilePointFusionNative/` container.
- Android: the application's files directory under `native-output/`.
- Linux: `mobile-fusion-native-output/`, or the `--output` directory.

## Apple builds

The checked Apple builds target arm64 Macs and arm64 simulators; Intel Macs/x86 simulators are not supported by these scripts. The installed Xcode SDKs supply all dependencies. No historical Xcode project/settings are changed.

```sh
python3 updated/platforms/apple/build.py --platform macos --output build/native-macos
open build/native-macos/MobilePointFusion.app
python3 updated/platforms/apple/build.py --platform simulator --output build/native-simulator
```

The direct simulator build is a build artifact, not evidence of simulator execution. For a physical device, generate a separate Xcode target and supply signing configuration externally:

```sh
python3 updated/platforms/apple/generate_ios_project.py --output build/native-ios-project
xcodebuild -project build/native-ios-project/MobilePointFusion.xcodeproj \
  -target MobilePointFusion -configuration Debug -sdk iphoneos \
  -destination generic/platform=iOS -allowProvisioningUpdates \
  DEVELOPMENT_TEAM="$PF_APPLE_TEAM" CODE_SIGN_IDENTITY='Apple Development' \
  SYMROOT="$PWD/build/native-ios-products" build
```

Regenerate the private iOS project after shader edits: its resource folder is a generation-time copy. Keep generated projects/build outputs outside version control.

macOS uses Cocoa/NSOpenGLView with OpenGL 4.1. It changes the GLSL version declaration from `300 es` to `330 core` and allocates equivalent textures using `glTexImage2D`, since immutable texture storage is unavailable in Apple's GL 4.1 API. Shader algorithm bodies remain the same.

iOS uses UIKit/CAEAGLLayer with an EAGL ES3 context. The first physical iPhone 12/iOS18.0.1 build reported an incomplete index framebuffer using RGBA32F; that diagnostic did not record the status enum. ES3 does not require RGBA32F renderability. Its updated representation uses RGBA32UI color attachments: fragment wrappers store `floatBitsToUint`, sampler helpers recover `uintBitsToFloat`, and integer readback preserves the original 32-bit float records and IDs. It does not reduce values to half floats. Original files remain unchanged; the representation wrappers are generated at shader compilation. A Linux control run in this storage mode produced a byte-identical final PLY to the float-storage run. This checks the representation on Mesa; the physical iOS execution is separately recorded. [Required ES3 integer targets](https://registry.khronos.org/OpenGL/specs/es/3.0/es_spec_3.0.pdf#page=141) replace the float-renderability dependency for this mode.

## Android build

The checked build uses JDK 21, Android platform/build-tools 34 and NDK 27.2.12479018. The native library targets arm64/API 26. An existing D8 8.2.33 JAR from Android command-line-tools 12.0 was used; build-tools 34's older D8 failed internally on these classes. Supply an installed compatible R8/D8 JAR explicitly; the script does not download tools or modify the SDK.

```sh
python3 updated/platforms/android/build.py --sdk "$PF_ANDROID_SDK" \
  --r8-jar "$PF_R8_JAR" --output build/native-android
adb install -r build/native-android/MobilePointFusion.apk
adb shell am start -n io.github.twerdster.mobilepointfusion/.FusionActivity
adb shell run-as io.github.twerdster.mobilepointfusion cat files/native-output/native-state.json
```

The default APK requires RGBA32F framebuffer rendering (checked on Adreno540); `--integer-float-storage` selects the iOS-style representation for another ES3 device. Storage mode is recorded in new run JSON. The debug APK uses Java Activity/GLSurfaceView controls and a C++ JNI bridge executing on the view's ES3 thread. It requests no camera, storage or network permission. The local debug keystore stays in the build output and is not a release-signing identity.

## Linux build

Add X11 development libraries to the offline EGL/GLES requirements. Use an available desktop display; Xvfb can provide an isolated validation display.

```sh
cmake -S updated -B build/native-linux -DNATIVE_UI=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native-linux --parallel 2
ctest --test-dir build/native-linux --output-on-failure
export LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 EGL_PLATFORM=x11
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
build/native-linux/points_fusion_ui --output native-linux-output
```

`--auto-exit` closes the UI after completing 48 views, for a bounded validation run. `-DSANITIZERS=ON` instruments the UI target as well as the command-line targets. Set `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`; Mesa/shader machine code is not instrumented and leak detection remains disabled. `-DINTEGER_FLOAT_STORAGE=ON` exercises the same lossless storage used on iOS; ordinary Linux runs retain RGBA32F.

## Recorded platform results

The initial macOS run completed 48 views on Apple M3 Max/OpenGL 4.1: 9,227 final points, 81,563 merges, 5,000 removals and 95,790 CPU-checked shader records. Reset/Pause/Resume were exercised through native controls; the point clouds were visually inspected. Linux X11/EGL on an isolated Xvfb display completed 48 views on Mesa llvmpipe: 9,232 points, 81,563 merges, 4,996 removals and 95,791 checked records. Its full UI sanitizer run completed without reports. Observed platform differences are recorded rather than forcing a universal point count; their precise cause has not been isolated.

A physical Pixel 2/Android 10 run on Adreno 540 completed the same 48-view fixture with 9,227 points, 81,563 merges, 5,000 removals and 95,790 checked records. Physical iPhone 12/iOS 18.0.1 on Apple A14 GPU completed the fixed lossless-storage run with the same counts. Both frozen mobile builds were rerun and recorded on those devices. [Exact device JSON and validation](NATIVE_VALIDATION.json) retain the results and artifact hashes. Signed builds alone are not runtime evidence.

One [iOS background/foreground check](validation/native-platforms/ios-lifecycle.json) after view 48 retained the same process. At 20 seconds in that separate recording, both clouds and the completed counters remained visible without error text. This checks return to the completed scene; interruption during fusion and repeated lifecycle cycles were not tested. Recordings retain generated-input/supplied-pose qualifications and identify their actual platform. Original credits and source-license limitations remain unchanged.

## Screen recordings

| Native app | Recording | Final points / merges / removals |
| --- | --- | --- |
| Mac M3 Max / OpenGL4.1 | [GIF](assets/macos-native.gif) · [MP4](assets/macos-native.mp4) · [PNG](assets/macos-native.png) | 9,227 / 81,563 / 5,000 |
| Linux Xvfb / software Mesa llvmpipe | [GIF](assets/linux-native.gif) · [MP4](assets/linux-native.mp4) · [PNG](assets/linux-native.png) | 9,232 / 81,563 / 4,996 |
| Pixel 2 / Android10 / Adreno540 | [GIF](assets/android-native.gif) · [MP4](assets/android-native.mp4) · [PNG](assets/android-native.png) | 9,227 / 81,563 / 5,000 |
| iPhone 12 / iOS18.0.1 / Apple A14 | [GIF](assets/ios-native.gif) · [MP4](assets/ios-native.mp4) · [PNG](assets/ios-native.png) | 9,227 / 81,563 / 5,000 |

These are actual app-window captures with generated input and supplied poses. Their on-screen CPU count refers specifically to fusion correspondence selections from the GPU index map; it does not independently verify index rasterization or removal decisions. The separate offline preview in README reprojects saved PLYs. Source-screenshot hashes and encode/probe details accompany the recordings.

The UIKit display link pauses and flushes GL before resigning active, resumes on activation, and avoids GL drawing in the background. Drawable allocation skips zero sizes and unchanged sizes; layout/display exceptions are contained and shown on screen. Android releases native state on finishing and discards old context handles safely; an EGL context loss restarts the generated fixture. The manifest handles orientation/screen-size changes without recreating the activity. The storage wrappers target the supplied shader declarations, not arbitrary GLSL; test both representations after shader edits.
