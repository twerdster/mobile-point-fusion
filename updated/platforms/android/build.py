#!/usr/bin/env python3
"""Build a task-local debug APK using existing Android SDK/NDK/JDK, no Gradle download."""
import argparse, pathlib, shutil, subprocess, os, zipfile

p = argparse.ArgumentParser()
p.add_argument("--sdk", type=pathlib.Path, required=True)
p.add_argument("--output", type=pathlib.Path, required=True)
p.add_argument("--ndk-version", default="27.2.12479018")
p.add_argument("--build-tools", default="34.0.0")
p.add_argument("--r8-jar", type=pathlib.Path, required=True)
p.add_argument("--integer-float-storage", action="store_true")
a = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[3]
src = pathlib.Path(__file__).parent
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
sdk = a.sdk.resolve()
bt = sdk / "build-tools" / a.build_tools
jar = sdk / "platforms/android-34/android.jar"
pre = sdk / "ndk" / a.ndk_version / "toolchains/llvm/prebuilt"
host = next(pre.glob("*"))
clang = host / "bin/aarch64-linux-android26-clang++"
lib = out / "lib/arm64-v8a/libfusion_native.so"
lib.parent.mkdir(parents=True, exist_ok=True)
subprocess.run(
    [
        str(clang),
        "-std=c++17",
        "-O2",
        "-g",
        "-shared",
        "-fPIC",
        "-fexceptions",
        "-static-libstdc++",
        '-DSOURCE_ROOT="."',
        *(["-DPF_INTEGER_FLOAT_STORAGE=1"] if a.integer_float_storage else []),
        str(src / "jni.cpp"),
        "-o",
        str(lib),
        "-lGLESv3",
        "-lEGL",
        "-landroid",
        "-llog",
    ],
    check=True,
)
assets = out / "assets"
shaders = [
    "source/original/PointsFusion/PointsFusion/indexMap.vsh",
    "source/original/PointsFusion/PointsFusion/indexMap.fsh",
    "updated/shaders/Fusion.vsh",
    "updated/shaders/Fusion.fsh",
    "updated/shaders/PointRemoval.vsh",
    "updated/shaders/PointRemoval.fsh",
]
for s in shaders:
    dest = assets / "fusion" / s
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / s, dest)
classes = out / "classes"
classes.mkdir(exist_ok=True)
dex = out / "dex"
dex.mkdir(exist_ok=True)
subprocess.run(
    [
        "javac",
        "--release",
        "8",
        "-classpath",
        str(jar),
        "-d",
        str(classes),
        str(src / "FusionActivity.java"),
    ],
    check=True,
)
subprocess.run(
    [
        "java",
        "-cp",
        str(a.r8_jar),
        "com.android.tools.r8.D8",
        "--min-api",
        "26",
        "--lib",
        str(jar),
        "--output",
        str(dex),
        *[str(f) for f in classes.rglob("*.class")],
    ],
    check=True,
)
unsigned = out / "unsigned.apk"
subprocess.run(
    [
        str(bt / "aapt"),
        "package",
        "-f",
        "-M",
        str(src / "AndroidManifest.xml"),
        "-I",
        str(jar),
        "-A",
        str(assets),
        "-F",
        str(unsigned),
    ],
    check=True,
)
with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_DEFLATED) as z:
    z.write(lib, "lib/arm64-v8a/libfusion_native.so")
    z.write(dex / "classes.dex", "classes.dex")
aligned = out / "aligned.apk"
subprocess.run(
    [str(bt / "zipalign"), "-f", "4", str(unsigned), str(aligned)], check=True
)
key = out / "debug.keystore"
if not key.exists():
    subprocess.run(
        [
            "keytool",
            "-genkeypair",
            "-keystore",
            str(key),
            "-storepass",
            "android",
            "-keypass",
            "android",
            "-alias",
            "androiddebugkey",
            "-dname",
            "CN=Local Debug",
            "-keyalg",
            "RSA",
            "-validity",
            "3650",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
apk = out / "MobilePointFusion.apk"
subprocess.run(
    [
        str(bt / "apksigner"),
        "sign",
        "--ks",
        str(key),
        "--ks-pass",
        "pass:android",
        "--out",
        str(apk),
        str(aligned),
    ],
    check=True,
)
subprocess.run([str(bt / "apksigner"), "verify", str(apk)], check=True)
print(apk)
