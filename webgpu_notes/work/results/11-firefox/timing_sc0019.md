| scene | config | launches ft/gt | fps (uninstrumented) | median ms/frame | p95 ms/frame | GPU busy ms/frame | renderer | console errors | browser on GPU | shaders |
|---|---|---|---|---|---|---|---|---|---|---|
| benchmark_pbr | sc19 | 6/6 | 998 (878..1038) | 0.940 (0.920..1.060) | 1.330 (1.260..1.620) | 0.09 (0.09..0.09) | Mobile | 36 | 6/6 | {"baked":354,"precompiled":0,"translated":20} |
| benchmark_pbr | tint | 6/6 | 1017 (974..1024) | 0.940 (0.920..0.960) | 1.300 (1.280..1.380) | 0.09 (0.09..0.09) | Mobile | 36 | 6/6 | {"baked":354,"precompiled":0,"translated":20} |
| benchmark_postfx | sc19 | 3/3 | 383 (363..387) | 2.500 (2.480..2.580) | 3.020 (2.960..3.500) | 0.57 (0.56..0.61) | Mobile | 18 | 3/3 | {"baked":366,"precompiled":0,"translated":30} |
| benchmark_postfx | tint | 3/3 | 349 (337..355) | 2.620 (2.600..2.660) | 3.680 (3.560..3.840) | 0.58 (0.58..0.59) | Mobile | 18 | 3/3 | {"baked":366,"precompiled":0,"translated":30} |
| benchmark_shadows | sc19 | 6/6 | 884 (861..896) | 1.080 (1.060..1.100) | 1.470 (1.420..1.520) | 0.19 (0.19..0.19) | Mobile | 36 | 6/6 | {"baked":354,"precompiled":0,"translated":20} |
| benchmark_shadows | tint | 6/6 | 900 (876..924) | 1.070 (1.040..1.100) | 1.430 (1.380..1.520) | 0.19 (0.19..0.19) | Mobile | 36 | 6/6 | {"baked":354,"precompiled":0,"translated":20} |
| demo_3d_platformer | sc19 | 3/3 | 770 (770..780) | 1.200 (1.200..1.220) | 1.640 (1.640..1.660) | 0.27 (0.27..0.27) | Forward+ | 0 | 3/3 | {"baked":602,"precompiled":0,"translated":0} |
| demo_3d_platformer | tint | 3/3 | 773 (762..780) | 1.200 (1.180..1.220) | 1.640 (1.620..1.700) | 0.27 (0.27..0.27) | Forward+ | 0 | 3/3 | {"baked":602,"precompiled":0,"translated":0} |
| heavy | sc19 | 3/3 | 149 (148..158) | 6.480 (6.140..6.560) | 7.840 (7.300..7.980) | 3.82 (3.77..3.85) | Forward+ | 0 | 3/3 | {"baked":762,"precompiled":0,"translated":0} |
| heavy | tint | 3/3 | 152 (102..161) | 6.360 (6.060..9.220) | 7.680 (6.940..14.460) | 3.88 (3.81..3.88) | Forward+ | 0 | 3/3 | {"baked":762,"precompiled":0,"translated":0} |
