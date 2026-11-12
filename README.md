# Mobile Point Fusion

Mobile 3D reconstruction and point-cloud fusion.

Original project credits: D. Ben-Hoda, G. Kamar, Aaron Wetzler, Y. Honen, Ron Kimmel.

## Status

A reviewed January 2015 **PointsFusion v2.3** source snapshot is preserved in [`source/original/`](source/original/). It includes the Objective-C application, OpenGL ES fusion/index-map/removal shaders and the Xcode project. This earlier snapshot is not established as the final October 2015 release. No successful modern build or device run is claimed.

## Running

The original project targets iOS 7.1 and uses UIKit, GLKit and OpenGL ES 3. The project file is [`source/original/PointsFusion/PointsFusion.xcodeproj`](source/original/PointsFusion/PointsFusion.xcodeproj). The recovered test target is an old placeholder. Unattributed sample OBJ models and the launch bitmap were omitted, so restoring resources, updating obsolete Xcode settings and testing on suitable Apple hardware are still required. Source recovery alone does not establish compatibility with current iOS.

## Original code

The public allowlist contains 28 source/project files, including 17 Objective-C/header/shader files when the prefix header and original test are counted. Twenty-seven files preserve their original bytes. One commented-out shader line has a personal workstation name replaced; executable shader code is unchanged. Mac metadata, user state, nested old ZIPs, four sample OBJ models and the default PNG were excluded.

The [source provenance manifest](source/PROVENANCE.json) records the published paths, hashes and any redactions. Original student/third-party notices remain in place. No blanket license is assigned to recovered work. Future ports belong in `updated/` and must be described and tested as updated implementations.

## Links

- [Project entry on Aaron Wetzler's website](https://twerdster.github.io/#mobile-point-fusion)

- [Project page](https://gip.cs.technion.ac.il/last-projects-2016-2.html)

## Attribution and Updates

Original student and third-party attribution will be retained. No new license for original source code is implied by this README. Original authors can open an issue to contribute updates or request corrections or removal of their material.

