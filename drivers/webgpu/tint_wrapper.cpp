/**************************************************************************/
/*  tint_wrapper.cpp                                                      */
/**************************************************************************/
/*                       This file is part of:                            */
/*                           GODOT ENGINE                                 */
/*                      https://godotengine.org                           */
/**************************************************************************/
/* Compiled with C++20 in the Tint build environment.  Wraps Tint's       */
/* SPIR-V reader + WGSL writer behind a simple C-compatible interface     */
/* so that the main Godot driver code (C++17) never includes Tint headers.*/
/**************************************************************************/

#include "tint_wrapper.h"

#include "src/tint/api/tint.h"
#include "src/tint/lang/wgsl/writer/common/options.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

void tint_wrapper_initialize() {
	tint::Initialize();
}

char *tint_wrapper_spirv_to_wgsl(const uint32_t *p_spirv_words, size_t p_word_count, char **r_error) {
	std::vector<uint32_t> words(p_spirv_words, p_spirv_words + p_word_count);

	// Allow all WGSL extensions and language features so Tint can emit
	// constructs like readonly storage textures without validation errors.
	tint::wgsl::writer::Options wgsl_options;
	wgsl_options.allowed_features = tint::wgsl::AllowedFeatures::Everything();
	// Godot's GLSL shaders use textureSample/dpdx in non-uniform control flow
	// (valid in Vulkan, but WGSL requires uniform control flow for derivatives).
	// This inserts `diagnostic(off, derivative_uniformity)` in the output.
	wgsl_options.allow_non_uniform_derivatives = true;
	// Same relaxation for subgroup operations (cluster_render.glsl,
	// scene_forward_clustered.glsl — Forward+ only, see Task 9.1/9.2):
	// GLSL's GL_KHR_shader_subgroup_* calls appear in non-uniform control
	// flow too. Inserts `diagnostic(off, subgroup_uniformity)`.
	wgsl_options.allow_non_uniform_subgroup_operations = true;
	// glslang's SPIR-V output for a GLSL function whose if/else both return
	// (e.g. view_to_pos()-shaped helpers across ssao.glsl/ssil.glsl/
	// voxel_gi_debug.glsl/sdfgi_direct_light.glsl/gi.glsl/scene_forward_
	// clustered.glsl/canvas.glsl) always includes a merge block Tint's IR
	// requires a terminator for; Tint's WGSL writer emits a synthetic
	// trailing `return <T>();` there rather than proving it provably
	// unreachable itself. Confirmed via SPIRV-Tools' own dead-branch-elim +
	// block-merge + aggressive-DCE passes (already used elsewhere in this
	// pipeline, see eliminate_dead_resources() in spirv_preprocess.cpp) that
	// this isn't a removable dead SPIR-V block -- the trailing return is a
	// Tint WGSL-writer code-generation choice, not a real code-elimination
	// gap, so a SPIR-V-level preprocessing pass can't help. Dawn then
	// (correctly, per WGSL semantics) warns "code is unreachable" on that
	// return every time this pattern occurs -- cosmetic, but frequent enough
	// across ordinary Godot shaders to be console noise on every page load.
	// Inserts `diagnostic(off, chromium.unreachable_code)`, the same
	// mechanism as the two relaxations above, rather than the shader
	// silently disabling *all* Dawn compilation diagnostics.
	wgsl_options.disable_unreachable_code_warning = true;

	auto result = tint::SpirvToWgsl(words, wgsl_options);
	if (result != tint::Success) {
		if (r_error) {
			const std::string &reason = result.Failure().reason;
			char *err = (char *)malloc(reason.size() + 1);
			if (err) {
				memcpy(err, reason.c_str(), reason.size() + 1);
			}
			*r_error = err;
		}
		return nullptr;
	}

	const std::string &wgsl = result.Get();
	char *out = (char *)malloc(wgsl.size() + 1);
	if (!out) {
		return nullptr;
	}
	memcpy(out, wgsl.c_str(), wgsl.size() + 1);
	return out;
}
