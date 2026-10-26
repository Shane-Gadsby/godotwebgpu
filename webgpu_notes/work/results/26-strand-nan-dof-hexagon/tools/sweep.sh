#!/usr/bin/env bash
# sweep.sh <editor> <project> <outdir>: the user's 42-position sweep; distance 4.70-4.90 at height 1.64,
# height 1.55-1.75 at distance 4.80. Per position: non-finite pixels before DOF (probe), black pixels in the face area.
set -u
E=$1; P=$2; O=$3; rm -rf $O; mkdir -p $O
steps="hide=AcknowledgementsPopup,set=DistanceSlider:step:0.01,set=HeightSlider:step:0.01,nanprobe,HeightSlider=1.64"
for i in $(seq 0 20); do d=$(python3 -c "print(f'{4.70+$i*0.01:.2f}')"); steps="$steps,DistanceSlider=$d,eval=HeightSlider.value,report"; done
steps="$steps,DistanceSlider=4.80"
for i in $(seq 0 20); do h=$(python3 -c "print(f'{1.55+$i*0.01:.2f}')"); steps="$steps,HeightSlider=$h,eval=DistanceSlider.value,report"; done
cd $P && SCENE=res://scenes/camera_studio.tscn OUT=$O EMBED=1 STEPS="$steps" timeout 900 $E --path . --rendering-driver vulkan --resolution 1280x720 --script $P/tools/ui_harness_probe.gd > $O/log.txt 2>&1
python3 - "$O" <<'PY'
import sys,re,glob,os
from PIL import Image
o=sys.argv[1]
log=open(o+'/log.txt').read().splitlines()
rows=[];cur={}
for l in log:
    m=re.match(r'STEP (DistanceSlider|HeightSlider)=([\d.]+)',l)
    if m: cur[m.group(1)]=m.group(2)
    m=re.match(r'NANPROBE frames=\d+ nonfinite_pixels=(\d+)',l)
    if m: rows.append((cur.get('DistanceSlider','?'),cur.get('HeightSlider','?'),int(m.group(1))))
shots=sorted(glob.glob(o+'/*_report_vp.png'), key=lambda p:int(os.path.basename(p).split('_')[0]))
bad=0
out=[]
for (d,h,n),p in zip(rows,shots):
    im=Image.open(p).convert('RGB').crop((500,220,760,460)); px=im.load()
    blk=sum(1 for y in range(im.size[1]) for x in range(im.size[0]) if sum(px[x,y])<6)
    fail = n>0 or blk>50
    bad+=fail
    out.append(f"d={d} h={h} nonfinite_preDOF={n} black_face_px={blk} {'FAIL' if fail else 'ok'}")
open(o+'/sweep.txt','w').write("\n".join(out)+f"\npositions={len(rows)} failing={bad}\n")
print("\n".join(l for l in out if 'FAIL' in l)); print(f"positions={len(rows)} failing={bad}")
PY
