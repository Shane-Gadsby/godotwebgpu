thirdparty/README.md line: `spirv-cross-wgsl` patch 0005 (`patches/0005-storage-image-16bit-norm-formats.patch`):
`image_format_to_wgsl` maps R16/Rg16/Rgba16 and their Snorm variants to r16unorm ... rgba16snorm (CanvasSdf's r16snorm
storage image threw "Storage image format is not supported in WGSL", leaving that stage unbaked in every export).
Harness for TESTS docs: `webgpu_tests/spirv_cross_eval/project_export.sh` (copies a project, exports with the editor
under a memory cap, captures every baked stage's SPIR-V through a wrapper converter) and `project_eval.py` (both
backends, then compare_interface.py / validate_wgsl.mjs).
