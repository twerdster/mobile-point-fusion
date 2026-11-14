"""Analytic depth/projection controls for the saved-cloud evaluator."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np

path = Path(__file__).resolve().parents[1] / "tools/evaluate_rgbd.py"
spec = importlib.util.spec_from_file_location("rgbd_evaluator", path)
evaluator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evaluator)


class ProjectionControls(unittest.TestCase):
    def setUp(self):
        self.calibration = (7, 5, 5.0, 9.0, 2.1, 1.8)
        v, u = np.mgrid[:5, :7]
        self.points = np.column_stack(((u.ravel() - 2.1) * 2.5 / 5,
                                       -(v.ravel() - 1.8) * 2.5 / 9,
                                       np.full(35, -2.5)))

    def test_off_axis_plane_and_rigid_pose(self):
        # 90-degree rotation and translation in world space must leave the
        # independent camera-grid prediction at exactly 2.5 metres.
        pose = np.array([[0., -1., 0., .4], [1., 0., 0., -.7],
                         [0., 0., 1., 1.2], [0., 0., 0., 1.]])
        world = self.points @ pose[:3, :3].T + pose[:3, 3]
        predicted = evaluator.project_depth(world, pose, self.calibration)
        np.testing.assert_allclose(predicted, np.full((5, 7), 2.5), atol=1e-14)
        report = evaluator.depth_agreement(predicted, np.full((5, 7), 2.5))
        self.assertEqual(report["projected_sensor_pixels"], 35)
        self.assertLess(report["rmse_depth_m"], 1e-14)

    def test_nearest_depth_visibility_and_missing_coverage(self):
        near = self.points[10] * (2.0 / 2.5)
        points = np.vstack([self.points[:-1], near, [0., 0., 1.]])
        predicted = evaluator.project_depth(points, np.eye(4), self.calibration)
        self.assertEqual(predicted.ravel()[10], 2.)
        self.assertTrue(np.isinf(predicted[-1, -1]))
        report = evaluator.depth_agreement(predicted, np.full((5, 7), 2.5))
        self.assertEqual(report["projected_sensor_pixels"], 34)
        self.assertEqual(report["nearer_than_sensor_threshold_pixels"], 1)
        self.assertEqual(report["within_threshold_pixels"], 33)

    def test_range_gate_and_metre_units(self):
        measured = np.array([[0., .49, .5, 3.5, 3.51]])
        predicted = measured + .02
        report = evaluator.depth_agreement(predicted, measured)
        self.assertEqual(report["valid_sensor_pixels"], 2)
        self.assertEqual(report["coverage"], 1.)
        self.assertAlmostEqual(report["mean_absolute_depth_error_m"], .02)

    def test_binary_depth_scale_and_column_major_pose(self):
        pose = np.array([[0., -1., 0., .4], [1., 0., 0., -.7],
                         [0., 0., 1., 1.2], [0., 0., 0., 1.]])
        data = b"PFRGBD1\0" + struct.pack("<III5f", 3, 3, 1, 5000, 10, 12, 1, 1)
        data += struct.pack("<d", 1305031102.25) + pose.astype("<f4").tobytes(order="F")
        data += np.full(9, 12500, dtype="<u2").tobytes() + bytes(27)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "control.pfrgbd"
            path.write_bytes(data)
            calibration, frames = evaluator.read_pack(path)
            self.assertEqual(calibration, (3, 3, 10., 12., 1., 1.))
            self.assertEqual(frames[0][0], 1305031102.25)
            np.testing.assert_allclose(frames[0][1], pose, atol=1e-7)
            np.testing.assert_array_equal(frames[0][2], np.full((3, 3), 2.5))

    def test_native_input_concatenation_and_missing_frame_rejection(self):
        header = "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\nproperty float z\nend_header\n"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "input-0001.ply").write_text(header + "4 5 -6\n")
            (root / "input-0000.ply").write_text(header + "1 2 -3\n")
            points, provenance = evaluator.read_input_directory(root, 2)
            np.testing.assert_array_equal(points, [[1., 2., -3.], [4., 5., -6.]])
            self.assertEqual(provenance["points"], 2)
            self.assertEqual(len(provenance["files"]), 2)
            with self.assertRaises(ValueError):
                evaluator.read_input_directory(root, 3)

    def test_rejects_ply_with_mislabelled_coordinates(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "bad.ply"
            path.write_text("ply\nformat ascii 1.0\nelement vertex 1\nproperty float z\nproperty float y\nproperty float x\nend_header\n1 2 3\n")
            with self.assertRaisesRegex(ValueError, "x/y/z"):
                evaluator.read_ply(path)


if __name__ == "__main__":
    unittest.main()
