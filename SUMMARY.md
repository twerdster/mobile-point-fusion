# mobile-point-fusion: maintenance context

Read this file first, then SKILL.md and README.md. This context describes the repository as of 2026-11-12.

## Purpose and current source

January 2015 PointsFusion v2.3 Objective-C/OpenGL ES correspondence-shader and CPU-fusion snapshot; earlier than the reported final October 2015 release, which is unrecovered/unverified.

28 original source/project files in source/original/; 27 byte-preserved, one comment privacy edit recorded in source/PROVENANCE.json. ViewController.m reads OBJ vertex/normal data and combines GPU correspondence maps with CPU fusion/removal. Recovered entrypoint is a model-based experiment, not verified live capture.

## Use and validation

Inspect source/original/PointsFusion/PointsFusion.xcodeproj. iOS 7.1, UIKit, GLKit, OpenGL ES 3. Restore appropriately licensed OBJ/bitmap resources and modernize platform settings before attempting a port.

Recorded November 2026 Xcode 16.1 check listed project; simulator destination unavailable. Unsigned arm64 target failed at omitted OBJ resources. App was not run. Original test target is a placeholder.

zero.obj, monkey.obj, capsule.obj and Porsche_911_GT2.obj are missing. No shader correctness, fusion accuracy, sensor capture, current runtime or performance validation. No updated port/notebook.

## Documentation maintenance

The 2026-11-12 documentation revision organizes the README around implementation, available source, setup, recorded checks and limits. It preserves historical visuals and links, with captions separating historical results from updated examples. No runnable code, source provenance, pinned revisions, licenses or validation results changed in this revision. Recorded earlier tests were not rerun for the prose-only edits.

Preserve original source and authorship. New ports belong under updated/ and require their own scope/validation. Do not infer a blanket license from recovery. Review this file and SKILL.md before every commit and synchronize changes to workflow, commands, validation or status. Check relative links and figure rendering; never describe inventory counts or component tests as full-application validation.

Two actual Claude editorial review rounds on 2026-11-12 covered the README and related changed documentation, including context files, component guides and applicable algorithm/build notes or schematic text. Useful qualifications were checked against source, preserved project records and saved validation logs, then applied. Editorial review adds no runtime coverage.
