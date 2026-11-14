#!/usr/bin/env python3
"""Package TUM Freiburg 1 xyz depth/RGB/pose triples for point fusion.

Requires Python 3.10+, NumPy and Pillow. Input is an extracted Freiburg 1 xyz
directory containing depth.txt, rgb.txt and groundtruth.txt. The manifest's
dataset label, archive download URL and depth scale of 5000 units per metre
are fixed for that sequence. This performs format conversion and pose
association; it does not reconstruct or track. The validation-only label is
advisory JSON-sidecar metadata; the binary reader does not enforce it. See
RGBD_FORMAT.md for the little-endian binary contract and coordinates.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
from typing import Sequence

import numpy as np
from PIL import Image


MAGIC = b"PFRGBD1\0"
HEADER = struct.Struct("<8sIII5f")
FRAME_PREFIX = struct.Struct("<d16f")
D = np.diag([1.0, -1.0, -1.0, 1.0])
DEFAULT_INTRINSICS = (525.0, 525.0, 319.5, 239.5)
DATASET_URL = "https://cvg.cit.tum.de/data/datasets/rgbd-dataset"
FORMAT_URL = DATASET_URL + "/file_formats"
ARCHIVE_URL = ("https://webshare.cvg.cit.tum.de/g/rgbd/dataset/freiburg1/"
               "rgbd_dataset_freiburg1_xyz.tgz")


def sha256(path: Path) -> str:
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            result.update(chunk)
    return result.hexdigest()


def safe_path(root: Path, name: str) -> Path:
    """Dataset lists may reference only files within their own directory."""
    relative = Path(name)
    if relative.is_absolute() or ".." in relative.parts:
        raise ValueError("image path escapes the input sequence")
    path = (root / relative).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        raise ValueError("image path escapes the input sequence") from error
    if not path.is_file():
        raise ValueError(f"missing sequence file: {name}")
    return path


def load_index(path: Path) -> list[tuple[float, str, str]]:
    rows = []
    for number, text in enumerate(path.read_text().splitlines(), 1):
        fields = text.split("#", 1)[0].split()
        if not fields:
            continue
        if len(fields) != 2:
            raise ValueError(f"{path.name}:{number}: expected timestamp and path")
        timestamp = float(fields[0])
        if not np.isfinite(timestamp):
            raise ValueError(f"{path.name}:{number}: nonfinite timestamp")
        if rows and timestamp <= rows[-1][0]:
            raise ValueError(f"{path.name}: timestamps must strictly increase")
        rows.append((timestamp, fields[1], fields[0]))
    if not rows:
        raise ValueError(f"{path.name}: no image rows")
    return rows


def quaternion_matrix(quaternion: Sequence[float]) -> np.ndarray:
    """TUM xyzw quaternion -> proper camera-to-world rotation."""
    q = np.asarray(quaternion, dtype=np.float64)
    if q.shape != (4,) or not np.isfinite(q).all() or np.linalg.norm(q) < 1e-12:
        raise ValueError("invalid ground-truth quaternion")
    x, y, z, w = q / np.linalg.norm(q)
    return np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)],
    ])


def slerp(q0: np.ndarray, q1: np.ndarray, fraction: float) -> np.ndarray:
    q0, q1 = q0 / np.linalg.norm(q0), q1 / np.linalg.norm(q1)
    cosine = float(q0 @ q1)
    if cosine < 0:
        q1, cosine = -q1, -cosine
    cosine = float(np.clip(cosine, -1.0, 1.0))
    if cosine > 0.9995:
        q = q0 + fraction * (q1-q0)
        return q / np.linalg.norm(q)
    angle = np.arccos(cosine)
    return (np.sin((1-fraction)*angle)*q0 + np.sin(fraction*angle)*q1) / np.sin(angle)


def load_ground_truth(path: Path) -> np.ndarray:
    rows = []
    for number, text in enumerate(path.read_text().splitlines(), 1):
        fields = text.split("#", 1)[0].split()
        if not fields:
            continue
        row = np.asarray([float(x) for x in fields])
        if row.shape != (8,) or not np.isfinite(row).all():
            raise ValueError(f"{path.name}:{number}: expected eight finite values")
        quaternion_matrix(row[4:8])
        if rows and row[0] <= rows[-1][0]:
            raise ValueError("ground-truth timestamps must strictly increase")
        rows.append(row)
    if not rows:
        raise ValueError("no ground-truth poses")
    return np.asarray(rows)


def associate_pose(rows: np.ndarray, timestamp: float,
                   max_delta: float = 0.02) -> tuple[np.ndarray | None, dict]:
    """Interpolate translations/xyzw rotations; reject gaps and extrapolation."""
    index = int(np.searchsorted(rows[:, 0], timestamp))
    adjacent = [j for j in (index-1, index) if 0 <= j < len(rows)]
    nearest = min(adjacent, key=lambda j: abs(rows[j, 0]-timestamp))
    evidence = {"depth_timestamp": float(timestamp),
                "nearest_timestamp": float(rows[nearest, 0]),
                "nearest_delta_seconds": float(abs(rows[nearest, 0]-timestamp))}
    if rows[nearest, 0] == timestamp:
        lo = hi = rows[nearest]
        fraction = 0.0
    elif index == 0 or index == len(rows):
        return None, dict(evidence, accepted=False, reason="outside pose interval; no extrapolation")
    else:
        lo, hi = rows[index-1], rows[index]
        fraction = (timestamp-lo[0]) / (hi[0]-lo[0])
    low_delta, high_delta = float(timestamp-lo[0]), float(hi[0]-timestamp)
    evidence.update(lower_timestamp=float(lo[0]), upper_timestamp=float(hi[0]),
                    lower_delta_seconds=low_delta, upper_delta_seconds=high_delta)
    if max(low_delta, high_delta) > max_delta:
        return None, dict(evidence, accepted=False, reason="pose interpolation endpoint exceeds tolerance")
    pose = np.eye(4)
    pose[:3, 3] = lo[1:4]*(1-fraction) + hi[1:4]*fraction
    pose[:3, :3] = quaternion_matrix(slerp(lo[4:8], hi[4:8], float(fraction)))
    return pose, dict(evidence, accepted=True, interpolation_fraction=float(fraction))


def nearest_rgb(rows: list[tuple[float, str, str]], timestamp: float,
                max_delta: float) -> tuple[tuple[float, str, str] | None, float]:
    times = np.asarray([row[0] for row in rows])
    index = int(np.searchsorted(times, timestamp))
    candidates = [i for i in (index-1, index) if 0 <= i < len(rows)]
    selected = min(candidates, key=lambda i: (abs(times[i]-timestamp), times[i]))
    delta = float(times[selected]-timestamp)
    return (rows[selected] if abs(delta) <= max_delta else None), delta


def generate(input_directory: Path, output_pack: Path, frames: int = 120,
             pixel_stride: int = 4, max_pose_delta: float = 0.02,
             max_rgb_delta: float = 0.03,
             intrinsics: Sequence[float] = DEFAULT_INTRINSICS,
             archive_path: Path | None = None,
             source_indices: Sequence[int] | None = None,
             validation_only: bool = False) -> dict:
    """Write a new pack and same-stem JSON sidecar; never overwrite either.

    Selection is evenly spaced over accepted depth/RGB/pose triple indices, not
    uniformly spaced timestamps. All selected frames preserve raw depth values.
    Set frames=0 to retain every accepted triple. Explicit source_indices override
    frames and must be increasing accepted source indices. The coordinate anchor
    remains the first ORIGINAL depth timestamp, also for held-out subsets.
    Returned value is the manifest.
    """
    root, output_pack = Path(input_directory), Path(output_pack)
    manifest_path = output_pack.with_suffix(".json")
    if output_pack.suffix.lower() == ".json":
        raise ValueError("binary output must use a non-JSON extension")
    if output_pack.exists() or manifest_path.exists():
        raise ValueError("output pack or manifest already exists")
    if not root.is_dir():
        raise ValueError("input must be an extracted TUM sequence directory")
    if not isinstance(pixel_stride, int) or pixel_stride < 1 or frames < 0:
        raise ValueError("pixel stride must be positive; frame count nonnegative")
    if not np.isfinite([max_pose_delta, max_rgb_delta]).all() or min(max_pose_delta, max_rgb_delta) <= 0:
        raise ValueError("timestamp tolerances must be finite and positive")
    k = np.asarray(intrinsics, dtype=float)
    if k.shape != (4,) or not np.isfinite(k).all() or min(k[:2]) <= 0:
        raise ValueError("intrinsics require positive finite fx/fy and finite cx/cy")
    depth = load_index(root / "depth.txt")
    rgb = load_index(root / "rgb.txt")
    gt = load_ground_truth(root / "groundtruth.txt")
    first_pose, anchor_evidence = associate_pose(gt, depth[0][0], max_pose_delta)
    if first_pose is None:
        raise ValueError("first depth timestamp has no usable pose anchor")
    inverse_first = np.linalg.inv(first_pose)
    accepted, rejected = [], []
    for index, row in enumerate(depth):
        pose, pose_evidence = associate_pose(gt, row[0], max_pose_delta)
        color, delta = nearest_rgb(rgb, row[0], max_rgb_delta)
        if pose is None or color is None:
            rejected.append({"source_index": index, "source_frame_1based": index+1,
                             "depth_timestamp": row[0], "pose": pose_evidence,
                             "rgb_accepted": color is not None, "rgb_delta_seconds": delta})
        else:
            accepted.append((index, row, pose, pose_evidence, color, delta))
    if source_indices is None:
        count = len(accepted) if frames == 0 else frames
        if count < 1 or count > len(accepted):
            raise ValueError(f"requested {count} frames, but {len(accepted)} usable pairs exist")
        selection_offsets = np.rint(np.linspace(0, len(accepted)-1, count)).astype(int)
        selected = [accepted[i] for i in selection_offsets]
        if len(set(selection_offsets.tolist())) != count:
            raise ValueError("frame selection contains duplicate indices")
        selection_method = "rounded linspace over accepted pair indices; includes first and last accepted pair when frames>1"
    else:
        indices = list(source_indices)
        if not indices or any(not isinstance(x, (int, np.integer)) or x < 0 for x in indices):
            raise ValueError("explicit source indices must be nonempty nonnegative integers")
        if any(a >= b for a, b in zip(indices, indices[1:])):
            raise ValueError("explicit source indices must strictly increase")
        by_index = {item[0]: item for item in accepted}
        if any(x not in by_index for x in indices):
            raise ValueError("explicit source index has no accepted depth/RGB/pose pair")
        selected = [by_index[x] for x in indices]
        count = len(selected)
        selection_method = "explicit zero-based source indices; first original depth timestamp remains the global pose anchor"
    first_depth_path = safe_path(root, selected[0][1][1])
    with Image.open(first_depth_path) as image:
        source_width, source_height = image.size
    if source_width % pixel_stride or source_height % pixel_stride:
        raise ValueError("source dimensions must be divisible by pixel stride")
    width, height = source_width // pixel_stride, source_height // pixel_stride
    if width < 3 or height < 3:
        raise ValueError("sampled image must be at least 3 by 3")
    sampled_k = k / pixel_stride
    manifest = {
        "schema_version": 1, "binary_format": "PFRGBD1", "byte_order": "little-endian",
        "dataset": "TUM RGB-D Freiburg 1 xyz", "input_kind": "real recorded RGB/depth; external motion-capture poses",
        "dataset_url": DATASET_URL, "format_source": FORMAT_URL,
        "dataset_attribution": {
            "authors": ["Jürgen Sturm", "Nikolas Engelhard", "Felix Endres", "Wolfram Burgard", "Daniel Cremers"],
            "publication": "A Benchmark for the Evaluation of RGB-D SLAM Systems, IROS 2012",
            "license": "CC BY 4.0 unless stated otherwise", "license_source": DATASET_URL + "#license",
            "changes": "selected frames; exact pixel-stride sampling; nearest RGB association; interpolated, first-camera-anchored GL pose coordinates; binary packaging",
        },
        "source": {"directory_name": root.name,
                   "index_sha256": {name: sha256(root / name) for name in ("depth.txt", "rgb.txt", "groundtruth.txt")}},
        "selection": {"method": selection_method,
                      "source_depth_frames": len(depth), "accepted_pairs": len(accepted),
                      "selected_frames": count, "pixel_stride": pixel_stride,
                      "spatial_resampling": "raw[v*stride,u*stride], top-left row order; no interpolation",
                      "max_pose_endpoint_seconds": max_pose_delta, "max_rgb_delta_seconds": max_rgb_delta,
                      "rejected_pairs": rejected},
        "image": {"width": width, "height": height, "source_width": source_width, "source_height": source_height,
                  "fx": float(sampled_k[0]), "fy": float(sampled_k[1]), "cx": float(sampled_k[2]), "cy": float(sampled_k[3]),
                  "source_intrinsics": k.tolist(), "depth_units_per_metre": 5000.0, "invalid_depth": 0,
                  "additional_depth_correction": False, "undistortion": False,
                  "calibration_qualification": "ROS default pinhole intrinsics recommended by TUM for pre-registered depth; not an exact distortion model"},
        "coordinates": {"length_units": "metres", "camera": "x right, y up, negative z forward",
                        "world": "initial depth camera GL basis; origin at its optical center",
                        "D_row_major": D.ravel().tolist(),
                        "camera_to_world_formula": "D @ inverse(TUM_pose_at_first_depth_timestamp) @ TUM_pose_at_frame_timestamp @ D",
                        "pose_storage": "16 IEEE754 float32, column-major camera-to-world; original timestamp float64",
                        "anchor": anchor_evidence},
        "pose_usage": {"external_motion_capture_used_by_fusion": True, "pose_estimation": False,
                       "qualification": "Supplied poses position the observations; no independent trajectory accuracy or surface-ground-truth claim."},
        "usage": {"validation_only": bool(validation_only),
                  "role": "held-out measured-depth consistency input; do not fuse" if validation_only else "reconstruction input with externally supplied poses"},
        "frames": [],
    }
    if archive_path is not None:
        archive_path = Path(archive_path)
        manifest["source"]["archive"] = {"filename": archive_path.name, "bytes": archive_path.stat().st_size,
                                           "sha256": sha256(archive_path), "download_url": ARCHIVE_URL}
    output_pack.parent.mkdir(parents=True, exist_ok=True)
    created_pack = created_manifest = False
    try:
        with output_pack.open("xb") as stream:
            created_pack = True
            stream.write(HEADER.pack(MAGIC, width, height, count, 5000.0, *sampled_k))
            for ordinal, (index, row, pose, pose_evidence, color, delta) in enumerate(selected):
                depth_path, rgb_path = safe_path(root, row[1]), safe_path(root, color[1])
                with Image.open(depth_path) as image:
                    array = np.asarray(image)
                    if image.format != "PNG" or array.ndim != 2 or array.dtype != np.uint16:
                        raise ValueError("depth must be unsigned 16-bit monochrome PNG")
                    if image.size != (source_width, source_height):
                        raise ValueError("inconsistent depth image dimensions")
                    sampled_depth = np.ascontiguousarray(array[::pixel_stride, ::pixel_stride], dtype="<u2")
                with Image.open(rgb_path) as image:
                    if image.format != "PNG" or image.size != (source_width, source_height):
                        raise ValueError("RGB must be a PNG with the source depth dimensions")
                    sampled_rgb = np.ascontiguousarray(np.asarray(image.convert("RGB"))[::pixel_stride, ::pixel_stride])
                camera_to_world = D @ inverse_first @ pose @ D
                stored_pose = camera_to_world.astype("<f4").ravel(order="F")
                stream.write(FRAME_PREFIX.pack(row[0], *stored_pose))
                depth_bytes, rgb_bytes = sampled_depth.tobytes(), sampled_rgb.tobytes()
                stream.write(depth_bytes)
                stream.write(rgb_bytes)
                manifest["frames"].append({
                    "frame": ordinal, "source_index": index, "source_frame_1based": index+1,
                    "depth_timestamp": row[0], "depth_timestamp_text": row[2],
                    "source_depth_path": row[1], "source_depth_png_sha256": sha256(depth_path),
                    "sampled_depth_sha256": hashlib.sha256(depth_bytes).hexdigest(),
                    "nonzero_sampled_depth_pixels": int(np.count_nonzero(sampled_depth)),
                    "rgb_timestamp": color[0], "rgb_timestamp_text": color[2], "rgb_delta_seconds": delta,
                    "source_rgb_path": color[1], "source_rgb_png_sha256": sha256(rgb_path),
                    "sampled_rgb_sha256": hashlib.sha256(rgb_bytes).hexdigest(),
                    "pose_association": pose_evidence,
                    "camera_to_world_column_major": stored_pose.tolist(),
                })
            stream.flush()
        manifest["binary"] = {"filename": output_pack.name, "bytes": output_pack.stat().st_size,
                              "sha256": sha256(output_pack), "header_bytes": HEADER.size,
                              "frame_prefix_bytes": FRAME_PREFIX.size,
                              "frame_bytes": FRAME_PREFIX.size + width*height*5}
        manifest["selection"]["capture_timestamp_span_seconds"] = float(selected[-1][1][0]-selected[0][1][0])
        manifest["selection"]["selected_source_indices"] = [row[0] for row in selected]
        with manifest_path.open("x") as stream:
            created_manifest = True
            stream.write(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    except Exception:
        if created_pack:
            output_pack.unlink(missing_ok=True)
        if created_manifest:
            manifest_path.unlink(missing_ok=True)
        raise
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_directory", type=Path)
    parser.add_argument("output_pack", type=Path)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument("--frames", type=int, default=120, help="evenly selected accepted pairs; 0=all")
    selection.add_argument("--indices", type=int, nargs="+", help="explicit increasing zero-based source indices; same original-first-depth pose anchor")
    parser.add_argument("--pixel-stride", type=int, default=4)
    parser.add_argument("--max-pose-delta", type=float, default=0.02)
    parser.add_argument("--max-rgb-delta", type=float, default=0.03)
    parser.add_argument("--archive", type=Path, help="optional original TGZ for provenance hashing")
    parser.add_argument("--validation-only", action="store_true", help="mark a held-out pack as validation input that must not be fused")
    args = parser.parse_args()
    result = generate(args.input_directory, args.output_pack, frames=args.frames,
                      pixel_stride=args.pixel_stride, max_pose_delta=args.max_pose_delta,
                      max_rgb_delta=args.max_rgb_delta, archive_path=args.archive,
                      source_indices=args.indices, validation_only=args.validation_only)
    print(json.dumps({"binary": result["binary"], "image": result["image"],
                      "frames": result["selection"]["selected_frames"],
                      "rejected_pairs": len(result["selection"]["rejected_pairs"]),
                      "capture_timestamp_span_seconds": result["selection"]["capture_timestamp_span_seconds"]}, indent=2))


if __name__ == "__main__":
    main()
