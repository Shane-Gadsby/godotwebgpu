#!/usr/bin/env bash
S=/tmp/claude-1000/-mnt-109313D2109313D2-godot-editors-godotwebgpu/37660edb-e8da-40fd-a186-506b3daa7485/scratchpad/w74
R=$S/webres; mkdir -p $R; cd $S/bench
W="warm=600&frames=18&change_at=4&seq=1"
for b in base new; do
	for q in "up=fsr2&s0=1.0&s1=0.6" "up=fsr2&s0=0.6&s1=1.0" "up=taa&s0=1.0&s1=0.6" "up=taa&s0=0.6&s1=1.0" "up=bilinear&ae=1&s0=1.0&s1=0.6" "up=fsr2&ae=1&s0=1.0&s1=0.6"; do
		n=$(echo "$q" | tr '&=.' '___')
		node run_web.mjs --dir $S/web_$b --query "$q&$W" --out $R/${n}_$b.json --timeout 240 > $R/${n}_$b.log 2>&1 || echo "FAIL $b $q"
	done
done
echo WEB_DONE
