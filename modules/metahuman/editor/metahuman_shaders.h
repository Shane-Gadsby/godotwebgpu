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
// Repeats of the pore map per UV unit. The importer sets it from the mesh's UV density so one repeat
// covers the same skin area on every mesh (the body's UVs are ~3.4x coarser than the face's).
uniform float detail_normal_tiling = 32.0;
uniform float detail_normal_strength : hint_range(0.0, 1.0) = 0.5;
// View depth (m) over which the pore detail fades out. Unreal drops it past the face's LOD 0; the
// importer sets these from the mesh's LOD 0 range.
uniform float detail_fade_begin = 0.75;
uniform float detail_fade_end = 1.5;
uniform float specular_scale : hint_range(0.0, 2.0) = 1.0;
uniform float roughness_scale : hint_range(0.0, 2.0) = 1.0;
uniform float subsurface_strength : hint_range(0.0, 1.0) = 1.0;

void fragment() {
	vec4 srmf = texture(srmf_map, UV);
	ALBEDO = texture(base_color_map, UV).rgb;
	// Whiteout blend of the tiling pore normal over the baked one, in tangent space. Both maps are
	// RG-compressed (blue samples as 0), so z is rebuilt from xy.
	vec2 base_xy = texture(normal_map, UV).rg * 2.0 - 1.0;
	vec2 detail_xy = texture(detail_normal_map, UV * detail_normal_tiling).rg * 2.0 - 1.0;
	detail_xy *= detail_normal_strength * (1.0 - smoothstep(detail_fade_begin, detail_fade_end, -VERTEX.z));
	float base_z = sqrt(max(0.0, 1.0 - dot(base_xy, base_xy)));
	float detail_z = sqrt(max(0.0, 1.0 - dot(detail_xy, detail_xy)));
	vec3 n = normalize(vec3(base_xy + detail_xy, base_z * detail_z));
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
// How deep the iris sits under the cornea, in UV units.
uniform float iris_depth = 0.02;

void fragment() {
	// The iris sits behind the cornea: offset its lookup against the view, in tangent space, for the
	// parallax Unreal gets from refraction.
	vec3 view_ts = vec3(dot(VIEW, TANGENT), dot(VIEW, BINORMAL), dot(VIEW, NORMAL));
	vec2 parallax = view_ts.xy / max(view_ts.z, 0.25) * iris_depth;
	vec2 d0 = UV - iris_center;
	float iris_area = 1.0 - smoothstep(iris_uv_radius * 0.9, iris_uv_radius * 1.2, length(d0));
	vec2 d = d0 - parallax * iris_area;
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

// Groom strands (eyebrows, groom eyelashes, fuzz) as camera-facing ribbons; see
// MetaHumanHair::build_strands() for the vertex layout. Each ribbon is at least min_pixel_width wide
// and its alpha drops by the widening, so thin strands thin out at distance instead of flickering.
inline constexpr const char *HAIR_STRANDS = R"(shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, diffuse_lambert_wrap, specular_schlick_ggx, shadows_disabled;

uniform float max_width = 0.0001;
uniform float width_scale = 1.0;
uniform float root_scale = 1.0;
uniform float tip_scale = 0.5;
// Below ~2 px a ribbon covers pixel centers only now and then and breaks into dots.
uniform float min_pixel_width = 2.0;
uniform vec3 root_color : source_color = vec3(0.02, 0.015, 0.01);
uniform vec3 tip_color : source_color = vec3(0.02, 0.015, 0.01);
// Per-channel factors on the root-to-tip color, picked per strand.
uniform vec3 variation_a = vec3(1.0);
uniform vec3 variation_b = vec3(1.0);
uniform float use_groom_color = 0.0;
uniform float roughness : hint_range(0.0, 1.0) = 0.4;
uniform float opacity : hint_range(0.0, 1.0) = 1.0;
// Coverage of a sub-pixel strand is width / pixel; raised to this power. 1 is physically right, but
// one blended pass of strands that thin reads far lighter than the groom does with temporal
// accumulation, so brows and lashes default to 0.5.
uniform float coverage_exponent : hint_range(0.1, 1.0) = 0.5;
// Brows and lashes lie within a fraction of a millimeter of the skin; pulled this far toward the
// camera they stop depth-fighting it (seen as stipple at portrait distance).
uniform float depth_offset = 0.0005;
// Strands in the shadow map are drawn this much wider than they are, standing in for the clumped
// strands a decimated groom leaves out.
uniform float shadow_width_scale = 2.0;
uniform float specular : hint_range(0.0, 1.0) = 0.3;
uniform float root_darkening : hint_range(0.0, 1.0) = 1.0;

varying float v_coverage;
varying float v_t;
varying float v_seed;
varying vec3 v_groom_color;

// Hair simulation (MetaHumanHairSimulator): per guide point, how far it moved from where skinning
// puts it, in skeleton space; CUSTOM0 = (guide a, guide b, weight of a, position along the guide).
uniform sampler2D guide_offsets : hint_default_black, filter_nearest, repeat_disable;
uniform int guide_point_count = 0;

vec3 guide_offset(float p_guide, float p_t) {
	float x = clamp(p_t, 0.0, 1.0) * float(guide_point_count - 1);
	int i = int(floor(x));
	int j = min(i + 1, guide_point_count - 1);
	vec3 a = texelFetch(guide_offsets, ivec2(i, int(p_guide)), 0).xyz;
	vec3 b = texelFetch(guide_offsets, ivec2(j, int(p_guide)), 0).xyz;
	return mix(a, b, x - float(i));
}

vec3 simulated_offset(vec4 p_binding) {
	if (guide_point_count < 2) {
		return vec3(0.0);
	}
	return mix(guide_offset(p_binding.y, p_binding.w), guide_offset(p_binding.x, p_binding.w), p_binding.z);
}

void vertex() {
	VERTEX += simulated_offset(CUSTOM0);
	// Worked in world space (camera facing, pixel width), applied in model space: VERTEX here is the
	// skinned point, relative to the skeleton.
	mat3 to_world = mat3(MODEL_MATRIX);
	vec3 world = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 t = normalize(to_world * TANGENT);
	// In a shadow pass the "camera" is the light's, often orthographic: face its view axis, and keep the
	// true width (the pixel clamp below is for the screen; there it would make giant shadow casters).
	vec3 to_camera = IN_SHADOW_PASS ? normalize(INV_VIEW_MATRIX[2].xyz) : normalize(CAMERA_POSITION_WORLD - world);
	vec3 side = cross(t, to_camera);
	float side_length = length(side);
	side = side_length > 1e-5 ? side / side_length : normalize(cross(t, vec3(0.0, 1.0, 0.0)));
	float width = UV2.x * max_width * width_scale * mix(root_scale, tip_scale, UV.y);
	float depth = max(distance(CAMERA_POSITION_WORLD, world), 1e-3);
	// abs(): the projection's Y can be flipped, making [1][1] negative.
	float pixel = depth * 2.0 / (abs(PROJECTION_MATRIX[1][1]) * VIEWPORT_SIZE.y);
	float drawn = IN_SHADOW_PASS ? width * shadow_width_scale : max(width, pixel * min_pixel_width);
	v_coverage = clamp(width / drawn, 0.0, 1.0);
	mat3 to_model = inverse(to_world);
	VERTEX += to_model * (side * (UV.x - 0.5) * drawn + to_camera * depth_offset);
	vec3 n = normalize(cross(side, t));
	n = dot(n, to_camera) < 0.0 ? -n : n;
	NORMAL = normalize(to_model * n);
	TANGENT = normalize(to_model * t);
	BINORMAL = normalize(to_model * side);
	v_t = UV.y;
	v_seed = UV2.y;
	v_groom_color = COLOR.rgb;
}

void fragment() {
	vec3 base = mix(root_color, tip_color, v_t) * mix(variation_a, variation_b, v_seed);
	// Strands near the scalp sit under the rest of the hair and get less light.
	float root_light = mix(root_darkening, 1.0, smoothstep(0.0, 0.35, v_t));
	ALBEDO = mix(base, v_groom_color, use_groom_color) * root_light;
	AO = root_light;
	ROUGHNESS = roughness;
	SPECULAR = specular;
	ANISOTROPY = 0.6;
	ALPHA = pow(v_coverage, coverage_exponent) * opacity * (1.0 - 0.6 * smoothstep(0.7, 1.0, v_t));
}
)";

// A groom baked to one opaque mesh (MetaHumanHair::build_shell): TANGENT follows the strands, which
// Godot's anisotropic GGX stretches the highlight along; a fine noise across the flow draws strands.
inline constexpr const char *HAIR_SHELL = R"(shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_lambert_wrap, specular_schlick_ggx;

uniform vec3 hair_color : source_color = vec3(0.1, 0.07, 0.05);
uniform float use_groom_color = 0.0;
uniform float roughness : hint_range(0.0, 1.0) = 0.7;
uniform float specular : hint_range(0.0, 1.0) = 0.2;
uniform float anisotropy : hint_range(-1.0, 1.0) = 0.8;
uniform float strand_frequency = 350.0;
uniform float strand_contrast : hint_range(0.0, 1.0) = 0.2;
// How far the strand noise tilts the normal across the flow, breaking the highlight into streaks.
uniform float strand_tilt : hint_range(0.0, 1.0) = 0.12;

varying vec3 v_position;
varying vec3 v_binormal;
varying vec3 v_groom_color;

float hash(float p) {
	return fract(sin(p * 12.9898) * 43758.5453);
}

float strand_noise(float x) {
	float i = floor(x);
	return mix(hash(i), hash(i + 1.0), smoothstep(0.0, 1.0, fract(x)));
}

// Hair simulation (MetaHumanHairSimulator): per guide point, how far it moved from where skinning
// puts it, in skeleton space; CUSTOM0 = (guide a, guide b, weight of a, position along the guide).
uniform sampler2D guide_offsets : hint_default_black, filter_nearest, repeat_disable;
uniform int guide_point_count = 0;

vec3 guide_offset(float p_guide, float p_t) {
	float x = clamp(p_t, 0.0, 1.0) * float(guide_point_count - 1);
	int i = int(floor(x));
	int j = min(i + 1, guide_point_count - 1);
	vec3 a = texelFetch(guide_offsets, ivec2(i, int(p_guide)), 0).xyz;
	vec3 b = texelFetch(guide_offsets, ivec2(j, int(p_guide)), 0).xyz;
	return mix(a, b, x - float(i));
}

vec3 simulated_offset(vec4 p_binding) {
	if (guide_point_count < 2) {
		return vec3(0.0);
	}
	return mix(guide_offset(p_binding.y, p_binding.w), guide_offset(p_binding.x, p_binding.w), p_binding.z);
}

void vertex() {
	VERTEX += simulated_offset(CUSTOM0);
	v_position = VERTEX;
	v_binormal = cross(NORMAL, TANGENT.xyz);
	v_groom_color = COLOR.rgb;
}

void fragment() {
	// The across-the-strands coordinate is taken per fragment: per vertex it changes by many strands
	// across a triangle and interpolates into a maze.
	float across = dot(v_position, normalize(v_binormal)) * strand_frequency;
	// Fade the fine strands out where they get finer than a pixel or two; they alias into a maze.
	float fine_detail = clamp(1.5 - fwidth(across), 0.0, 1.0);
	float fine = mix(0.5, strand_noise(across), fine_detail);
	float coarse = strand_noise(across * 0.17 + 31.0);
	float strands = mix(fine, coarse, 0.4);
	vec3 base = mix(hair_color, v_groom_color, use_groom_color);
	ALBEDO = base * (1.0 - strand_contrast + strand_contrast * 2.0 * strands);
	// Tilt the normal across the strands (tangent space: x along the flow, y across it).
	NORMAL_MAP = normalize(vec3(0.0, (strands - 0.5) * 2.0 * strand_tilt, 1.0)) * 0.5 + 0.5;
	ROUGHNESS = clamp(roughness + (0.5 - fine) * 0.2, 0.0, 1.0);
	SPECULAR = specular;
	ANISOTROPY = anisotropy;
	AO = 0.55 + 0.45 * strands;
}
)";

} // namespace MetaHumanShaders
