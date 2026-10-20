/**************************************************************************/
/*  metahuman_shaders.h                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

// Shaders for the MetaHuman materials. They are kept to a few samples each: the target is 90 fps
// on an M1 or a mid-range laptop in a WebGPU export (webgpu_notes/TASKS.md §2.33). Normal maps
// arrive in OpenGL convention: the importer sets `normal_map_invert_y` on Unreal's textures.

namespace MetaHumanShaders {

// Head and body skin. SRMF: R specular, G roughness, B metallic (always 0 on skin), A unused.
inline constexpr const char *SKIN = R"(shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

uniform sampler2D base_color_map : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D normal_map : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2D srmf_map : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform sampler2D scatter_map : hint_default_white, filter_linear_mipmap, repeat_enable;
uniform sampler2D detail_normal_map : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float detail_normal_tiling = 16.0;
uniform float detail_normal_strength : hint_range(0.0, 1.0) = 0.5;
uniform float specular_scale : hint_range(0.0, 2.0) = 1.0;
uniform float roughness_scale : hint_range(0.0, 2.0) = 1.0;
uniform float subsurface_strength : hint_range(0.0, 1.0) = 1.0;

void fragment() {
	vec4 srmf = texture(srmf_map, UV);
	ALBEDO = texture(base_color_map, UV).rgb;
	// Whiteout blend of the tiling pore normal over the baked one, in tangent space.
	vec3 base_n = texture(normal_map, UV).rgb * 2.0 - 1.0;
	vec3 detail_n = texture(detail_normal_map, UV * detail_normal_tiling).rgb * 2.0 - 1.0;
	vec3 n = normalize(vec3(base_n.xy + detail_n.xy * detail_normal_strength, base_n.z));
	NORMAL_MAP = n * 0.5 + 0.5;
	ROUGHNESS = clamp(srmf.g * roughness_scale, 0.0, 1.0);
	SPECULAR = clamp(srmf.r * specular_scale, 0.0, 1.0);
	METALLIC = 0.0;
	SSS_STRENGTH = texture(scatter_map, UV).r * subsurface_strength;
}
)";

// Writing SSS_STRENGTH at all turns on Forward+'s screen-space scattering pass for the material,
// so the importer drops this line when subsurface scattering is off.
inline constexpr const char *SKIN_SSS_LINE = "\tSSS_STRENGTH = texture(scatter_map, UV).r * subsurface_strength;\n";

// Eyeball: sclera everywhere, the iris texture scaled into the disc around the front of the UV.
inline constexpr const char *EYE = R"(shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

uniform sampler2D sclera_color_map : source_color, filter_linear_mipmap_anisotropic;
uniform sampler2D iris_color_map : source_color, filter_linear_mipmap_anisotropic, repeat_disable;
uniform sampler2D sclera_normal_map : hint_normal, filter_linear_mipmap;
uniform sampler2D iris_normal_map : hint_normal, filter_linear_mipmap, repeat_disable;
uniform vec2 iris_center = vec2(0.5, 0.5);
uniform float iris_uv_radius = 0.15;
uniform float limbus_width = 0.012;
uniform float limbus_darkness : hint_range(0.0, 1.0) = 0.6;

void fragment() {
	vec2 d = UV - iris_center;
	float r = length(d);
	vec2 iris_uv = d / (2.0 * iris_uv_radius) + vec2(0.5);
	float iris = 1.0 - smoothstep(iris_uv_radius - limbus_width, iris_uv_radius, r);
	vec3 sclera = texture(sclera_color_map, UV).rgb;
	vec3 iris_color = texture(iris_color_map, iris_uv).rgb;
	float limbus = smoothstep(iris_uv_radius - 2.0 * limbus_width, iris_uv_radius, r) * (1.0 - smoothstep(iris_uv_radius, iris_uv_radius + limbus_width, r));
	ALBEDO = mix(sclera, iris_color, iris) * (1.0 - limbus * limbus_darkness);
	vec3 n = mix(texture(sclera_normal_map, UV).rgb, texture(iris_normal_map, iris_uv).rgb, iris);
	NORMAL_MAP = n;
	ROUGHNESS = mix(0.25, 0.4, iris);
	SPECULAR = 0.5;
}
)";

// The eye shell and the lacrimal edge: a wet layer that only adds a specular reflection.
inline constexpr const char *WET = R"(shader_type spatial;
render_mode blend_premul_alpha, depth_draw_never, cull_back, specular_schlick_ggx, shadows_disabled;

uniform float roughness : hint_range(0.0, 1.0) = 0.04;

void fragment() {
	ALBEDO = vec3(0.0);
	ALPHA = 0.0;
	ROUGHNESS = roughness;
	SPECULAR = 0.5;
}
)";

inline constexpr const char *TEETH = R"(shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

uniform sampler2D base_color_map : source_color, filter_linear_mipmap_anisotropic;
uniform sampler2D normal_map : hint_normal, filter_linear_mipmap;
// Teeth mask 002, red: depth into the mouth, 1 at the front teeth, 0 down the throat.
uniform sampler2D mouth_depth_map : hint_default_white, filter_linear_mipmap;
uniform float roughness : hint_range(0.0, 1.0) = 0.35;
uniform float mouth_darkening : hint_range(0.0, 1.0) = 0.85;

void fragment() {
	float depth = texture(mouth_depth_map, UV).r;
	// The inside of the mouth gets little light; no shadow map resolves that at mouth scale.
	float occlusion = mix(1.0 - mouth_darkening, 1.0, depth * depth);
	ALBEDO = texture(base_color_map, UV).rgb * occlusion;
	NORMAL_MAP = texture(normal_map, UV).rgb;
	ROUGHNESS = roughness;
	SPECULAR = 0.5 * occlusion;
	AO = occlusion;
}
)";

// The DNA's eyelash cards. Exports carry no lash texture, so strands are drawn procedurally across
// the card's U and thinned toward its tip along V. They are blended, not alpha-cut: strands a pixel
// or two wide alias badly without MSAA, and the cards are few and small.
inline constexpr const char *EYELASHES = R"(shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, diffuse_burley, specular_schlick_ggx, shadows_disabled;

uniform vec3 lash_color : source_color = vec3(0.012, 0.009, 0.008);
uniform float strand_count = 140.0;
uniform float strand_width : hint_range(0.05, 1.0) = 0.45;
uniform float opacity : hint_range(0.0, 1.0) = 0.9;
uniform bool tip_along_v_increasing = true;

void fragment() {
	float along = tip_along_v_increasing ? UV.y : 1.0 - UV.y;
	float cell = fract(UV.x * strand_count);
	float width = strand_width * (1.0 - 0.8 * along);
	// Antialias the strand edge over one pixel's worth of U.
	float aa = max(fwidth(UV.x * strand_count), 1e-4);
	float strand = 1.0 - smoothstep(width * 0.5 - aa, width * 0.5 + aa, abs(cell - 0.5));
	ALBEDO = lash_color;
	ROUGHNESS = 0.45;
	SPECULAR = 0.35;
	ALPHA = strand * opacity * (1.0 - 0.5 * along);
}
)";

// Clothing: Unreal's default garment. A flat fabric color with AO and stitches, and a tiling micro
// normal over the baked one.
inline constexpr const char *CLOTH = R"(shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

uniform vec3 fabric_color : source_color = vec3(0.59);
uniform vec3 stitch_color : source_color = vec3(0.59);
uniform sampler2D ambient_occlusion_map : hint_default_white, filter_linear_mipmap;
uniform sampler2D stitch_mask_map : hint_default_black, filter_linear_mipmap;
uniform sampler2D normal_map : hint_normal, filter_linear_mipmap_anisotropic;
uniform sampler2D micro_normal_map : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform float micro_scale = 80.0;
uniform float normal_strength = 1.0;
uniform float micro_normal_strength = 1.0;
uniform float roughness : hint_range(0.0, 1.0) = 0.85;

void fragment() {
	float ao = texture(ambient_occlusion_map, UV).r;
	float stitches = texture(stitch_mask_map, UV).r;
	ALBEDO = mix(fabric_color, stitch_color, stitches);
	vec3 base_n = texture(normal_map, UV).rgb * 2.0 - 1.0;
	vec3 micro_n = texture(micro_normal_map, UV * micro_scale).rgb * 2.0 - 1.0;
	vec3 n = normalize(vec3(base_n.xy * normal_strength + micro_n.xy * micro_normal_strength, base_n.z));
	NORMAL_MAP = n * 0.5 + 0.5;
	ROUGHNESS = roughness;
	SPECULAR = 0.35;
	AO = ao;
}
)";

} // namespace MetaHumanShaders
