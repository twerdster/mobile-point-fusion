#!/usr/bin/env python3
"""Render saved ES3/CPU fusion clouds; this script does not rerun fusion.

Generated OBJ input and supplied poses are explicit. NumPy/Pillow/FFmpeg are
needed only for this saved-output visualization, not the C++ reconstruction.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def cloud(path):
    with path.open() as f:
        for line in f:
            if line.strip() == 'end_header':
                break
        return np.loadtxt(f, ndmin=2)


def font(size, bold=False):
    candidates = [('/System/Library/Fonts/Supplemental/Arial Bold.ttf' if bold else
                   '/System/Library/Fonts/Supplemental/Arial.ttf'),
                  ('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf' if bold else
                   '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')]
    return ImageFont.truetype(next(p for p in candidates if Path(p).exists()), size)


def project(data, angle, width, height, zoom=1):
    eye = np.array([3.1*np.sin(angle), .35, 3.1*np.cos(angle)])
    z = eye / np.linalg.norm(eye)
    x = np.cross([0, 1, 0], z); x /= np.linalg.norm(x)
    y = np.cross(z, x)
    basis = np.array([x, y, z])
    p = (data[:, :3]-eye) @ basis.T
    n = data[:, 3:6] @ basis.T
    f = zoom*height/(2*np.tan(np.deg2rad(25)))
    xy = np.column_stack([width/2 + f*p[:, 0]/-p[:, 2],
                          height/2 - f*p[:, 1]/-p[:, 2]])
    valid = (p[:, 2] < -.1) & (xy[:, 0] >= 0) & (xy[:, 0] < width) & (xy[:, 1] >= 0) & (xy[:, 1] < height)
    light = .35+.65*np.clip(n @ np.array([.2, .3, 1.0]), 0, 1)
    colours = np.clip(data[:, 6:9]*light[:, None], 0, 255).astype('uint8')
    image = Image.new('RGB', (width, height), '#08121d'); draw = ImageDraw.Draw(image)
    for k in np.argsort(p[:, 2]):
        if valid[k]:
            a,b = xy[k]
            draw.ellipse((a-1.35,b-1.35,a+1.35,b+1.35), fill=tuple(colours[k]))
    return image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    root = args.directory; out = args.output or root/'media'; out.mkdir(parents=True, exist_ok=True)
    record = json.loads((root/'metrics.json').read_text()); frames = record['frames']
    base = Image.new('RGB', (1280,720), '#101c29'); d = ImageDraw.Draw(base)
    d.text((28,19), 'Mobile Point Fusion · saved offline reconstruction', font=font(31,True), fill='#edf5fc')
    model_label='Generated ellipsoid OBJ' if record.get('generated_model',True) else 'Provided ordered OBJ'
    d.text((28,62), model_label+' model · supplied camera poses · OpenGL ES 3 / Mesa llvmpipe', font=font(18), fill='#b1c5d8')
    d.text((28,103), 'Visible input points', font=font(22,True), fill='#edf5fc')
    d.text((462,103), 'Accumulated fused points', font=font(22,True), fill='#edf5fc')
    d.text((28,659), 'Actual saved shader/CPU results. Point samples, not a mesh. No real RGB-D, live camera or pose tracking.',font=font(17),fill='#b1c5d8')
    d.text((28,687), 'Original project: D. Ben-Hoda, G. Kamar, Aaron Wetzler, Y. Honen, Ron Kimmel · Updated offline adapter, 2026',font=font(15),fill='#b1c5d8')
    def render(i):
        f = frames[i]; angle = np.deg2rad(f['angle_degrees'])
        image = base.copy()
        image.paste(project(cloud(root/f'frames/{i:03d}-input.ply'),angle,410,460),(28,144))
        image.paste(project(cloud(root/f'frames/{i:03d}-model.ply'),angle,790,460,1.3),(462,144))
        draw=ImageDraw.Draw(image)
        text=f"View {i+1:02d}/{len(frames)} · input {f['input_points']:,} · model {f['model_points']:,} · merges {f['merged_total']:,} · removals {f['removed_total']:,}"
        draw.text((28,620),text,font=font(20),fill='#edf5fc')
        draw.text((1052,105),'6 fps playback',font=font(17),fill='#b1c5d8')
        draw.rectangle((0,715,round(1279*(i+1)/len(frames)),719),fill='#3abcc4')
        return image
    video=out/'mobile-point-fusion.mp4'
    command=['ffmpeg','-y','-f','rawvideo','-pixel_format','rgb24','-video_size','1280x720','-framerate','6','-i','pipe:0','-an','-c:v','libx264','-preset','medium','-crf','22','-pix_fmt','yuv420p','-movflags','+faststart','-metadata','title=Mobile Point Fusion: generated-model offline exercise','-metadata','comment=Actual saved GLES3/CPU fusion output. Generated ellipsoid OBJ observations and supplied camera poses. No live camera, real RGB-D, pose tracking or processing-performance claim. Original project D. Ben-Hoda, G. Kamar, Aaron Wetzler, Y. Honen, Ron Kimmel. Updated adapter, 2026.',str(video)]
    samples=[]
    with (out/'encode.log').open('w') as log:
        encoder=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=log)
        for i in range(len(frames)):
            image=render(i)
            encoder.stdin.write(image.tobytes())
            if i in (0,12,24,36,len(frames)-1):samples.append((i,image))
            if i==len(frames)-1:image.save(out/'demo.png')
        encoder.stdin.close()
        if encoder.wait():raise RuntimeError('FFmpeg failed; inspect encode.log')
    sheet=Image.new('RGB',(1280,3*380),'#101c29')
    for j,(i,image) in enumerate(samples):
        x,y=(j%2)*640,(j//2)*380
        sheet.paste(image.resize((640,360)),(x,y+20));ImageDraw.Draw(sheet).text((x+12,y+2),f'View {i+1}',font=font(14),fill='white')
    sheet.save(out/'contact-sheet.jpg',quality=92)
    gif=out/'demo.gif'
    gif_command=['ffmpeg','-v','error','-y','-i',str(video),'-filter_complex','[0:v]scale=800:450:flags=area,split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle','-loop','0',str(gif)]
    subprocess.run(gif_command,check=True)
    if gif.stat().st_size>=2_000_000:raise RuntimeError('GIF exceeds 2,000,000-byte cap')
    if video.stat().st_size>=10_000_000:raise RuntimeError('MP4 exceeds 10,000,000-byte cap')
    manifest={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'frames').glob('[0-9][0-9][0-9]-*.ply'))}
    provenance={'input':'Generated ellipsoid OBJ observations, known analytic camera orbit; not real depth data','visualization':'Pillow perspective projection of actual saved per-view PLY points and normals, depth-sorted point splats; no reconstruction performed by renderer','frame_count':len(frames),'fps':6,'seconds':len(frames)/6,'units':'arbitrary model units','metadata_source_sha256':hashlib.sha256((root/'metrics.json').read_bytes()).hexdigest(),'files':manifest,'video_command':command,'gif_command':gif_command,'media':{p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in (video,gif,out/'demo.png')}}
    (out/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(json.dumps(provenance['media'],indent=2))


if __name__=='__main__':main()
