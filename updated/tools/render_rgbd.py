#!/usr/bin/env python3
"""Render recorded RGB/depth beside an actual saved final fusion point cloud.

This is saved-result visualization, not fusion, live capture or pose tracking.
Requires Python 3.10+, NumPy, Pillow, FFmpeg and FFprobe. Raw rendered frames go
to --scratch; the requested PNG/GIF/MP4/provenance go to --output-prefix.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import platform

import numpy as np
from PIL import Image, ImageDraw, ImageFont
import PIL


WIDTH, HEIGHT = 1200, 460
PANEL_WIDTH, PANEL_HEIGHT = 376, 282
PANEL_X = (12, 412, 812)
PANEL_Y = 106
BACKGROUND = (9, 20, 31)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    names = (["/System/Library/Fonts/Supplemental/Arial Bold.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"] if bold else
             ["/System/Library/Fonts/Supplemental/Arial.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"])
    for name in names:
        if Path(name).is_file():
            return ImageFont.truetype(name, size)
    raise ValueError("No Arial/DejaVu font found; install a standard TrueType font")


def load_cloud(path: Path) -> tuple[np.ndarray, str]:
    properties, count, comment = [], None, []
    with path.open() as stream:
        if stream.readline().strip() != "ply" or stream.readline().strip() != "format ascii 1.0":
            raise ValueError("requires an ASCII PLY point cloud")
        for line in stream:
            fields = line.split()
            if fields[:2] == ["element", "vertex"]:
                count = int(fields[2])
            elif fields[:1] == ["property"]:
                properties.append(fields[-1])
            elif fields[:1] == ["comment"]:
                comment.append(line.strip()[8:])
            elif line.strip() == "end_header":
                break
        else:
            raise ValueError("PLY has no end_header")
        data = np.loadtxt(stream, ndmin=2)
    if properties[:9] != ["x", "y", "z", "nx", "ny", "nz", "red", "green", "blue"]:
        raise ValueError("PLY must contain position, normal and RGB properties in adapter order")
    if count is None or count <= 0 or len(data) != count or data.shape[1] != len(properties):
        raise ValueError("PLY count/property mismatch")
    if not np.isfinite(data).all() or np.any(data[:, 6:9] < 0) or np.any(data[:, 6:9] > 255):
        raise ValueError("nonfinite PLY data or RGB outside byte range")
    return data, "; ".join(comment)


def index_rows(path: Path) -> list[tuple[float, str, str]]:
    result = []
    for line in path.read_text().splitlines():
        fields = line.split("#", 1)[0].split()
        if fields:
            if len(fields) != 2:
                raise ValueError("malformed TUM image index")
            result.append((float(fields[0]), fields[1], fields[0]))
    if not result:
        raise ValueError("empty TUM image index")
    return result


def dataset_path(root: Path, name: str) -> Path:
    path = (root/name).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        raise ValueError("image escapes dataset directory") from error
    return path


def recorded_panels(dataset: Path, source_index: int) -> tuple[Image.Image, Image.Image, dict]:
    rows = index_rows(dataset/"depth.txt")
    if source_index < 0 or source_index >= len(rows):
        raise ValueError("source index outside depth sequence")
    depth_row = rows[source_index]
    rgb_row = min(index_rows(dataset/"rgb.txt"), key=lambda x: (abs(x[0]-depth_row[0]), x[0]))
    if abs(rgb_row[0]-depth_row[0]) > .03:
        raise ValueError("RGB association exceeds 30 ms")
    rgb_path, depth_path = dataset_path(dataset, rgb_row[1]), dataset_path(dataset, depth_row[1])
    with Image.open(rgb_path) as image:
        rgb = image.convert("RGB").resize((PANEL_WIDTH, PANEL_HEIGHT), Image.Resampling.LANCZOS)
    with Image.open(depth_path) as image:
        depth = np.asarray(image)
    if depth.dtype != np.uint16 or depth.ndim != 2:
        raise ValueError("depth must be unsigned 16-bit monochrome PNG")
    metres = depth.astype(float)/5000
    # Display-only linear colour ramp: near purple/blue -> cyan/green -> far yellow.
    stops = np.array([[68, 1, 84], [59, 82, 139], [33, 145, 140],
                      [94, 201, 98], [253, 231, 37]], dtype=float)
    t = np.clip((metres-.5)/3., 0, 1)*4
    low = np.minimum(t.astype(int), 3)
    colour = stops[low]*(1-(t-low))[..., None] + stops[low+1]*(t-low)[..., None]
    colour[depth == 0] = (0, 0, 0)
    depth_image = Image.fromarray(colour.astype(np.uint8)).resize((PANEL_WIDTH, PANEL_HEIGHT), Image.Resampling.NEAREST)
    return rgb, depth_image, {
        "source_index_zero_based": source_index, "source_frame_one_based": source_index+1,
        "source_depth_timestamp": depth_row[0], "source_depth_timestamp_text": depth_row[2],
        "source_depth_file": depth_row[1], "source_depth_png_sha256": sha256(depth_path),
        "source_rgb_timestamp": rgb_row[0], "source_rgb_timestamp_text": rgb_row[2],
        "source_rgb_file": rgb_row[1], "source_rgb_png_sha256": sha256(rgb_path),
        "rgb_delta_seconds": rgb_row[0]-depth_row[0], "depth_units_per_metre": 5000,
        "depth_display_range_metres": [.5, 3.5], "missing_depth_display": "black",
        "depth_display_colour_stops_RGB": stops.astype(int).tolist(),
        "RGB_display_resampling": "Lanczos to panel dimensions",
        "depth_display_resampling": "nearest to panel dimensions; colours only, no depth conversion changes",
        "RGB_panel_RGB24_sha256": hashlib.sha256(rgb.tobytes()).hexdigest(),
        "depth_panel_RGB24_sha256": hashlib.sha256(depth_image.tobytes()).hexdigest(),
    }


def project(data: np.ndarray, target: np.ndarray, angle: float, distance: float,
            elevation: float) -> tuple[Image.Image, dict]:
    """Perspective point splats with a depth buffer; no mesh or new geometry."""
    eye = target + np.array([distance*np.sin(angle), elevation, distance*np.cos(angle)])
    z = (eye-target)/np.linalg.norm(eye-target)
    x = np.cross([0, 1, 0], z)
    x /= np.linalg.norm(x)
    y = np.cross(z, x)
    basis = np.array([x, y, z])
    camera = (data[:, :3]-eye)@basis.T
    depth = -camera[:, 2]
    focal = PANEL_HEIGHT/(2*np.tan(np.deg2rad(30.)))
    usable = depth > .03
    ids = np.flatnonzero(usable)
    xy = np.column_stack([PANEL_WIDTH/2+focal*camera[ids, 0]/depth[ids],
                          PANEL_HEIGHT/2-focal*camera[ids, 1]/depth[ids]])
    ij = np.floor(xy).astype(int)
    inside = ((ij[:, 0] >= 1) & (ij[:, 0] < PANEL_WIDTH-1) &
              (ij[:, 1] >= 1) & (ij[:, 1] < PANEL_HEIGHT-1))
    ids, ij = ids[inside], ij[inside]
    if len(ids) < 100:
        raise ValueError("display view contains too few projected cloud points")
    normal = data[:, 3:6]
    length = np.linalg.norm(normal, axis=1)
    unit = np.divide(normal, length[:, None], out=np.zeros_like(normal), where=length[:, None] > 1e-12)
    light = np.array([.2, .35, 1.])
    light /= np.linalg.norm(light)
    shading = .65+.35*np.maximum(0, (unit@basis.T)@light)
    colours = np.clip(data[:, 6:9]*shading[:, None], 0, 255).astype(np.uint8)
    # Fixed display splat: center plus four adjacent pixels. The closest sample
    # wins at each pixel. No meshing, interpolation or point displacement occurs.
    offsets = np.array([[0, 0], [-1, 0], [1, 0], [0, -1], [0, 1]])
    splat = ij[:, None, :]+offsets[None, :, :]
    pixels = (splat[:, :, 1]*PANEL_WIDTH+splat[:, :, 0]).ravel()
    sample = np.repeat(ids, len(offsets))
    order = np.lexsort((depth[sample], pixels))
    sorted_pixels = pixels[order]
    first = np.r_[True, np.diff(sorted_pixels) != 0]
    keep = order[first]
    image = np.empty((PANEL_HEIGHT*PANEL_WIDTH, 3), dtype=np.uint8)
    image[:] = BACKGROUND
    image[pixels[keep]] = colours[sample[keep]]
    return Image.fromarray(image.reshape(PANEL_HEIGHT, PANEL_WIDTH, 3)), {
        "display_orbit_angle_degrees": float(np.degrees(angle)), "eye_metres": eye.tolist(),
        "target_metres": target.tolist(), "projected_samples_inside_panel": len(ids),
        "occupied_cloud_pixels": len(keep),
    }


def run(command: list[str], log: Path) -> None:
    with log.open("w") as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)


def probe(path: Path) -> dict:
    command = ["ffprobe", "-v", "error", "-count_frames", "-show_entries",
               "stream=codec_name,width,height,r_frame_rate,nb_read_frames,duration:format=duration,size",
               "-of", "json", str(path)]
    return json.loads(subprocess.check_output(command))


def faststart(path: Path) -> bool:
    atoms = []
    with path.open("rb") as stream:
        while True:
            head = stream.read(8)
            if len(head) != 8:
                break
            size, kind = struct.unpack(">I4s", head)
            if size == 1:
                size = struct.unpack(">Q", stream.read(8))[0]
                header = 16
            else:
                header = 8
            atoms.append(kind.decode("ascii", errors="replace"))
            if size < header:
                break
            stream.seek(size-header, 1)
    return "moov" in atoms and "mdat" in atoms and atoms.index("moov") < atoms.index("mdat")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cloud", type=Path, required=True)
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--source-index", type=int, default=0)
    parser.add_argument("--state", type=Path, help="optional valid saved-run JSON; its point count must match the PLY")
    parser.add_argument("--output-prefix", type=Path, required=True)
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=24)
    parser.add_argument("--fps", type=int, default=4)
    parser.add_argument("--orbit-degrees", type=float, default=12.)
    parser.add_argument("--distance", type=float, default=1.8)
    parser.add_argument("--elevation", type=float, default=.45)
    parser.add_argument("--target", type=float, nargs=3)
    parser.add_argument("--overwrite", action="store_true")
    args = parser.parse_args()
    if args.frames < 4 or args.fps <= 0 or args.distance <= .1:
        raise ValueError("require at least 4 frames, positive fps and distance > 0.1 m")
    if not all(shutil.which(name) for name in ("ffmpeg", "ffprobe")):
        raise ValueError("FFmpeg and FFprobe are required")
    prefix = args.output_prefix
    outputs = {kind: Path(str(prefix)+suffix) for kind, suffix in
               (("png", ".png"), ("gif", ".gif"), ("mp4", ".mp4"), ("provenance", "-provenance.json"))}
    if not args.overwrite and any(path.exists() for path in outputs.values()):
        raise ValueError("preview output exists; choose a new prefix or explicit --overwrite")
    prefix.parent.mkdir(parents=True, exist_ok=True)
    args.scratch.mkdir(parents=True, exist_ok=True)
    frame_dir = args.scratch/"frames"
    frame_dir.mkdir(exist_ok=True)
    cloud_sha = sha256(args.cloud)
    data, ply_comment = load_cloud(args.cloud)
    if sha256(args.cloud) != cloud_sha:
        raise ValueError("cloud changed while being read")
    rgb, depth, source = recorded_panels(args.dataset, args.source_index)
    target = np.asarray(args.target) if args.target else np.median(data[:, :3], axis=0)
    state_path = args.state
    state = json.loads(state_path.read_text()) if state_path else {}
    if "model_points" in state and state["model_points"] != len(data):
        raise ValueError("saved state and final cloud point count disagree")
    base = Image.new("RGB", (WIDTH, HEIGHT), BACKGROUND)
    draw = ImageDraw.Draw(base)
    draw.text((15, 10), "Mobile Point Fusion | recorded TUM RGB-D", font=font(26, True), fill="#f0f6fb")
    draw.text((15, 46), "Real Kinect RGB/depth + supplied motion-capture poses | metres | no pose estimation", font=font(17), fill="#bad0df")
    labels = ("Recorded Kinect RGB", "Measured depth (display colours)", "Saved final cloud, display orbit")
    for x, label in zip(PANEL_X, labels):
        draw.text((x, 79), label, font=font(17, True), fill="#f0f6fb")
    base.paste(rgb, (PANEL_X[0], PANEL_Y))
    base.paste(depth, (PANEL_X[1], PANEL_Y))
    draw.text((12, 399), f"Source frame {args.source_index+1} | recorded colour", font=font(15), fill="#bad0df")
    draw.text((412, 399), "0.5–3.5 m colour scale; black = missing", font=font(15), fill="#bad0df")
    draw.text((812, 399), f"{len(data):,} final points | stored RGB + normals", font=font(15), fill="#bad0df")
    if state:
        draw.text((12, 424), f"Result from {state.get('frame', '?')} supplied-pose views; saved final geometry is unchanged throughout this display orbit.", font=font(15), fill="#bad0df")
    else:
        draw.text((12, 424), "Saved final geometry is unchanged throughout this display orbit; no new fusion is executed.", font=font(15), fill="#bad0df")
    draw.text((12, 442), "TUM RGB-D: Sturm et al., IROS 2012 | CC BY 4.0 | Saved-result visualization, not a live app or a processing-speed measurement.", font=font(13), fill="#a5bdcf")
    samples, frame_records = [], []
    for index in range(args.frames):
        angle = np.deg2rad(args.orbit_degrees)*np.sin(2*np.pi*index/args.frames)
        cloud_image, record = project(data, target, angle, args.distance, args.elevation)
        image = base.copy()
        image.paste(cloud_image, (PANEL_X[2], PANEL_Y))
        path = frame_dir/f"{index:04d}.png"
        image.save(path)
        frame_records.append(dict(record, frame=index, raw_png_sha256=sha256(path)))
        if index == 0:
            image.save(outputs["png"])
        if index in {0, args.frames//8, args.frames//4, args.frames//2, 3*args.frames//4, args.frames-1}:
            samples.append((index, image.copy()))
    sheet = Image.new("RGB", (1200, 3*250), BACKGROUND)
    for i, (index, image) in enumerate(samples):
        x, y = (i % 2)*600, (i//2)*250
        sheet.paste(image.resize((600, 230)), (x, y+20))
        ImageDraw.Draw(sheet).text((x+8, y+2), f"Display frame {index}", font=font(14), fill="white")
    sheet.save(args.scratch/"contact-sheet.png")
    video_command = ["ffmpeg", "-hide_banner", "-y", "-framerate", str(args.fps),
                     "-i", str(frame_dir/"%04d.png"), "-frames:v", str(args.frames), "-an",
                     "-c:v", "libx264", "-preset", "slow", "-crf", "23", "-pix_fmt", "yuv420p",
                     "-movflags", "+faststart", "-metadata", "title=Mobile Point Fusion: real RGB-D saved final cloud",
                     "-metadata", "comment=Recorded TUM Kinect RGB/depth; supplied motion-capture poses; no pose estimation. Saved final cloud, display orbit only; not a live app or performance benchmark. TUM data Sturm et al., IROS 2012, CC BY 4.0.", str(outputs["mp4"])]
    run(video_command, args.scratch/"encode-mp4.log")
    gif_filter = "[0:v]scale=960:368:flags=area,split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle"
    gif_command = ["ffmpeg", "-hide_banner", "-y", "-framerate", str(args.fps), "-i", str(frame_dir/"%04d.png"),
                   "-filter_complex", gif_filter, "-frames:v", str(args.frames), "-loop", "0", str(outputs["gif"])]
    run(gif_command, args.scratch/"encode-gif.log")
    if outputs["gif"].stat().st_size >= 2_000_000:
        raise ValueError("GIF exceeds strict 2,000,000-byte cap; reduce --frames or orbit amplitude and rerun explicitly")
    if outputs["mp4"].stat().st_size >= 10_000_000:
        raise ValueError("MP4 exceeds strict 10,000,000-byte cap")
    probes = {}
    for kind in ("gif", "mp4"):
        run(["ffmpeg", "-v", "error", "-i", str(outputs[kind]), "-f", "null", "-"], args.scratch/f"decode-{kind}.log")
        probes[kind] = probe(outputs[kind])
    gif_hashes, gif_duration = set(), 0
    with Image.open(outputs["gif"]) as image:
        gif_count, gif_loop = image.n_frames, image.info.get("loop")
        for i in range(image.n_frames):
            image.seek(i)
            gif_hashes.add(hashlib.sha256(image.convert("RGB").tobytes()).hexdigest())
            gif_duration += image.info.get("duration", 0)
    if gif_count != args.frames or len(gif_hashes) < 4 or not faststart(outputs["mp4"]):
        raise ValueError("unexpected GIF frame count/content or MP4 faststart layout")
    provenance = {
        "schema_version": 1, "status": "rendered_and_decoded",
        "scope": "Actual saved final point cloud with recorded RGB/depth reference. Display orbit only; no new fusion, live app, tracking, surface-accuracy or speed claim.",
        "source_cloud": {"filename": args.cloud.name, "sha256": cloud_sha, "points": len(data), "units": "metres", "PLY_comment": ply_comment},
        "recorded_panels": source,
        "saved_run_state": {key: state[key] for key in ("frame", "frames", "model_points", "merged_total", "removed_total", "renderer", "gl_version", "real_RGBD", "pose_estimation") if key in state},
        "saved_run_state_sha256": sha256(state_path) if state else None,
        "rendering": {"canvas": [WIDTH, HEIGHT], "panel_size": [PANEL_WIDTH, PANEL_HEIGHT],
                      "point_samples": "all stored finite points; no confidence or geometric filtering",
                      "projection": "60-degree vertical perspective FOV, near 0.03 m; nearest depth wins; fixed center plus four-neighbor display splat",
                      "colour": "stored RGB multiplied by 0.65+0.35*max(0,unit(view_normal) dot unit(0.2,0.35,1)); zero normals use ambient",
                      "normal_handling": "stored averaged normals normalized for display only",
                      "target_metres": target.tolist(), "distance_metres": args.distance, "elevation_metres": args.elevation,
                      "orbit_amplitude_degrees": args.orbit_degrees, "orbit_function": "amplitude*sin(2*pi*frame/frame_count)",
                      "frame_count": args.frames, "display_fps": args.fps, "display_seconds": args.frames/args.fps,
                      "runtime_performance_claim": False, "geometry_changes_during_orbit": False},
        "dataset_credit": {"authors": "Jürgen Sturm, Nikolas Engelhard, Felix Endres, Wolfram Burgard, Daniel Cremers",
                           "publication": "A Benchmark for the Evaluation of RGB-D SLAM Systems, IROS 2012",
                           "dataset": "https://cvg.cit.tum.de/data/datasets/rgbd-dataset", "license": "CC BY 4.0", "license_url": "https://creativecommons.org/licenses/by/4.0/",
                           "changes": "selected recorded reference frame; display-only RGB resizing/depth colour mapping; projection/shading of separately computed final fusion geometry; GIF palette and H264 encoding"},
        "encode_commands": {"mp4": [str(x).replace(str(frame_dir), "$FRAME_DIR").replace(str(outputs["mp4"]), "$MP4") for x in video_command],
                            "gif": [str(x).replace(str(frame_dir), "$FRAME_DIR").replace(str(outputs["gif"]), "$GIF") for x in gif_command]},
        "source_renderer_sha256": sha256(Path(__file__)),
        "environment": {"python": platform.python_version(), "numpy": np.__version__, "pillow": PIL.__version__,
                        "FFmpeg": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
                        "font_regular_filename": Path(font(13).path).name,
                        "font_regular_sha256": sha256(Path(font(13).path)),
                        "font_bold_filename": Path(font(17, True).path).name,
                        "font_bold_sha256": sha256(Path(font(17, True).path))},
        "reproduction": f"python3 updated/tools/render_rgbd.py --cloud /path/to/fused.ply --dataset /path/to/tum-fr1-xyz --source-index {args.source_index} --output-prefix updated/assets/real-rgbd-preview --scratch /path/to/preview-scratch --frames {args.frames} --fps {args.fps} --orbit-degrees {args.orbit_degrees} --distance {args.distance} --elevation {args.elevation} --target {' '.join(str(x) for x in target)}" + (" --state /path/to/native-state.json" if args.state else ""),
        "media": {path.name: {"bytes": path.stat().st_size, "sha256": sha256(path)} for kind, path in outputs.items() if kind != "provenance"},
        "QA": {"ffmpeg_full_decode": {"mp4": "passed", "gif": "passed"}, "ffprobe": probes,
               "GIF_decoded_frames": gif_count, "GIF_unique_RGB_frames": len(gif_hashes), "GIF_total_duration_ms": gif_duration,
               "GIF_loop": gif_loop, "MP4_moov_before_mdat": True},
        "frames": frame_records,
    }
    outputs["provenance"].write_text(json.dumps(provenance, indent=2, ensure_ascii=False)+"\n")
    print(json.dumps({"media": provenance["media"], "QA": provenance["QA"]}, indent=2))


if __name__ == "__main__":
    main()
