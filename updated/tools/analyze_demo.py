#!/usr/bin/env python3
"""Compare saved points to finite generated reference samples, in model units.

This does not evaluate pose tracking, a real sensor, or a continuous mesh.
Requires NumPy; computes exact nearest-sample distances in bounded batches.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np


def read_ply(path):
    with path.open() as f:
        for line in f:
            if line.strip() == "end_header":
                break
        return np.loadtxt(f, ndmin=2)[:, :3]


def nearest(a, b):
    result = []
    for start in range(0, len(a), 256):
        q = a[start:start+256]
        distance2 = (q*q).sum(1)[:, None] + (b*b).sum(1)[None, :] - 2*q@b.T
        result.extend(np.sqrt(np.maximum(distance2.min(1), 0)).tolist())
    return np.asarray(result)


def stats(d):
    return {"rmse": float(np.sqrt(np.mean(d*d))), "median": float(np.median(d)),
            "p95": float(np.quantile(d, .95)), "maximum": float(d.max()), "samples": len(d)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("directory", type=Path)
    args = p.parse_args()
    root = args.directory
    reference = read_ply(root/"reference.ply")
    fused = read_ply(root/"fused.ply")
    first = read_ply(root/"frames/000-input.ply")
    # Restrict to numbered data files; exFAT may contain AppleDouble ._ sidecars.
    raw_union=np.concatenate([read_ply(p) for p in sorted((root/'frames').glob('[0-9][0-9][0-9]-input.ply'))])
    out = {
        "scope": "exact nearest finite generated-reference point; no continuous surface or real sensor ground truth",
        "units": "arbitrary model units",
        "reference_samples": len(reference), "final_fused_samples": len(fused),
        "reference_definition": "all sampled ellipsoid primitives, including their hidden intersections; not a watertight external surface",
        "final_fused_to_reference": stats(nearest(fused, reference)),
        "first_observation_to_reference": stats(nearest(first, reference)),
        "all_raw_observations_to_reference": stats(nearest(raw_union, reference)),
        "reference_coverage_radius_model_units": .02,
        "reference_coverage_by_first_observation": float(np.mean(nearest(reference, first) <= .02)),
        "reference_coverage_by_final_fused_model": float(np.mean(nearest(reference, fused) <= .02)),
        "reference_coverage_by_all_raw_observations": float(np.mean(nearest(reference, raw_union) <= .02)),
        "raw_observations_samples":len(raw_union),
        "accuracy_pass_threshold": None,
        "files": {n: hashlib.sha256((root/n).read_bytes()).hexdigest()
                  for n in ("reference.ply", "fused.ply", "frames/000-input.ply", "metrics.json")},
    }
    (root/"geometry-evaluation.json").write_text(json.dumps(out, indent=2)+"\n")
    print(json.dumps(out, indent=2))


if __name__ == "__main__":
    main()
