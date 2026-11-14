#!/usr/bin/env python3
"""Build new native app with installed Apple SDK; never edits historical project."""
import argparse, pathlib, plistlib, shutil, subprocess, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from recording_resources import bundle_recording

p = argparse.ArgumentParser()
p.add_argument("--platform", choices=["macos", "simulator"], required=True)
p.add_argument("--output", type=pathlib.Path, required=True)
p.add_argument("--sign", default="-")
p.add_argument("--recording", type=pathlib.Path)
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[3]
app = a.output.resolve() / "MobilePointFusion.app"
app.mkdir(parents=True, exist_ok=True)
mac = a.platform == "macos"
contents = app / "Contents" if mac else app
binary = contents / "MacOS" / "MobilePointFusion" if mac else app / "MobilePointFusion"
binary.parent.mkdir(parents=True, exist_ok=True)
resources = contents / "Resources" if mac else app
bundle_recording(a.recording, resources / "fusion")
for relative in [
    "source/original/PointsFusion/PointsFusion/indexMap.vsh",
    "source/original/PointsFusion/PointsFusion/indexMap.fsh",
    "updated/shaders/Fusion.vsh",
    "updated/shaders/Fusion.fsh",
    "updated/shaders/PointRemoval.vsh",
    "updated/shaders/PointRemoval.fsh",
]:
    dest = resources / "fusion" / relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / relative, dest)
sdk = {"macos": "macosx", "simulator": "iphonesimulator", "ios": "iphoneos"}[a.platform]
sdkpath = subprocess.check_output(
    ["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True
).strip()
target = {
    "macos": "arm64-apple-macos12.0",
    "simulator": "arm64-apple-ios15.0-simulator",
    "ios": "arm64-apple-ios15.0",
}[a.platform]
cmd = [
    "xcrun",
    "--sdk",
    sdk,
    "clang++",
    "-std=c++17",
    "-O2",
    "-g",
    "-fobjc-arc",
    "-fexceptions",
    "-Wno-deprecated-declarations",
    "-target",
    target,
    "-isysroot",
    sdkpath,
    '-DSOURCE_ROOT="."',
    str(pathlib.Path(__file__).with_name("main.mm")),
    "-o",
    str(binary),
]
for framework in (
    ["Cocoa", "OpenGL"] if mac else ["UIKit", "QuartzCore", "OpenGLES", "CoreGraphics"]
):
    cmd += ["-framework", framework]
print("Compiling", a.platform, flush=True)
subprocess.run(cmd, check=True)
info = {
    "CFBundleExecutable": "MobilePointFusion",
    "CFBundleIdentifier": "io.github.twerdster.mobilepointfusion.updated",
    "CFBundleName": "Mobile Point Fusion",
    "CFBundleDisplayName": "Mobile Point Fusion",
    "CFBundlePackageType": "APPL",
    "CFBundleShortVersionString": "1.0",
    "CFBundleVersion": "1",
    "NSHighResolutionCapable": True,
}
if not mac:
    info.update(
        {
            "MinimumOSVersion": "15.0",
            "UIDeviceFamily": [1, 2],
            "UISupportedInterfaceOrientations": [
                "UIInterfaceOrientationPortrait",
                "UIInterfaceOrientationLandscapeLeft",
                "UIInterfaceOrientationLandscapeRight",
            ],
            "UILaunchScreen": {},
        }
    )
with (contents / "Info.plist").open("wb") as f:
    plistlib.dump(info, f)
subprocess.run(["codesign", "--force", "--sign", a.sign, str(app)], check=True)
print(app)
