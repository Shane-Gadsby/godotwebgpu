#!/usr/bin/env bash
# Native renders for task 33 under the gpu lease. Usage: render_all.sh <editor> <outdir> [cases...]
set -uo pipefail
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w33
W=/mnt/109313D2109313D2/godot-editors/godotwebgpu-33
ED="$1"; OUT="$2"; shift 2
mkdir -p "$OUT"
until "$W/webgpu_notes/work/lease.sh" take gpu worker-33 1; do sleep 30; done
r() { # r <name> <renderer> <WxH> args...
	local name=$1 rm=$2 res=$3; shift 3
	timeout 120 xvfb-run -a -s "-screen 0 4000x2300x24" "$ED" --path "$S/lensproj" --rendering-method "$rm" --resolution "$res" -s lens_test.gd -- out="$OUT/$name.$rm.png" "$@" 2>&1 | grep -E "LENSINFO|ERROR|SCRIPT" | head -5 | sed "s/^/$name.$rm: /"
}
for rm in ${RMS:-forward_plus mobile gl_compatibility}; do
	for c in "$@"; do
		case $c in
		default) r default_phys $rm 1280x720 scene=default attrs=physical
			r default_prac $rm 1280x720 scene=default attrs=practical
			r default_none $rm 1280x720 scene=default attrs=none ;;
		vig) r vig_f24_n1.4 $rm 1920x1080 scene=flat bg=0.5 attrs=physical vignetting_enabled=true frustum_focal_length=24 exposure_aperture=1.4 lens_max_aperture=1.4
			r vig_f24_n5.6 $rm 1920x1080 scene=flat bg=0.5 attrs=physical vignetting_enabled=true frustum_focal_length=24 exposure_aperture=5.6 lens_max_aperture=1.4
			r vig_prac $rm 1920x1080 scene=flat bg=0.5 attrs=practical vignetting_enabled=true vignetting_strength=1.5 vignetting_falloff=3.0 ;;
		dist) r dots_off $rm 1920x1080 scene=dots attrs=practical
			r dots_barrel $rm 1920x1080 scene=dots attrs=practical distortion_enabled=true distortion_k1=-0.1 distortion_k2=0.02
			r dots_barrel_nocrop $rm 1920x1080 scene=dots attrs=practical distortion_enabled=true distortion_k1=-0.1 distortion_k2=0.02 distortion_auto_crop=false
			r dots_pin $rm 1920x1080 scene=dots attrs=practical distortion_enabled=true distortion_k1=0.08 ;;
		ca) r ca_1080 $rm 1920x1080 scene=dots attrs=practical chromatic_aberration_enabled=true chromatic_aberration_strength=5
			r ca_540 $rm 960x540 scene=dots attrs=practical chromatic_aberration_enabled=true chromatic_aberration_strength=5 ;;
		diff) r edge_off $rm 3840x2160 scene=edge attrs=physical sensor_format=3 exposure_aperture=64
			r edge_f64 $rm 3840x2160 scene=edge attrs=physical sensor_format=3 exposure_aperture=64 diffraction_enabled=true
			r edge_f64_1080 $rm 1920x1080 scene=edge attrs=physical sensor_format=3 exposure_aperture=64 diffraction_enabled=true ;;
		ident) r ident_off $rm 1280x720 scene=default attrs=practical
			r ident_on $rm 1280x720 scene=default attrs=practical chromatic_aberration_enabled=true chromatic_aberration_strength=0.0001 ;;
		esac
	done
done
"$W/webgpu_notes/work/lease.sh" drop gpu worker-33
echo RENDER_DONE
