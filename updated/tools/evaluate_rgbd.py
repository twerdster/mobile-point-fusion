#!/usr/bin/env python3
"""Compare saved point clouds with held-out measured depth, using supplied poses.

This is point-centre depth agreement on the sensor grid, not surface ground truth
or a tracking benchmark. It does not rerun or implement the fusion operators.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np


def read_pack(path):
    data = Path(path).read_bytes()
    if len(data) < 40 or data[:8] != b"PFRGBD1\0":
        raise ValueError("invalid PFRGBD1 pack")
    width, height, count, scale, fx, fy, cx, cy = struct.unpack_from("<III5f", data, 8)
    if not (width >= 3 and height >= 3 and count > 0 and scale > 0 and fx > 0 and fy > 0):
        raise ValueError("invalid pack dimensions/calibration")
    if not np.isfinite([scale, fx, fy, cx, cy]).all():
        raise ValueError("nonfinite calibration")
    stride = 72 + width * height * 5
    if len(data) != 40 + count * stride:
        raise ValueError("pack length mismatch")
    frames = []
    for i in range(count):
        offset = 40 + i * stride
        stamp = struct.unpack_from("<d", data, offset)[0]
        pose = np.frombuffer(data, dtype="<f4", count=16, offset=offset + 8).reshape(4, 4, order="F").astype(float)
        depth = np.frombuffer(data, dtype="<u2", count=width * height, offset=offset + 72).reshape(height, width).astype(float) / scale
        if not np.isfinite(pose).all() or not np.isfinite(stamp):
            raise ValueError("nonfinite pose/timestamp")
        frames.append((stamp, pose, depth))
    return (width, height, fx, fy, cx, cy), frames


def read_ply(path):
    """Read the adapter's ASCII, vertex-only PLY with x/y/z first."""
    with Path(path).open() as source:
        if source.readline().strip() != "ply" or source.readline().strip() != "format ascii 1.0":
            raise ValueError("expected ASCII PLY 1.0")
        count = None
        properties = []
        for line in source:
            if line.startswith("element "):
                fields = line.split()
                if fields[1] != "vertex" or count is not None:
                    raise ValueError("expected a single vertex element")
                count = int(fields[2])
            if line.startswith("property "):
                properties.append(line.split())
            if line.strip() == "end_header":
                break
        else:
            raise ValueError("missing PLY header")
        if count is None or count < 0 or [p[-1] for p in properties[:3]] != ["x", "y", "z"] or any(p[1] == "list" for p in properties):
            raise ValueError("expected vertex x/y/z followed by scalar properties")
        if count == 0:
            if source.read().strip():
                raise ValueError("data after empty PLY header")
            return np.empty((0, 3))
        points = np.loadtxt(source, usecols=(0, 1, 2), ndmin=2)
    if count is None or len(points) != count or points.shape[1] != 3 or not np.isfinite(points).all():
        raise ValueError("PLY count/position validation failed")
    return points


