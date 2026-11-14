# Recorded Kinect RGB-D replay

The adapter accepts measured depth and RGB images from TUM Freiburg
1 xyz, the same archive used for [OpenFusion's real-data run][openfusion]. The
prepared reconstruction input contains 120 selected frames spanning 26.594239
seconds of the 798-frame recording. It samples every fourth image pixel to
160 × 120. It does not process all 798 frames. Camera poses come from TUM motion
capture and are supplied to fusion; this path does not estimate camera motion.

The C++ reader unprojects raw depth into metres, derives camera-facing normals
from local depth differences, transforms observations using the supplied poses,
and passes them through the existing index-map shader, adapted fusion shader,
weighted C++ merge and overlap removal. It does not load a
previous reconstruction. Zero depth is missing. The reader keeps 0.5–3.5 m depth;
normals need four valid axial neighbours with depth differences no greater than
0.05 m. Border and depth-edge samples without such normals are omitted.

## Run the prepared recording

From the repository root, build the [Linux target](README.md#build-and-use) and
select software Mesa as shown there, then run:

```sh
build/offline/points_fusion \
  --recording updated/data/tum-fr1-xyz-120.pfrgbd \
  --export-inputs --output fusion-output-rgbd
```

The pack and its [source manifest](data/tum-fr1-xyz-120.json) are included, so this
command needs no dataset download. Output includes `native-state.json`, one row
per processed frame in `frames.jsonl`, `first-input.ply`, `fused.ply` and, with
`--export-inputs`, the unprojected input PLY for every frame under
`inputs/input-NNNN.ply` (zero-based four-digit frame numbers). Use a fresh output
directory: the evaluator rejects extra or missing numbered inputs. All
coordinates are metres. These are point clouds, not triangle meshes.

The existing 0.012 point radius and 0.055 correspondence-distance gate now mean
12 mm and 55 mm. The angle gate remains 20 degrees. Source-relative capture time,
computed by subtracting double-precision timestamps before float conversion,
drives the retained six-second low-confidence age rule. Those choices are not a
calibrated sensor-noise model. Geometry and normals are weighted on a merge;
colour and radius keep their first-inserted values, as in the existing adapter.

The real replay exposed CPU/GPU rounding differences at the matching thresholds.
The angle cutoff is now calculated once on the CPU and uploaded as a float32
uniform, retaining the strict 20-degree comparison. On an ID disagreement, the
verifier captures one query from the same Fusion vertex program, with invariant
outputs, checks its transform/normal against bounded FP32 error relative to a
double reference, and requires the exact CPU selection using that query. It
does not widen either matching gate. Rechecks have separate counters and logs;
other disagreements stop the run. This check still uses the GPU index map and
is not an independent implementation of the complete pipeline.

## Native build instructions

Add `--recording updated/data/tum-fr1-xyz-120.pfrgbd` to the Mac/Android build
scripts or the iOS project generator in [NATIVE.md](NATIVE.md). They bundle the
pack, its JSON manifest and dataset notice for on-device unprojection, fusion
and display of the input and accumulated model. Builds without this option retain the
generated example; rebuilding also removes a stale recording from resources.

For Linux's native window, first build the native target and select its X11
environment as in [NATIVE.md](NATIVE.md#linux-build), including `EGL_PLATFORM=x11`
and an available display. Then pass the path at launch:

```sh
build/native-linux/points_fusion_ui \
  --recording updated/data/tum-fr1-xyz-120.pfrgbd \
  --output fusion-output-rgbd-native
```

All four native apps completed 120 frames with 1,460,277 accepted observations:

| Actual execution | Final points | Merges | Strict matches / GPU-query rechecks | Movie |
| --- | ---: | ---: | ---: | --- |
| iPhone 12, iOS 18.0.1, A14 | 94,847 | 1,133,815 | 1,460,276 / 1 | [iOS](assets/ios-real-rgbd.mp4) |
| Pixel 2, Android 10, Adreno 540 | 94,842 | 1,133,815 | 1,460,277 / 0 | [Android](assets/android-real-rgbd.mp4) |
| Mac M3 Max, macOS 15.1.1, OpenGL 4.1 | 94,847 | 1,133,813 | 1,460,276 / 1 | [Mac](assets/macos-real-rgbd.mp4) |
| Linux X11/Xvfb, Mesa llvmpipe | 94,842 | 1,133,826 | 1,460,277 / 0 | [Linux](assets/linux-real-rgbd.mp4) |

The [runtime record](validation/real-rgbd/runtime-validation.json) binds source,
input, binaries, counters and output hashes. Mac/iOS rechecks require the exact
CPU winner from a separately bounded measured GPU query; they are not direct
CPU-transform matches. Counts retain platform differences. The clips show
excerpts of actual app execution, including the completed state. The iOS USB
stream's timestamps differ from wall time; no clip is a throughput benchmark.
Changing the display orbit does not change acquisition poses or fusion coordinates.

## Measured checks and limits

The Linux Mesa replay processed 1,460,277 accepted observations. All
1,460,277 rasterized fusion selections agreed with the CPU selection using the
read-back GPU index map. That checks fusion selection, rather than independently
validating index rasterization or deletion decisions. The run made 1,133,826
merges and retained 94,842 points after overlap removal and age expiry.

The state field `valid_depth_pixels_total` counts in-range, nonzero depth before
normal filtering; it excludes the separately counted range rejections.

A separate check projects the final cloud into 12 sensor frames excluded from
reconstruction. The [held-out manifest](data/tum-fr1-xyz-holdout-12.json) retains
the same original camera anchor; its timestamps are disjoint from the 120-frame
input. These frames are interleaved with nearby reconstruction viewpoints;
this measures near-viewpoint consistency, not novel-view prediction. A raw-union
baseline concatenates the actual native-unprojected input positions before
fusion, after the depth and normal filters described above. The evaluator uses
the point centre nearest the camera among those rounding into each 160 × 120
sensor pixel, without display splats or hole filling:

| Held-out depth agreement | Fused cloud | Raw union |
| --- | ---: | ---: |
| Points | 94,842 | 1,460,277 |
| Valid sensor pixels with a projected point | 87.13% | 99.66% |
| Mean absolute depth residual, overlapping pixels | 41.20 mm | 50.68 mm |
| Depth residual RMSE, overlapping pixels | 147.15 mm | 147.21 mm |
| Within 50 mm, fraction of all valid sensor pixels | 76.14% | 80.70% |

Each cloud's residuals use its own overlapping pixels. The fused cloud's lower
mean error may reflect an easier subset; the comparison over all valid pixels
(within 50 mm) favours the raw union. Point-centre sampling and age expiry can
affect coverage, and these percentages do not measure displayed splat coverage.
Both RMSE values show a heavy residual tail; sparse foreground gaps can expose
background points in this evaluator. The mixed results do not establish overall
accuracy or denoising improvement. This is agreement with noisy depth from the same sensor
and scene, using supplied poses and calibration. There is no independent surface
scan, tracking result, general correctness criterion or accuracy pass threshold.

Four CTests pass 84 assertions in regular, ASan/UBSan and integer-framebuffer
configurations: 25 earlier checks, 45 calibrated-input/CPU checks and 14 new GPU
and shared-loop checks. The full 120-frame Linux replay also passes ASan/UBSan
with halt-on-error, leak detection off and uninstrumented Mesa/shader code.
Conversion has 14 constructed-input Python tests, including units, colours,
sampling, pose interpolation, timing rejection, malformed inputs and subset
anchoring. Additional C++ CPU/GPU tests exercise calibrated pixels and z-buffer
visibility, real weighted merging versus concatenation, and recorded-time age
expiry. Six evaluator controls cover analytic off-axis projection under a rigid
pose, nearest-depth visibility and missing coverage, range limits and metre
units, binary depth/pose decoding, input concatenation and missing-frame
rejection, and rejection of mislabelled PLY coordinates. Test
counts are constructed cases, not additional recordings. See the exact logs in
the validation folder.

## Reproduce preparation and depth evaluation

The [format guide](RGBD_FORMAT.md) gives the archive hash, complete conversion
commands, pixel-centre and coordinate conventions, timestamp rules and binary
layout. Depth uses raw TUM values divided by 5000 to obtain metres, without
applying the dataset correction again. RGB and depth are associated by timestamp;
they are not simultaneous measurements. The spatially registered depth is not
undistorted again. Pose gaps reject source frames 202–204, counted one-based;
120 of the remaining 795 valid triples are selected across the full interval.

Use Python 3.10+, NumPy and Pillow for conversion, tests and saved-output tools.
The evaluator concatenates the per-frame PLYs written by `--export-inputs` and
checks that held-out pack timestamps are disjoint from `--fusion-recording`,
calibrations match and there is one input PLY per fusion frame. It hashes every
file, but does not establish that the fused PLY, input PLYs and fusion pack came
from one native run. PLY input must be
ASCII, vertex-only, with x/y/z as the first three properties:

```sh
python3 updated/tools/evaluate_rgbd.py \
  --recording updated/data/tum-fr1-xyz-holdout-12.pfrgbd \
  --fusion-recording updated/data/tum-fr1-xyz-120.pfrgbd \
  --fused fusion-output-rgbd/fused.ply \
  --input-directory fusion-output-rgbd/inputs \
  --output fusion-output-rgbd/heldout-depth-agreement.json
python3 -m unittest discover -s updated/tests -p 'test_*rgbd.py' -v
```

The test command runs 20 cases: 14 converter tests and six evaluator controls.
The original source and root project reference remain unchanged. The new reader,
converter, replay frontends and evaluation are maintenance additions.

## Data credit

Jürgen Sturm, Nikolas Engelhard, Felix Endres, Wolfram Burgard and Daniel Cremers,
*A Benchmark for the Evaluation of RGB-D SLAM Systems*, IROS 2012. TUM provides
the [dataset][tum], [format/calibration notes][formats] and [CC BY 4.0 terms][terms].
The included [notice](data/NOTICE.txt) identifies selection, pixel sampling,
coordinate conversion and packaging. Dataset terms remain separate from the
recovered code's source notices.

[openfusion]: https://github.com/twerdster/openfusion/blob/main/docs/REAL_DATA.md
[tum]: https://cvg.cit.tum.de/data/datasets/rgbd-dataset
[formats]: https://cvg.cit.tum.de/data/datasets/rgbd-dataset/file_formats
[terms]: https://cvg.cit.tum.de/data/datasets/rgbd-dataset#license
