#!/usr/bin/env python3
"""Copy the historical project outside the repo and add generated test resources.

All four historical resource names refer to the SAME supplied generated model.
They are replacements, never recovered versions of the missing historical OBJ.
No source, project settings, notices or files under source/original are changed.
"""
import argparse
import shutil
from pathlib import Path
import struct
import zlib


def chunk(kind, data):
    return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('generated_obj', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    repo = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    if output.is_relative_to(repo) or output.exists():
        p.error('output must be a new directory outside this repository')
    if not args.generated_obj.is_file():
        p.error('generated OBJ does not exist; run the offline demo first')
    shutil.copytree(repo/'source/original/PointsFusion', output)
    for name in ('zero', 'monkey', 'capsule', 'Porsche_911_GT2'):
        shutil.copyfile(args.generated_obj, output/'PointsFusion'/f'{name}.obj')
    # Plain new launch placeholder, not a reconstruction or app screenshot.
    w,h=640,480
    raw=(b'\x00'+bytes([16,28,41])*w)*h
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    (output/'default.png').write_bytes(png)
    print(output/'PointsFusion.xcodeproj')


if __name__=='__main__':main()
