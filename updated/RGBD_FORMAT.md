# Recorded RGB-D input format

`tools/prepare_tum_rgbd.py` packages measured images and supplied motion-capture
poses from TUM RGB-D Freiburg 1 xyz. The converter is scoped to this sequence:
its manifest dataset label, archive download URL and depth scale of 5000 units
per metre are fixed for Freiburg 1 xyz. It requires Python 3.10 or newer, NumPy
and Pillow, and an extracted Freiburg 1 xyz directory containing `depth.txt`,
`rgb.txt`, `groundtruth.txt`, and the referenced PNG files. It does not estimate
camera poses or execute fusion.

```sh
python3 updated/tools/prepare_tum_rgbd.py \
  /path/to/rgbd_dataset_freiburg1_xyz tum-fr1-xyz-120.pfrgbd \
  --frames 120 --pixel-stride 4 \
  --archive /path/to/rgbd_dataset_freiburg1_xyz.tgz
```

The optional archive argument records its bytes and SHA-256; the converter
reads images from the extracted directory. Output paths must be new. The
same-stem JSON records source-image hashes, timestamps, selection,
calibration, pose interpolation and attribution. Keep downloaded raw data and
newly converted packs outside source and build directories. The two checked
packs are deliberately included under `updated/data/`.

## Binary layout

All numbers are little-endian. Integers are unsigned; floating-point fields use
IEEE 754. There is no padding between fields.

| Header field | Representation |
| --- | --- |
| Magic | 8 bytes: `PFRGBD1` followed by NUL |
| Width, height, frame count | Three `uint32` values |
| Depth units per metre, fx, fy, cx, cy | Five `float32` values |

The header is 40 bytes. Each frame follows immediately:

| Frame field | Representation |
| --- | --- |
| Original depth timestamp | One `float64`, Unix seconds |
| Camera-to-world matrix | Sixteen `float32`, column-major |
| Depth image | Width × height `uint16` values |
| RGB image | Width × height × three `uint8` values, RGB channel order |

Images use row-major, top-left image order. Depth is camera-axis distance, not
range along a unit ray. Zero is missing depth. Divide raw depth by the header's
depth units per metre to obtain metres. No depth correction is applied again.
The frame prefix is 72 bytes; a 160 × 120 frame occupies 96,072 bytes.

## Sampling, coordinates and supplied poses

The default pixel stride of four samples native pixels `(4u, 4v)` exactly,
without interpolation or hole filling. For 640 × 480 input the packed image is
160 × 120. Default source intrinsics are 525 / 525 / 319.5 / 239.5; all four
values are divided by the stride, giving 131.25 / 131.25 / 79.875 / 59.875.
These are the TUM-recommended ROS default parameters for pre-registered depth,
without additional undistortion; they are not an exact lens-distortion model.

The packed camera basis is x right, y up, negative z forward. Integer `(u,v)`
coordinates denote pixel centers, following TUM's convention with `cx = 319.5`
at 640-pixel width. At top-left image pixel `(u,v)` and axial depth `d` in metres,
its point is:

```text
camera_point = ((u-cx)*d/fx, -(v-cy)*d/fy, -d)
world_point  = camera_to_world * (camera_point, 1)
```

Let `D = diag(1,-1,-1,1)`, `T0` be the TUM camera-to-world pose interpolated at
the first source depth timestamp, and `Ti` the pose interpolated at a frame's
depth timestamp. The packed matrix is `D * inverse(T0) * Ti * D`. World origin
is the first camera's optical center in its GL basis. Translations remain in
metres. The two sign changes form a proper rotation; no reflected coordinate
basis or arbitrary scale is introduced. Consumers invert the matrix for a
world-to-camera view transform.

Translation uses linear interpolation; normalized xyzw quaternions use shortest
path spherical interpolation. Both bracketing motion-capture samples must lie
within 20 ms of the depth timestamp, and no extrapolation is allowed. RGB uses
the nearest timestamp within 30 ms, choosing the earlier timestamp for a tie.
The manifest preserves the actual differences. Passing these timing limits
does not imply simultaneous RGB and depth exposures.

