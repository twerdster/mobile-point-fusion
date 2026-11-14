"""Constructed-input conversion regressions; no fusion or real-camera accuracy."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np
from PIL import Image


MODULE = Path(__file__).resolve().parents[1] / "tools" / "prepare_tum_rgbd.py"
SPEC = importlib.util.spec_from_file_location("prepare_tum_rgbd", MODULE)
converter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(converter)


class ConversionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.source = self.base / "source"
        (self.source / "depth").mkdir(parents=True)
        (self.source / "rgb").mkdir()
        self.depth = (np.arange(36).reshape(6, 6) + 5000).astype(np.uint16)
        self.depth[0, 0] = 0
        self.color = np.arange(108, dtype=np.uint8).reshape(6, 6, 3)
        depth_rows, rgb_rows = [], []
        for i in range(5):
            time = 10.005 + i * .02
            Image.fromarray(self.depth).save(self.source / f"depth/{i}.png")
            Image.fromarray(self.color).save(self.source / f"rgb/{i}.png")
            depth_rows.append(f"{time:.6f} depth/{i}.png\n")
            rgb_rows.append(f"{time+.005:.6f} rgb/{i}.png\n")
        (self.source / "depth.txt").write_text("# depth\n" + "".join(depth_rows))
        (self.source / "rgb.txt").write_text("".join(rgb_rows))
        # Known 1 m/s translation along source camera x; no rotation.
        (self.source / "groundtruth.txt").write_text("".join(
            f"{10+i*.01:.6f} {i*.01:.6f} 0 0 0 0 0 1\n" for i in range(11)))
        self.output = self.base / "recording.pfrgbd"

    def generate(self, **kwargs):
        options = dict(frames=3, pixel_stride=2, intrinsics=(4., 4., 2.5, 2.5))
        options.update(kwargs)
        return converter.generate(self.source, self.output, **options)

    def decode(self):
        data = self.output.read_bytes()
        magic, width, height, count, scale, fx, fy, cx, cy = struct.unpack_from("<8sIII5f", data)
        offset, frames = 40, []
        for _ in range(count):
            timestamp, *pose = struct.unpack_from("<d16f", data, offset)
            offset += 72
            depth = np.frombuffer(data, "<u2", width*height, offset).reshape(height, width)
            offset += width*height*2
            rgb = np.frombuffer(data, "u1", width*height*3, offset).reshape(height, width, 3)
            offset += width*height*3
            frames.append((timestamp, np.asarray(pose).reshape(4, 4, order="F"), depth, rgb))
        self.assertEqual(offset, len(data))
        return (magic, width, height, count, scale, fx, fy, cx, cy), frames

    def test_binary_sampling_units_colors_and_frame_selection(self):
        manifest = self.generate()
        header, frames = self.decode()
        self.assertEqual(header, (b"PFRGBD1\0", 3, 3, 3, 5000., 2., 2., 1.25, 1.25))
        self.assertEqual(manifest["selection"]["selected_source_indices"], [0, 2, 4])
        for i, (_, _, depth, rgb) in enumerate(frames):
            np.testing.assert_array_equal(depth, self.depth[::2, ::2])
            np.testing.assert_array_equal(rgb, self.color[::2, ::2])
            self.assertEqual(int(depth[0, 0]), 0)
            self.assertAlmostEqual(float(depth[1, 1]) / header[4], 1.0028)
            self.assertAlmostEqual(frames[i][0], 10.005 + i*.04)
        self.assertFalse(manifest["image"]["additional_depth_correction"])
        self.assertEqual(self.output.stat().st_size, 40 + 3*(72+9*5))

    def test_known_translation_and_first_depth_anchor(self):
        self.generate()
        _, frames = self.decode()
        np.testing.assert_allclose(frames[0][1], np.eye(4), atol=1e-7)
        np.testing.assert_allclose(frames[1][1][:3, 3], [.04, 0, 0], atol=1e-7)
        np.testing.assert_allclose(frames[2][1][:3, 3], [.08, 0, 0], atol=1e-7)

    def test_heldout_explicit_indices_keep_original_anchor(self):
        manifest = self.generate(source_indices=[1, 3], validation_only=True)
        _, frames = self.decode()
        self.assertEqual(manifest["selection"]["selected_source_indices"], [1, 3])
        self.assertTrue(manifest["usage"]["validation_only"])
        self.assertAlmostEqual(manifest["coordinates"]["anchor"]["depth_timestamp"], 10.005)
        np.testing.assert_allclose(frames[0][1][:3, 3], [.02, 0, 0], atol=1e-7)
        np.testing.assert_allclose(frames[1][1][:3, 3], [.06, 0, 0], atol=1e-7)

    def test_spherical_rotation_interpolation_and_antipodal_quaternions(self):
        # At halfway between identity and a 90-degree z rotation, transform x
        # to (sqrt(.5),sqrt(.5),0). Antipodal quaternion signs must not change it.
        rows = np.array([[1., 0, 0, 0, 0, 0, 0, 1],
                         [1.02, 0, 0, 0, 0, 0, -np.sqrt(.5), -np.sqrt(.5)]])
        pose, evidence = converter.associate_pose(rows, 1.01, .02)
        self.assertTrue(evidence["accepted"])
        np.testing.assert_allclose(pose[:3, :3] @ [1, 0, 0],
                                   [np.sqrt(.5), np.sqrt(.5), 0], atol=1e-12)

    def test_rgb_nearest_tie_and_threshold(self):
        rows = [(1., "early.png", "1"), (1.02, "late.png", "1.02")]
        self.assertEqual(converter.nearest_rgb(rows, 1.01, .02)[0][1], "early.png")
        self.assertIsNone(converter.nearest_rgb(rows, 1.5, .03)[0])

    def test_pose_gap_is_rejected_without_extrapolation(self):
        rows = np.array([[1., 0, 0, 0, 0, 0, 0, 1],
                         [1.11, 0, 0, 0, 0, 0, 0, 1]])
        self.assertIsNone(converter.associate_pose(rows, 1.055, .02)[0])
        self.assertIsNone(converter.associate_pose(rows, .999, .02)[0])
        self.assertIsNone(converter.associate_pose(rows, 1.111, .02)[0])

    def test_overwrite_protection(self):
        self.output.write_bytes(b"existing input")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.generate()
        self.assertEqual(self.output.read_bytes(), b"existing input")

    def test_manifest_alone_prevents_overwrite(self):
        self.output.with_suffix(".json").write_text("existing metadata")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.generate()
        self.assertEqual(self.output.with_suffix(".json").read_text(), "existing metadata")
        self.assertFalse(self.output.exists())

    def test_path_escape_rejected_and_partial_output_removed(self):
        (self.source / "depth.txt").write_text("10.005 ../escape.png\n")
        with self.assertRaisesRegex(ValueError, "escapes"):
            self.generate(frames=1)
        self.assertFalse(self.output.exists())
        self.assertFalse(self.output.with_suffix(".json").exists())

    def test_bad_depth_encoding_rejected_and_partial_output_removed(self):
        Image.fromarray(np.ones((6, 6), dtype=np.uint8)).save(self.source / "depth/0.png")
        with self.assertRaisesRegex(ValueError, "16-bit"):
            self.generate()
        self.assertFalse(self.output.exists())
        self.assertFalse(self.output.with_suffix(".json").exists())

    def test_rgb_dimension_mismatch_rejected(self):
        Image.fromarray(np.ones((4, 6, 3), dtype=np.uint8)).save(self.source / "rgb/0.png")
        with self.assertRaisesRegex(ValueError, "RGB"):
            self.generate()
        self.assertFalse(self.output.exists())

    def test_bad_indices_calibration_and_selection_rejected(self):
        for options in ({"source_indices": [-1]}, {"source_indices": [1, 1]},
                        {"source_indices": [3, 1]}, {"source_indices": [99]},
                        {"frames": 6}, {"pixel_stride": 0},
                        {"intrinsics": [0, 4, 2.5, 2.5]},
                        {"max_pose_delta": float("nan")}):
            with self.subTest(options=options), self.assertRaises(ValueError):
                self.generate(**options)
            self.assertFalse(self.output.exists())

    def test_nonmonotonic_index_and_invalid_quaternion_rejected(self):
        with self.assertRaisesRegex(ValueError, "strictly increase"):
            path = self.base / "bad.txt"
            path.write_text("2 rgb/a.png\n1 rgb/b.png\n")
            converter.load_index(path)
        with self.assertRaisesRegex(ValueError, "quaternion"):
            converter.quaternion_matrix([0, 0, 0, 0])

    def test_manifest_roundtrip_and_source_hashes(self):
        manifest = self.generate(frames=0)
        self.assertEqual(manifest["selection"]["selected_frames"], 5)
        self.assertEqual(json.loads(self.output.with_suffix(".json").read_text()), manifest)
        self.assertEqual(manifest["source"]["index_sha256"]["groundtruth.txt"],
                         converter.sha256(self.source / "groundtruth.txt"))
        self.assertEqual(manifest["binary"]["sha256"], converter.sha256(self.output))


if __name__ == "__main__":
    unittest.main()