def read_input_directory(directory, expected_frames):
    """Concatenate the actual post-filter --export-inputs observations."""
    paths = sorted(Path(directory).glob("input-*.ply"))
    expected = [f"input-{i:04d}.ply" for i in range(expected_frames)]
    if [p.name for p in paths] != expected:
        raise ValueError("input directory must contain exactly one numbered PLY per fusion frame")
    points = np.concatenate([read_ply(p) for p in paths])
    provenance = dict(
        construction="Concatenated native --export-inputs PLY positions, after depth/normal filtering; no fusion",
        files=[dict(file=p.name, sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in paths],
        points=len(points),
    )
    return points, provenance


def project_depth(points, pose, calibration):
    width, height, fx, fy, cx, cy = calibration
    # The pack stores a rigid GL camera-to-world transform. Use a full inverse
    # here, independently of the runtime's transpose-based rigid inverse.
    view = np.linalg.inv(pose)
    camera = points @ view[:3, :3].T + view[:3, 3]
    z = -camera[:, 2]
    keep = np.isfinite(camera).all(axis=1) & (z > 0.1) & (z < 20)
    camera, z = camera[keep], z[keep]
    u = np.floor(fx * camera[:, 0] / z + cx + 0.5).astype(np.int64)
    v = np.floor(-fy * camera[:, 1] / z + cy + 0.5).astype(np.int64)
    keep = (u >= 0) & (u < width) & (v >= 0) & (v < height)
    predicted = np.full(width * height, np.inf)
    np.minimum.at(predicted, v[keep] * width + u[keep], z[keep])
    return predicted.reshape(height, width)


def depth_agreement(predicted, measured, minimum=0.5, maximum=3.5, threshold=0.05):
    valid = (measured >= minimum) & (measured <= maximum)
    overlap = valid & np.isfinite(predicted)
    residual = predicted[overlap] - measured[overlap]
    n, seen = int(valid.sum()), int(overlap.sum())
    return dict(
        valid_sensor_pixels=n,
        projected_sensor_pixels=seen,
        coverage=seen / n if n else None,
        within_threshold_pixels=int((np.abs(residual) <= threshold).sum()),
        absolute_residual_sum_m=float(np.abs(residual).sum()),
        squared_residual_sum_m2=float(np.square(residual).sum()),
        mean_absolute_depth_error_m=float(np.abs(residual).mean()) if seen else None,
        rmse_depth_m=float(np.sqrt(np.square(residual).mean())) if seen else None,
        median_signed_depth_error_m=float(np.median(residual)) if seen else None,
        p95_absolute_depth_error_m=float(np.percentile(np.abs(residual), 95)) if seen else None,
        nearer_than_sensor_threshold_pixels=int((residual < -threshold).sum()),
        farther_than_sensor_threshold_pixels=int((residual > threshold).sum()),
    )


def evaluate(points, calibration, frames):
    rows = [dict(frame=i, source_depth_timestamp=stamp, **depth_agreement(project_depth(points, pose, calibration), depth)) for i, (stamp, pose, depth) in enumerate(frames)]
    valid = sum(r["valid_sensor_pixels"] for r in rows)
    overlap = sum(r["projected_sensor_pixels"] for r in rows)
    summary = dict(
        points=len(points),
        frames=len(rows),
        valid_sensor_pixels=valid,
        projected_sensor_pixels=overlap,
        coverage=overlap / valid if valid else None,
        within_50mm_fraction_of_sensor_pixels=sum(r["within_threshold_pixels"] for r in rows) / valid if valid else None,
        mean_absolute_depth_error_m=sum(r["absolute_residual_sum_m"] for r in rows) / overlap if overlap else None,
        rmse_depth_m=float(np.sqrt(sum(r["squared_residual_sum_m2"] for r in rows) / overlap)) if overlap else None,
        nearer_than_50mm_fraction_of_projected_pixels=sum(r["nearer_than_sensor_threshold_pixels"] for r in rows) / overlap if overlap else None,
        farther_than_50mm_fraction_of_projected_pixels=sum(r["farther_than_sensor_threshold_pixels"] for r in rows) / overlap if overlap else None,
    )
    return dict(summary=summary, per_frame=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recording", type=Path, required=True, help="Held-out depth pack with the reconstruction's world anchor")
    parser.add_argument("--fusion-recording", type=Path, required=True, help="Pack used for fusion; verifies held-out timestamps are disjoint")
    parser.add_argument("--fused", type=Path, required=True)
    raw = parser.add_mutually_exclusive_group(required=True)
    raw.add_argument("--raw-union", type=Path, help="Previously concatenated ASCII vertex-only PLY; origin is not verified")
    raw.add_argument("--input-directory", type=Path, help="Native --export-inputs directory, one PLY per fusion frame")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    calibration, frames = read_pack(args.recording)
    fusion_calibration, fusion_frames = read_pack(args.fusion_recording)
    if calibration != fusion_calibration:
        raise ValueError("reconstruction and held-out calibration differ")
    if set(f[0] for f in frames) & set(f[0] for f in fusion_frames):
        raise ValueError("held-out depth timestamps overlap reconstruction input")
    inputs = {name: dict(file=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for name, path in [("heldout", args.recording), ("reconstruction", args.fusion_recording), ("fused", args.fused)]}
    if args.input_directory:
        raw_points, inputs["raw_union"] = read_input_directory(args.input_directory, len(fusion_frames))
    else:
        raw_points = read_ply(args.raw_union)
        inputs["raw_union"] = dict(file=args.raw_union.name, sha256=hashlib.sha256(args.raw_union.read_bytes()).hexdigest(), origin_verified=False)
    result = dict(
        scope="Held-out measured depth agreement with supplied motion-capture poses; no independent surface geometry or tracking evaluation",
        projection=f"Point centre nearest the camera among those rounding into each {calibration[0]}x{calibration[1]} sensor pixel; no splat/hole filling; measured-depth validity range 0.5–3.5 m, agreement threshold 0.05 m",
        limitations=["Same sensor, scene and calibration as reconstruction; interleaved nearby viewpoints, not novel-view prediction", "Depth noise, provided pose error and projection coverage affect these measurements", "Each cloud's residuals use its own overlapping pixel set; lower MAE need not mean better geometry", "Point-centre rendering differs from app display splats; sparse foreground gaps can reveal background", "Shared world anchor is checked during preparation, not established by the evaluator", "No accuracy pass threshold or benchmark claim"],
        inputs=inputs,
        heldout_timestamps_disjoint_from_reconstruction=True,
        calibration=dict(zip(["width", "height", "fx", "fy", "cx", "cy"], calibration)),
        fused=evaluate(read_ply(args.fused), calibration, frames),
        raw_union=evaluate(raw_points, calibration, frames),
    )
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    print(json.dumps({k: result[k]["summary"] for k in ["fused", "raw_union"]}, indent=2))


if __name__ == "__main__":
    main()
