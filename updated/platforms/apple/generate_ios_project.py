#!/usr/bin/env python3
"""Create a standalone private Xcode target; signing settings supplied externally."""
from pathlib import Path
import argparse, hashlib, plistlib, shutil, sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from recording_resources import bundle_recording

p = argparse.ArgumentParser()
p.add_argument("--output", type=Path, required=True)
p.add_argument("--recording", type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parents[3]
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
bundle_recording(a.recording, out / "fusion")
objects = {}


def ident(s):
    return hashlib.sha1(s.encode()).hexdigest()[:24].upper()


def obj(label, isa, **kw):
    key = ident(label)
    objects[key] = {"isa": isa, **kw}
    return key


def file(path, kind, tree="<group>"):
    return obj(
        "file:" + path,
        "PBXFileReference",
        path=path,
        lastKnownFileType=kind,
        sourceTree=tree,
    )


def build(ref):
    return obj("build:" + ref, "PBXBuildFile", fileRef=ref)


source = file(
    str(Path(__file__).with_name("main.mm")), "sourcecode.cpp.objcpp", "<absolute>"
)
for name in [
    "source/original/PointsFusion/PointsFusion/indexMap.vsh",
    "source/original/PointsFusion/PointsFusion/indexMap.fsh",
    "updated/shaders/Fusion.vsh",
    "updated/shaders/Fusion.fsh",
    "updated/shaders/PointRemoval.vsh",
    "updated/shaders/PointRemoval.fsh",
]:
    dest = out / "fusion" / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(root / name, dest)
resources = file("fusion", "folder")
frameworks = []
for name in ["UIKit", "Foundation", "QuartzCore", "OpenGLES", "CoreGraphics"]:
    frameworks.append(
        file(
            "System/Library/Frameworks/" + name + ".framework",
            "wrapper.framework",
            "SDKROOT",
        )
    )
product = file("MobilePointFusion.app", "wrapper.application", "BUILT_PRODUCTS_DIR")
phases = [
    obj(
        "sources",
        "PBXSourcesBuildPhase",
        buildActionMask=2147483647,
        files=[build(source)],
        runOnlyForDeploymentPostprocessing=0,
    ),
    obj(
        "resources",
        "PBXResourcesBuildPhase",
        buildActionMask=2147483647,
        files=[build(resources)],
        runOnlyForDeploymentPostprocessing=0,
    ),
    obj(
        "frameworks",
        "PBXFrameworksBuildPhase",
        buildActionMask=2147483647,
        files=[build(f) for f in frameworks],
        runOnlyForDeploymentPostprocessing=0,
    ),
]
settings = {
    "SDKROOT": "iphoneos",
    "IPHONEOS_DEPLOYMENT_TARGET": "15.0",
    "TARGETED_DEVICE_FAMILY": "1,2",
    "CLANG_ENABLE_OBJC_ARC": "YES",
    "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
    "CLANG_CXX_LIBRARY": "libc++",
    "CODE_SIGN_STYLE": "Automatic",
    "DEVELOPMENT_TEAM": "",
    "PRODUCT_BUNDLE_IDENTIFIER": "io.github.twerdster.mobilepointfusion.updated",
    "PRODUCT_NAME": "MobilePointFusion",
    "INFOPLIST_FILE": "Info.plist",
    "GCC_PREPROCESSOR_DEFINITIONS": ["$(inherited)", 'SOURCE_ROOT=\\".\\"'],
    "WARNING_CFLAGS": ["-Wno-deprecated-declarations"],
    "SUPPORTED_PLATFORMS": "iphoneos iphonesimulator",
    "ENABLE_BITCODE": "NO",
    "GCC_OPTIMIZATION_LEVEL": "2",
    "DEBUG_INFORMATION_FORMAT": "dwarf",
    "ENABLE_USER_SCRIPT_SANDBOXING": "YES",
}
cfgs = [
    obj("cfg:" + n, "XCBuildConfiguration", name=n, buildSettings=settings)
    for n in ["Debug", "Release"]
]
configs = obj(
    "configs",
    "XCConfigurationList",
    buildConfigurations=cfgs,
    defaultConfigurationIsVisible=0,
    defaultConfigurationName="Debug",
)
target = obj(
    "target",
    "PBXNativeTarget",
    name="MobilePointFusion",
    productName="MobilePointFusion",
    productReference=product,
    productType="com.apple.product-type.application",
    buildConfigurationList=configs,
    buildPhases=phases,
    dependencies=[],
    buildRules=[],
)
products = obj(
    "products", "PBXGroup", name="Products", children=[product], sourceTree="<group>"
)
main = obj(
    "main",
    "PBXGroup",
    children=[source, resources, *frameworks, products],
    sourceTree="<group>",
)
project = obj(
    "project",
    "PBXProject",
    attributes={"LastUpgradeCheck": "1610"},
    buildConfigurationList=configs,
    compatibilityVersion="Xcode 14.0",
    developmentRegion="en",
    knownRegions=["en", "Base"],
    mainGroup=main,
    productRefGroup=products,
    projectDirPath="",
    projectRoot="",
    targets=[target],
)
xproj = out / "MobilePointFusion.xcodeproj"
xproj.mkdir(exist_ok=True)
(xproj / "project.pbxproj").write_bytes(
    plistlib.dumps(
        {
            "archiveVersion": "1",
            "classes": {},
            "objectVersion": "56",
            "objects": objects,
            "rootObject": project,
        },
        sort_keys=False,
    )
)
info = {
    "CFBundleExecutable": "$(EXECUTABLE_NAME)",
    "CFBundleIdentifier": "$(PRODUCT_BUNDLE_IDENTIFIER)",
    "CFBundleName": "Mobile Point Fusion",
    "CFBundleDisplayName": "Mobile Point Fusion",
    "CFBundlePackageType": "APPL",
    "CFBundleShortVersionString": "1.0",
    "CFBundleVersion": "1",
    "MinimumOSVersion": "15.0",
    "UIDeviceFamily": [1, 2],
    "UISupportedInterfaceOrientations": [
        "UIInterfaceOrientationPortrait",
        "UIInterfaceOrientationLandscapeLeft",
        "UIInterfaceOrientationLandscapeRight",
    ],
    "UILaunchScreen": {},
}
(out / "Info.plist").write_bytes(plistlib.dumps(info))
print(xproj)
