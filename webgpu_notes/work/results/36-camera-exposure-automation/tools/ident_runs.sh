#!/usr/bin/env bash
# ident_runs.sh <editor> <outdir>: the bit-identical cases, 8-bit PNG, all three renderers (caller holds gpu).
W=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w36
E=$1 O=$2
mkdir -p $O
for rm in forward_plus mobile gl_compatibility; do
	$W/r.sh $E $rm 1280x720 scene=default attrs=none caps=12 out=$O/def_none_$rm | grep -v xic
	$W/r.sh $E $rm 1280x720 scene=default attrs=practical caps=12 out=$O/def_prac_$rm | grep -v xic
	$W/r.sh $E $rm 1280x720 scene=default attrs=physical caps=12 out=$O/def_phys_$rm | grep -v xic
	$W/r.sh $E $rm 1280x720 scene=default attrs=practical caps=12 out=$O/def_ae_$rm auto_exposure_enabled=true | grep -v xic
	$W/r.sh $E $rm 1280x720 scene=default attrs=practical caps=12 out=$O/def_wb_$rm white_balance_temperature=4500 white_balance_tint=10 | grep -v xic
	$W/r.sh $E $rm 1280x720 scene=awb attrs=practical caps=12 out=$O/awb_ae_$rm auto_exposure_enabled=true | grep -v xic
done
echo IDENT_DONE
