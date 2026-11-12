# Historical student-project archive

## Current source state — 2026-11-12

A reviewed January 2015 **PointsFusion v2.3** source snapshot is preserved in [`source/original/`](source/original/). It includes the Objective-C application, OpenGL ES fusion/index-map/removal shaders and the Xcode project. This earlier snapshot is not established as the final October 2015 release. No successful modern build or device run is claimed.

The public allowlist contains 28 source/project files, including 17 Objective-C/header/shader files when the prefix header and original test are counted. Twenty-seven files preserve their original bytes. One commented-out shader line has a personal workstation name replaced; executable shader code is unchanged. Mac metadata, user state, nested old ZIPs, four sample OBJ models and the default PNG were excluded.

## Setup and validation

The original project targets iOS 7.1 and uses UIKit, GLKit and OpenGL ES 3. The project file is [`source/original/PointsFusion/PointsFusion.xcodeproj`](source/original/PointsFusion/PointsFusion.xcodeproj). The recovered test target is an old placeholder. Unattributed sample OBJ models and the launch bitmap were omitted, so restoring resources, updating obsolete Xcode settings and testing on suitable Apple hardware are still required. Source recovery alone does not establish compatibility with current iOS.

Publication validation covers the source allowlist, SHA-256 provenance, text/XML or project metadata where applicable, and credential/private-material scanning. It is not an end-to-end runtime result. Read README.md and source/PROVENANCE.json before further edits; preserve source/original and existing documents. Any future updated implementation belongs in updated/ with its own requirements, tests, attribution and a clear description of its differences. Do not introduce a blanket license. Review this context file before committing; keep actual validation and source counts accurate.

## Interactive possibilities

A new offline point-cloud fusion or shader visualization could be practical in a notebook or browser. The recovered iOS application itself is not a Colab or Hugging Face application.

## Repository presentation

README includes an unchanged original website figure, three factual language/platform/source badges and the original-code section. Student credits, source provenance and build limitations remain intact. No source files, dependencies or licenses changed in this presentation update.

## Local build entrypoint check

A November 2026 Xcode 16.1 check could list the project. The stock simulator build found no compatible destination. A separate unsigned arm64 target build progressed to resource processing and failed because the intentionally omitted `zero.obj`, `monkey.obj`, `capsule.obj` and `Porsche_911_GT2.obj` are required. These checks used private native-filesystem copies, changed no original project settings, and did not run the app. Licensed replacement resources and further platform work are still needed.
