"""Bundle an optional prepared recording without retaining stale input assets."""

from pathlib import Path
import shutil


def bundle_recording(recording, destination):
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    if (
        recording is not None
        and Path(recording).resolve() == (destination / "recording.pfrgbd").resolve()
    ):
        raise ValueError(
            "Input recording must be outside its build resource destination"
        )
    for name in ("recording.pfrgbd", "recording.json", "recording-NOTICE.txt"):
        (destination / name).unlink(missing_ok=True)
    if recording is None:
        return
    recording = Path(recording).resolve()
    with recording.open("rb") as stream:
        if stream.read(8) != b"PFRGBD1\0":
            raise ValueError("--recording must be a prepared PFRGBD1 pack")
    shutil.copyfile(recording, destination / "recording.pfrgbd")
    metadata = recording.with_suffix(".json")
    if metadata.is_file():
        shutil.copyfile(metadata, destination / "recording.json")
    for notice in (
        recording.with_suffix(".NOTICE.txt"),
        recording.parent / "NOTICE.txt",
    ):
        if notice.is_file():
            shutil.copyfile(notice, destination / "recording-NOTICE.txt")
            break