`--frames 120` selects rounded, evenly spaced indices among accepted
depth/RGB/pose triples, including the first and last accepted triple. Their
timestamps need not be uniformly spaced. `--frames 0` retains all accepted
triples. `--indices` instead accepts explicit increasing zero-based source
indices; it is mutually exclusive with `--frames`. This allows disjoint
reconstruction and held-out input packs. `--validation-only` marks the JSON
sidecar with an advisory instruction to keep that input out of fusion. The
binary carries no validation-only flag, and the binary reader does not enforce
the sidecar label. Neither flag changes `T0`: a subset starting later still uses
the pose at the first original depth timestamp. The checked 12-frame held-out
subset uses source indices outside the 120-frame reconstruction selection:

```sh
python3 updated/tools/prepare_tum_rgbd.py \
  /path/to/rgbd_dataset_freiburg1_xyz tum-fr1-xyz-holdout-12.pfrgbd \
  --indices 1 72 145 220 292 364 436 508 580 652 725 796 \
  --pixel-stride 4 --validation-only \
  --archive /path/to/rgbd_dataset_freiburg1_xyz.tgz
```

For the checked Freiburg 1 xyz archive, 795 of 798 frames have accepted
poses; source frames 202–204 (one-based) cross a 110.1 ms motion-capture gap and
are excluded. This is an input-pose exclusion, not a tracking failure.

The original archive checked for these subsets is 448,204,271 bytes, SHA-256
`a0236d97b8c30cd93b653656d2b6c293ff7c982a4130ef2a1a8beecdb124ef98`.
The 120-frame pack spans 26.594239 seconds of capture timestamps; the disjoint
held-out pack spans 26.528646 seconds inside that interval. These are capture
times, not measured processing rates. The held-out frames are interleaved with
the reconstruction frames and use nearby viewpoints. They measure consistency
at nearby viewpoints rather than novel-view prediction. The checked binaries
are:

| Pack | Bytes | SHA-256 |
| --- | ---: | --- |
| `tum-fr1-xyz-120.pfrgbd` | 11,528,680 | `1033e30ae101dea653b1be77aaa3972c8a026ca5a152a9bf479cee07a12ff39c` |
| `tum-fr1-xyz-holdout-12.pfrgbd` | 1,152,904 | `10aa619d7909209b9be3ddd7859a9d1bb1c50e01fb6d71daed8a5714b5e43b2a` |

The final converter reproduced the 120-frame pack byte for byte. Both packs
also passed comparisons of sampled source-image pixels, timestamps and supplied
pose coordinates; those comparisons were the recorded validation of the
12-frame held-out pack.

Keep each pack's same-stem JSON and `NOTICE.txt` with any bundled copy. Selection,
sampling and supplied-pose metadata belong with the data, rather than relying on
the binary filename to communicate its provenance or validation-only role.

The fusion pipeline receives these external poses. A reconstruction produced
with them does not demonstrate camera tracking or provide an independent
trajectory accuracy result. This sequence has no surface ground-truth scan;
rendered geometry alone does not establish surface accuracy or denoising.

Constructed-input conversion checks cover raw units/colors, sampling, supplied
translation/rotation, subset anchoring, timing rejections and malformed inputs:

```sh
python3 -m unittest discover -s updated/tests -p test_prepare_tum_rgbd.py -v
```

## Data source and terms

TUM data credit: Jürgen Sturm, Nikolas Engelhard, Felix Endres, Wolfram Burgard
and Daniel Cremers, *A Benchmark for the Evaluation of RGB-D SLAM Systems*,
IROS 2012. The dataset authors provide the [sequence downloads][downloads],
[format and calibration notes][formats], and [CC BY 4.0 terms][license] unless
otherwise stated. Selection, spatial sampling, coordinate conversion and binary
packaging are maintenance transformations; they are not dataset-author outputs.
Dataset terms are separate from the recovered software's source notices.

[downloads]: https://cvg.cit.tum.de/data/datasets/rgbd-dataset/download
[formats]: https://cvg.cit.tum.de/data/datasets/rgbd-dataset/file_formats
[license]: https://cvg.cit.tum.de/data/datasets/rgbd-dataset#license
