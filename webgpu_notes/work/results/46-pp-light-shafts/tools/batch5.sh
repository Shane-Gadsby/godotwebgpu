#!/usr/bin/env bash
S=<scratch>
NEW=/mnt/109313D2109313D2/godot-editors/godotwebgpu-46/bin/godot.linuxbsd.editor.x86_64
BASE=/mnt/109313D2109313D2/godot-editors/godotwebgpu-46base/bin/godot.linuxbsd.editor.x86_64
R=$S/render.sh; O=$S/out6; A="python3 $S/analyze.py"
mkdir -p $O
for rm in forward_plus mobile gl_compatibility; do
  echo "=== defaults $rm (base 7b978e6baa vs work/46, shafts off)"
  for sc in "scene=bars lit=1 light_energy=2 glow=1" "scene=bars lit=1 light_energy=2 sun=3 halo=1 msaa=4"; do
    $R $BASE $rm 1280x720 $O/def_base_$rm.png $sc >/dev/null
    $R $NEW  $rm 1280x720 $O/def_new_$rm.png $sc >/dev/null
    $A diff $O/def_base_$rm.png $O/def_new_$rm.png
  done
  echo "=== behind the camera $rm (on vs off)"
  $R $NEW $rm 1280x720 $O/behind_off_$rm.png scene=bars sun=0.7 behind=1 >/dev/null
  $R $NEW $rm 1280x720 $O/behind_on_$rm.png scene=bars sun=0.7 behind=1 shafts=1 thr=0.5 >/dev/null
  $A diff $O/behind_off_$rm.png $O/behind_on_$rm.png
  echo "=== enabled, no light opted in $rm (vs off)"
  $R $NEW $rm 1280x720 $O/nl_off_$rm.png scene=bars sun=0 halo=0.7 >/dev/null
  $R $NEW $rm 1280x720 $O/nl_on_$rm.png scene=bars sun=0 halo=0.7 shafts=1 thr=0.5 light=0 >/dev/null
  $A diff $O/nl_off_$rm.png $O/nl_on_$rm.png
done
sed "s|O=\$S/out4|O=\$S/out6|" $S/batch4.sh > $S/batch4b.sh; bash $S/batch4b.sh
for rm in forward_plus mobile gl_compatibility; do echo "== edge $rm"; python3 $S/edge2.py $O $rm; done
echo "=== msaa 4x"
for rm in forward_plus mobile; do
  $R $NEW $rm 1280x720 $O/msaa_off_$rm.png scene=bars sun=0 halo=0.7 msaa=4 >/dev/null
  $R $NEW $rm 1280x720 $O/msaa_on_$rm.png scene=bars sun=0 halo=0.7 msaa=4 shafts=1 thr=0.5 decay=1.5 >/dev/null
  echo -n "$rm "; $A model $O/msaa_off_$rm.png $O/msaa_on_$rm.png --decay 1.5 --thr 0.5
done
echo "=== resolution (forward_plus)"
for res in 640x360 1920x1080; do
  $R $NEW forward_plus $res $O/res_off_$res.png scene=none sx=0 sy=0 sun=0.7 >/dev/null
  $R $NEW forward_plus $res $O/res_on_$res.png scene=none sx=0 sy=0 sun=0.7 shafts=1 thr=0.5 decay=2 >/dev/null
  echo $res; $A falloff $O/res_off_$res.png $O/res_on_$res.png --sx 0 --sy 0 --decay 2 --M -1 | grep -v "^  D="
done
echo "=== ortho"
for rm in forward_plus mobile gl_compatibility; do
  $R $NEW $rm 1280x720 $O/ortho_on_$rm.png scene=bars sun=0 halo=0 shafts=1 thr=0.05 decay=1 ortho=1 sx=0.6 sy=0.4 >/dev/null
done
echo "=== timing 1920x1080 (off / enabled no light / on), 2 runs"
for rm in forward_plus mobile gl_compatibility; do
  for run in 1; do
    for cfg in "off:shafts=0" "nolight:shafts=1 light=0" "on:shafts=1"; do
      n=${cfg%%:*}; a=${cfg#*:}
      $R $NEW $rm 1920x1080 $O/t_${rm}_$n.png scene=bars lit=1 light_energy=2 sun=3 halo=1 thr=1 frames=620 timing=1 $a | sed "s/^/$rm $n run$run /"
    done
  done
done
echo "=== web"
bash $S/webrun.sh
