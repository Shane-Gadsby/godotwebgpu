/**************************************************************************/
/*  export_plugin.cpp                                                     */
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

#include "export_plugin.h"

#include "logo_svg.gen.h"
#include "run_icon_svg.gen.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/pck_packer.h"
#include "core/io/zip_io.h"
#include "core/os/os.h"
#include "editor/editor_string_names.h"
#include "editor/export/editor_export.h"
#include "editor/export/shader_baker/shader_baker_export_plugin.h"
#include "editor/file_system/editor_paths.h"
#include "editor/import/resource_importer_texture_settings.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/resources/image_texture.h"
#include "servers/audio/audio_server.h"
#include "servers/rendering/renderer_rd/renderer_scene_render_rd.h"
#include "servers/rendering/renderer_rd/shader_rd.h"

#include "modules/modules_enabled.gen.h" // IWYU pragma: keep. For mono.
#include "modules/svg/image_loader_svg.h"

Error EditorExportPlatformWeb::_extract_template(const String &p_template, const String &p_dir, const String &p_name, bool pwa) {
	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);
	unzFile pkg = unzOpen2(p_template.utf8().get_data(), &io);

	if (!pkg) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Could not open template for export: \"%s\"."), p_template));
		return ERR_FILE_NOT_FOUND;
	}

	if (unzGoToFirstFile(pkg) != UNZ_OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Invalid export template: \"%s\"."), p_template));
		unzClose(pkg);
		return ERR_FILE_CORRUPT;
	}

	do {
		//get filename
		unz_file_info info;
		char fname[16384];
		unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);

		String file = String::utf8(fname);

		// Skip folders.
		if (file.ends_with("/")) {
			continue;
		}

		// Skip service worker and offline page if not exporting pwa.
		if (!pwa && (file == "godot.service.worker.js" || file == "godot.offline.html")) {
			continue;
		}
		Vector<uint8_t> data;
		data.resize(info.uncompressed_size);

		//read
		unzOpenCurrentFile(pkg);
		unzReadCurrentFile(pkg, data.ptrw(), data.size());
		unzCloseCurrentFile(pkg);

		//write
		String dst = p_dir.path_join(file.replace("godot", p_name));
		Ref<FileAccess> f = FileAccess::open(dst, FileAccess::WRITE);
		if (f.is_null()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Could not write file: \"%s\"."), dst));
			unzClose(pkg);
			return ERR_FILE_CANT_WRITE;
		}
		f->store_buffer(data.ptr(), data.size());

	} while (unzGoToNextFile(pkg) == UNZ_OK);
	unzClose(pkg);
	return OK;
}

Error EditorExportPlatformWeb::_write_or_error(const uint8_t *p_content, int p_size, String p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), p_path));
		return ERR_FILE_CANT_WRITE;
	}
	f->store_buffer(p_content, p_size);
	return OK;
}

void EditorExportPlatformWeb::_replace_strings(const HashMap<String, String> &p_replaces, Vector<uint8_t> &r_template) {
	String str_template = String::utf8(reinterpret_cast<const char *>(r_template.ptr()), r_template.size());
	String out;
	Vector<String> lines = str_template.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String current_line = lines[i];
		for (const KeyValue<String, String> &E : p_replaces) {
			current_line = current_line.replace(E.key, E.value);
		}
		out += current_line + "\n";
	}
	CharString cs = out.utf8();
	r_template.resize(cs.length());
	for (int i = 0; i < cs.length(); i++) {
		r_template.write[i] = cs[i];
	}
}

// The loading screen's stages, in the order a player sees them. Each stage is
// three export options -- `loading_screen/<key>/text`, `.../show_text` and
// `.../show_spinner` -- and the shell (misc/dist/html/full-size.html) receives
// them as the `$GODOT_LOADING_SCREEN` JSON object, keyed by `key`. The defaults
// are the strings the shell used to hard-code; a stage's `text` is shown as-is,
// except that "downloading" gets the percentage and "compiling_shaders" the
// shader count appended when the shell knows them. The keys are part of the
// shell contract: a custom shell looks stages up by these names.
struct WebLoadingScreenStage {
	const char *key;
	const char *default_text;
	bool show_text;
	bool show_spinner;
};

// The spinner defaults match the previous behavior: it only appears once the
// engine is running (from "Starting engine..." on), where the main thread can
// block for seconds and a CSS animation is the only thing that keeps moving.
static const WebLoadingScreenStage web_loading_screen_stages[] = {
	{ "initializing_webgpu", "Initializing WebGPU...", true, false },
	{ "downloading", "Downloading...", true, false },
	{ "starting_engine", "Starting engine...", true, true },
	{ "initializing_engine", "Initializing engine...", true, true },
	{ "loading_extensions", "Loading extensions...", true, true },
	{ "starting_display", "Starting display...", true, true },
	{ "starting_renderer", "Starting renderer...", true, true },
	{ "starting_audio", "Starting audio...", true, true },
	{ "loading_translations", "Loading translations...", true, true },
	{ "loading_fonts", "Loading fonts...", true, true },
	{ "registering_types", "Registering types...", true, true },
	{ "finalizing_setup", "Finalizing setup...", true, true },
	{ "loading_autoloads", "Loading autoloads...", true, true },
	{ "loading_main_scene", "Loading main scene...", true, true },
	{ "rendering_first_frame", "Rendering first frame...", true, true },
	{ "compiling_shaders", "Compiling shaders...", true, true },
};

void EditorExportPlatformWeb::_fix_html(Vector<uint8_t> &p_html, const Ref<EditorExportPreset> &p_preset, const String &p_name, bool p_debug, BitField<EditorExportPlatform::DebugFlags> p_flags, const Vector<SharedObject> p_shared_objects, const Dictionary &p_file_sizes, const Dictionary &p_texture_packs) {
	// Engine.js config
	Dictionary config;
	Array libs;
	for (int i = 0; i < p_shared_objects.size(); i++) {
		libs.push_back(p_shared_objects[i].path.get_file());
	}
	Vector<String> flags = gen_export_flags(p_flags & (~DEBUG_FLAG_DUMB_CLIENT));
	Array args;
	for (int i = 0; i < flags.size(); i++) {
		args.push_back(flags[i]);
	}
	config["canvasResizePolicy"] = p_preset->get("html/canvas_resize_policy");
	config["experimentalVK"] = p_preset->get("html/experimental_virtual_keyboard");
	config["focusCanvas"] = p_preset->get("html/focus_canvas_on_start");
	config["fullscreenOnFirstTap"] = p_preset->get("html/fullscreen_on_first_tap");
	config["gdextensionLibs"] = libs;
	config["executable"] = p_name;
	if (p_preset->get("variant/thread_support") && p_preset->get("variant/separate_render_thread")) {
		// Rendering on its own thread (DisplayServerWeb's render thread, webgpu_notes/TASKS.md 2.5). The
		// engine falls back to the Safe model where the browser cannot use WebGPU from a worker.
		args.push_back("--render-thread");
		args.push_back("separate");
	}
	config["args"] = args;
	config["fileSizes"] = p_file_sizes;
	// Present only when _save_shader_pack() wrote one. The shell preloads it next
	// to the main pack and the engine mounts it on its own (ProjectSettings::_setup).
	if (p_file_sizes.has(p_name + ".shaders.pck")) {
		config["shaderPack"] = p_name + ".shaders.pck";
	}
	// Present only when _save_texture_packs() wrote them: one pack per VRAM
	// compression family, keyed by family. The shell fetches the one the device
	// samples (or "bc" when it samples none, which the engine then decompresses)
	// as <main pack>.textures.pck, which the engine mounts beside the main pack.
	if (!p_texture_packs.is_empty()) {
		config["texturePacks"] = p_texture_packs;
	}
	// Present only when _save_shader_extras() wrote one; fetched after start.
	if (p_file_sizes.has(p_name + ".shaders_extra.bin")) {
		config["shaderExtras"] = p_name + ".shaders_extra.bin";
	}
	config["ensureCrossOriginIsolationHeaders"] = (bool)p_preset->get("progressive_web_app/ensure_cross_origin_isolation_headers");

	// Pass the rendering driver so the HTML shell can pre-initialize WebGPU if needed.
	String rendering_method = get_project_setting(p_preset, "rendering/renderer/rendering_method.web");
	if (rendering_method == "forward_plus" || rendering_method == "mobile") {
		config["renderingDriver"] = "webgpu";
	} else {
		config["renderingDriver"] = "opengl3";
	}

	config["godotPoolSize"] = p_preset->get("threads/godot_pool_size");
	config["emscriptenPoolSize"] = p_preset->get("threads/emscripten_pool_size");

	String head_include;
	if (p_preset->get("html/export_icon")) {
		head_include += "<link id=\"-gd-engine-icon\" rel=\"icon\" type=\"image/png\" href=\"" + p_name + ".icon.png\" />\n";
		head_include += "<link rel=\"apple-touch-icon\" href=\"" + p_name + ".apple-touch-icon.png\"/>\n";
	}
	if (p_preset->get("progressive_web_app/enabled")) {
		head_include += "<link rel=\"manifest\" href=\"" + p_name + ".manifest.json\">\n";
		config["serviceWorker"] = p_name + ".service.worker.js";
	}

	// Loading screen stages (see web_loading_screen_stages above). Every stage is
	// always present so the shell never has to fall back to its own defaults.
	Dictionary loading_screen;
	for (const WebLoadingScreenStage &stage : web_loading_screen_stages) {
		const String prefix = String("loading_screen/") + stage.key + "/";
		Dictionary stage_config;
		stage_config["text"] = p_preset->get(prefix + "text");
		stage_config["showText"] = p_preset->get(prefix + "show_text");
		stage_config["showSpinner"] = p_preset->get(prefix + "show_spinner");
		loading_screen[stage.key] = stage_config;
	}

	// Replaces HTML string
	const String str_config = Variant(config).to_json_string();
	const String str_loading_screen = Variant(loading_screen).to_json_string();
	const String custom_head_include = p_preset->get("html/head_include");
	HashMap<String, String> replaces;
	replaces["$GODOT_URL"] = p_name + ".js";
	replaces["$GODOT_PROJECT_NAME"] = get_project_setting(p_preset, "application/config/name");
	replaces["$GODOT_HEAD_INCLUDE"] = head_include + custom_head_include;
	replaces["$GODOT_CONFIG"] = str_config;
	replaces["$GODOT_LOADING_SCREEN"] = str_loading_screen;
	replaces["$GODOT_SPLASH_COLOR"] = "#" + Color(get_project_setting(p_preset, "application/boot_splash/bg_color")).to_html(false);

	Vector<String> godot_splash_classes;
	godot_splash_classes.push_back("show-image--" + String(get_project_setting(p_preset, "application/boot_splash/show_image")));
	RSE::SplashStretchMode boot_splash_stretch_mode = get_project_setting(p_preset, "application/boot_splash/stretch_mode");
	godot_splash_classes.push_back("fullsize--" + String(((boot_splash_stretch_mode != RSE::SplashStretchMode::SPLASH_STRETCH_MODE_DISABLED) ? "true" : "false")));
	godot_splash_classes.push_back("use-filter--" + String(get_project_setting(p_preset, "application/boot_splash/use_filter")));
	replaces["$GODOT_SPLASH_CLASSES"] = String(" ").join(godot_splash_classes);
	replaces["$GODOT_SPLASH"] = p_name + ".png";

	if (p_preset->get("variant/thread_support")) {
		replaces["$GODOT_THREADS_ENABLED"] = "true";
	} else {
		replaces["$GODOT_THREADS_ENABLED"] = "false";
	}

	_replace_strings(replaces, p_html);
}

Error EditorExportPlatformWeb::_add_manifest_icon(const Ref<EditorExportPreset> &p_preset, const String &p_path, const String &p_icon, int p_size, Array &r_arr) {
	const String name = p_path.get_file().get_basename();
	const String icon_name = vformat("%s.%dx%d.png", name, p_size, p_size);
	const String icon_dest = p_path.get_base_dir().path_join(icon_name);

	Ref<Image> icon;
	if (!p_icon.is_empty()) {
		Error err = OK;
		icon = _load_icon_or_splash_image(p_icon, &err);
		if (err != OK || icon.is_null() || icon->is_empty()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Icon Creation"), vformat(TTR("Could not read file: \"%s\"."), p_icon));
			return err;
		}
		if (icon->get_width() != p_size || icon->get_height() != p_size) {
			icon->resize(p_size, p_size);
		}
	} else {
		icon = _get_project_icon(p_preset);
		icon->resize(p_size, p_size);
	}
	const Error err = icon->save_png(icon_dest);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Icon Creation"), vformat(TTR("Could not write file: \"%s\"."), icon_dest));
		return err;
	}
	Dictionary icon_dict;
	icon_dict["sizes"] = vformat("%dx%d", p_size, p_size);
	icon_dict["type"] = "image/png";
	icon_dict["src"] = icon_name;
	r_arr.push_back(icon_dict);
	return err;
}

Error EditorExportPlatformWeb::_build_pwa(const Ref<EditorExportPreset> &p_preset, const String p_path, const Vector<SharedObject> &p_shared_objects) {
	String proj_name = get_project_setting(p_preset, "application/config/name");
	if (proj_name.is_empty()) {
		proj_name = "Godot Game";
	}

	// Service worker
	const String dir = p_path.get_base_dir();
	const String name = p_path.get_file().get_basename();
	bool extensions = (bool)p_preset->get("variant/extensions_support");
	bool ensure_crossorigin_isolation_headers = (bool)p_preset->get("progressive_web_app/ensure_cross_origin_isolation_headers");
	HashMap<String, String> replaces;
	replaces["___GODOT_VERSION___"] = String::num_int64(OS::get_singleton()->get_unix_time()) + "|" + String::num_int64(OS::get_singleton()->get_ticks_usec());
	replaces["___GODOT_NAME___"] = proj_name.substr(0, 16);
	replaces["___GODOT_OFFLINE_PAGE___"] = name + ".offline.html";
	replaces["___GODOT_ENSURE_CROSSORIGIN_ISOLATION_HEADERS___"] = ensure_crossorigin_isolation_headers ? "true" : "false";

	// Files cached during worker install.
	Array cache_files = {
		name + ".html",
		name + ".js",
		name + ".offline.html"
	};
	if (p_preset->get("html/export_icon")) {
		cache_files.push_back(name + ".icon.png");
		cache_files.push_back(name + ".apple-touch-icon.png");
	}

	cache_files.push_back(name + ".audio.worklet.js");
	replaces["___GODOT_CACHE___"] = Variant(cache_files).to_json_string();

	// Heavy files that are cached on demand.
	Array opt_cache_files = {
		name + ".wasm",
		name + ".pck"
	};
	if (FileAccess::exists(dir.path_join(name + ".shaders.pck"))) {
		opt_cache_files.push_back(name + ".shaders.pck");
	}
	if (FileAccess::exists(dir.path_join(name + ".shaders_extra.bin"))) {
		opt_cache_files.push_back(name + ".shaders_extra.bin");
	}
	for (const char *suffix : { ".textures_bc.pck", ".textures_etc2_astc.pck" }) {
		if (FileAccess::exists(dir.path_join(name + suffix))) {
			opt_cache_files.push_back(name + suffix);
		}
	}
	if (extensions) {
		opt_cache_files.push_back(name + ".side.wasm");
		for (int i = 0; i < p_shared_objects.size(); i++) {
			opt_cache_files.push_back(p_shared_objects[i].path.get_file());
		}
	}
	replaces["___GODOT_OPT_CACHE___"] = Variant(opt_cache_files).to_json_string();

	const String sw_path = dir.path_join(name + ".service.worker.js");
	Vector<uint8_t> sw;
	{
		Ref<FileAccess> f = FileAccess::open(sw_path, FileAccess::READ);
		if (f.is_null()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("PWA"), vformat(TTR("Could not read file: \"%s\"."), sw_path));
			return ERR_FILE_CANT_READ;
		}
		sw.resize(f->get_length());
		f->get_buffer(sw.ptrw(), sw.size());
	}
	_replace_strings(replaces, sw);
	Error err = _write_or_error(sw.ptr(), sw.size(), dir.path_join(name + ".service.worker.js"));
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}

	// Custom offline page
	const String offline_page = p_preset->get("progressive_web_app/offline_page");
	if (!offline_page.is_empty()) {
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		const String offline_dest = dir.path_join(name + ".offline.html");
		err = da->copy(ProjectSettings::get_singleton()->globalize_path(offline_page), offline_dest);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("PWA"), vformat(TTR("Could not read file: \"%s\"."), offline_dest));
			return err;
		}
	}

	// Manifest
	const char *modes[4] = { "fullscreen", "standalone", "minimal-ui", "browser" };
	// Indexed by DisplayServerEnums::ScreenOrientation.
	const char *orientations[7] = { "landscape-primary", "portrait-primary", "landscape-secondary", "portrait-secondary", "landscape", "portrait", "any" };
	const int display = CLAMP(int(p_preset->get("progressive_web_app/display")), 0, 3);
	const int orientation = CLAMP(int(_get_orientation(p_preset)), 0, 6);

	Dictionary manifest;
	manifest["name"] = proj_name;
	manifest["start_url"] = "./" + name + ".html";
	manifest["display"] = String::utf8(modes[display]);
	manifest["orientation"] = String::utf8(orientations[orientation]);
	manifest["background_color"] = "#" + p_preset->get("progressive_web_app/background_color").operator Color().to_html(false);

	Array icons_arr;
	const String icon144_path = p_preset->get("progressive_web_app/icon_144x144");
	err = _add_manifest_icon(p_preset, p_path, icon144_path, 144, icons_arr);
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}
	const String icon180_path = p_preset->get("progressive_web_app/icon_180x180");
	err = _add_manifest_icon(p_preset, p_path, icon180_path, 180, icons_arr);
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}
	const String icon512_path = p_preset->get("progressive_web_app/icon_512x512");
	err = _add_manifest_icon(p_preset, p_path, icon512_path, 512, icons_arr);
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}
	manifest["icons"] = icons_arr;

	CharString cs = Variant(manifest).to_json_string().utf8();
	err = _write_or_error((const uint8_t *)cs.get_data(), cs.length(), dir.path_join(name + ".manifest.json"));
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}

	return OK;
}

void EditorExportPlatformWeb::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	if (p_preset->get("vram_texture_compression/for_desktop")) {
		r_features->push_back("s3tc");
		r_features->push_back("bptc");
	}
	if (p_preset->get("vram_texture_compression/for_mobile")) {
		r_features->push_back("etc2");
		r_features->push_back("astc");
	}
	if (p_preset->get("variant/thread_support").operator bool()) {
		r_features->push_back("threads");
	} else {
		r_features->push_back("nothreads");
	}
	if (p_preset->get("variant/extensions_support").operator bool()) {
		r_features->push_back("web_extensions");
	} else {
		r_features->push_back("web_noextensions");
	}
	if (p_preset->get("shader_baker/enabled")) {
		// Only meaningful for the WebGPU renderer (rendering/rendering_device/driver.web
		// == "webgpu"); ShaderBakerExportPlugin::_initialize_container_format() itself
		// no-ops via ShaderBakerExportPluginPlatformWebGPU::matches_driver() otherwise.
		r_features->push_back("shader_baker");
	}
	r_features->push_back("wasm32");
}

void EditorExportPlatformWeb::get_export_options(List<ExportOption> *r_options) const {
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/debug", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/release", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "variant/extensions_support"), false)); // GDExtension support.
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "variant/thread_support"), false, true)); // Thread support (i.e. run with or without COEP/COOP headers).
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "variant/separate_render_thread"), true)); // Render on the web render thread (threads only).
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "vram_texture_compression/for_desktop"), true)); // S3TC
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "vram_texture_compression/for_mobile"), false)); // ETC or ETC2, depending on renderer
	// With both families exported, ship each in a pack of its own and let the
	// browser fetch only the one its GPU samples, instead of every player
	// downloading both. See _save_texture_packs().
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "vram_texture_compression/separate_packs"), true));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/export_icon"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "html/custom_html_shell", PROPERTY_HINT_FILE, "*.html"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "html/head_include", PROPERTY_HINT_MULTILINE_TEXT, "monospace,no_wrap"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "html/canvas_resize_policy", PROPERTY_HINT_ENUM, "None,Project,Adaptive"), 2));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/focus_canvas_on_start"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/fullscreen_on_first_tap"), true));
	// Index 0 follows display/window/handheld/orientation; index N is ScreenOrientation N - 1.
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "html/orientation", PROPERTY_HINT_ENUM, "Project Setting,Landscape,Portrait,Reverse Landscape,Reverse Portrait,Sensor Landscape,Sensor Portrait,Sensor"), 0));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/experimental_virtual_keyboard"), false));

	// One text field and two tickboxes per loading-screen stage; the inspector
	// shows them as a "Loading Screen" section with a subsection per stage.
	for (const WebLoadingScreenStage &stage : web_loading_screen_stages) {
		const String prefix = String("loading_screen/") + stage.key + "/";
		r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, prefix + "text"), String(stage.default_text)));
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, prefix + "show_text"), stage.show_text));
		r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, prefix + "show_spinner"), stage.show_spinner));
	}

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "progressive_web_app/enabled"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "progressive_web_app/ensure_cross_origin_isolation_headers"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/offline_page", PROPERTY_HINT_FILE, "*.html"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "progressive_web_app/display", PROPERTY_HINT_ENUM, "Fullscreen,Standalone,Minimal UI,Browser"), 1));
	// Superseded by html/orientation, which the manifest now follows; kept hidden so a preset that
	// set it still exports the orientation it asked for (see _get_orientation()).
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "progressive_web_app/orientation", PROPERTY_HINT_ENUM, "Any,Landscape,Portrait"), 0));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/icon_144x144", PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/icon_180x180", PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/icon_512x512", PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::COLOR, "progressive_web_app/background_color", PROPERTY_HINT_COLOR_NO_ALPHA), Color()));

	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "threads/emscripten_pool_size"), 8));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "threads/godot_pool_size"), 4));

	// Precompiles every shader's SPIR-V to WGSL at export time (see
	// editor/shader/shader_baker/shader_baker_export_plugin_platform_webgpu.cpp)
	// so the exported .pck ships ready-to-use WGSL and the browser never
	// converts a baked shader itself. Only takes effect when the project's
	// rendering/rendering_device/driver.web setting is "webgpu"; harmless
	// no-op otherwise. Requires the editor to have been built with
	// `webgpu=yes` (see drivers/webgpu/README.md) so this container format
	// is available at all — if it wasn't, export proceeds without baking and
	// shaders fall back to the existing runtime SPIR-V -> WGSL translation.
	//
	// Defaults on: for WebGPU the cost of *not* baking is paid by every player
	// as a multi-second freeze on first load, which is not a sane thing to
	// leave behind an unticked box. Turning it off is still fully supported and
	// means exactly what it says — nothing is baked, and the running game
	// translates its shaders in the browser on demand — which is
	// what you want while iterating, since baking every declared variant of
	// every shader the project uses is the slowest part of an export.
	//
	// Note that flipping this default only reaches presets that don't already
	// store a value for it: EditorExport::update_export_presets() applies an
	// option's default solely when `!preset->has(option_name)`
	// (editor/export/editor_export.cpp), and export_presets.cfg persists every
	// option. Presets created before this default changed therefore keep their
	// stored `false` — the export warning below is what tells their owner.
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "shader_baker/enabled"), true));
	// Ship the baked cache as <name>.shaders.pck instead of inside <name>.pck.
	// The shader cache is two thirds of a baked pck and changes only when the
	// shaders do, so a deploy that uploads whole files (scp, S3, a zip) then
	// re-uploads the game's own 15 MB on a code change instead of 50 MB. Needs
	// a template from the same build as the editor, which the bake needs anyway.
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "shader_baker/separate_pack"), true));
	// Leave out the shader groups this export can never select: the target's
	// unsupported paths (FP16 scene shaders, VRS, image-atomic fog), multiview
	// without XR, and Forward+'s advanced passes (motion vectors, separate
	// specular, lightmaps, VoxelGI, SDFGI) when nothing in the project uses
	// them. A group the game turns on anyway compiles from source at that point.
	// See webgpu_notes/TASKS.md §2.27.
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "shader_baker/variant_pruning", PROPERTY_HINT_ENUM, "Disabled,Automatic"), 1));
}

bool EditorExportPlatformWeb::get_export_option_visibility(const EditorExportPreset *p_preset, const String &p_option) const {
	bool advanced_options_enabled = p_preset->are_advanced_options_enabled();
	if (p_option == "custom_template/debug" || p_option == "custom_template/release") {
		return advanced_options_enabled;
	}

	if (p_option == "threads/godot_pool_size" || p_option == "threads/emscripten_pool_size" || p_option == "variant/separate_render_thread") {
		return p_preset->get("variant/thread_support").operator bool();
	}

	if (p_option == "vram_texture_compression/separate_packs") {
		return p_preset->get("vram_texture_compression/for_desktop").operator bool() && p_preset->get("vram_texture_compression/for_mobile").operator bool();
	}

	if (p_option == "shader_baker/separate_pack" || p_option == "shader_baker/variant_pruning") {
		return p_preset->get("shader_baker/enabled").operator bool();
	}

	if (p_option == "progressive_web_app/orientation") {
		return false; // Superseded by html/orientation.
	}

	return true;
}

DisplayServerEnums::ScreenOrientation EditorExportPlatformWeb::_get_orientation(const Ref<EditorExportPreset> &p_preset, bool *r_overrides_project) const {
	// One place sets the orientation for both the page (the engine locks it in fullscreen, through
	// display/window/handheld/orientation) and the PWA manifest: html/orientation. A preset from
	// before that option may only have the PWA one, which then still applies to both.
	const int html_orientation = p_preset->get("html/orientation");
	if (html_orientation >= 1 && html_orientation <= DisplayServerEnums::SCREEN_SENSOR + 1) {
		if (r_overrides_project) {
			*r_overrides_project = true;
		}
		return DisplayServerEnums::ScreenOrientation(html_orientation - 1);
	}
	const int pwa_orientation = p_preset->get("progressive_web_app/orientation"); // Any, Landscape, Portrait.
	if (pwa_orientation == 1 || pwa_orientation == 2) {
		if (r_overrides_project) {
			*r_overrides_project = true;
		}
		return pwa_orientation == 1 ? DisplayServerEnums::SCREEN_SENSOR_LANDSCAPE : DisplayServerEnums::SCREEN_SENSOR_PORTRAIT;
	}
	if (r_overrides_project) {
		*r_overrides_project = false;
	}
	const int project_orientation = get_project_setting(p_preset, "display/window/handheld/orientation");
	return DisplayServerEnums::ScreenOrientation(CLAMP(project_orientation, 0, int(DisplayServerEnums::SCREEN_SENSOR)));
}

HashMap<String, Variant> EditorExportPlatformWeb::get_custom_project_settings(const Ref<EditorExportPreset> &p_preset) const {
	HashMap<String, Variant> settings;
	bool overrides_project = false;
	const DisplayServerEnums::ScreenOrientation orientation = _get_orientation(p_preset, &overrides_project);
	if (overrides_project) {
		// The engine applies it at startup (main.cpp) through DisplayServerWeb::screen_set_orientation().
		settings["display/window/handheld/orientation"] = int(orientation);
	}
	return settings;
}

String EditorExportPlatformWeb::get_name() const {
	return "Web";
}

String EditorExportPlatformWeb::get_os_name() const {
	return "Web";
}

Ref<Texture2D> EditorExportPlatformWeb::get_logo() const {
	return logo;
}

bool EditorExportPlatformWeb::has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug) const {
#ifdef MODULE_MONO_ENABLED
	// Don't check for additional errors, as this particular error cannot be resolved.
	r_error += TTR("Exporting to Web is currently not supported in Godot 4 when using C#/.NET. Use Godot 3 to target Web with C#/Mono instead.") + "\n";
	r_error += TTR("If this project does not use C#, use a non-C# editor build to export the project.") + "\n";
	return false;
#else

	String err;
	bool valid = false;
	bool extensions = (bool)p_preset->get("variant/extensions_support");
	bool thread_support = (bool)p_preset->get("variant/thread_support");

	// Look for export templates (first official, and if defined custom templates).
	bool dvalid = exists_export_template(_get_template_name(extensions, thread_support, true), &err);
	bool rvalid = exists_export_template(_get_template_name(extensions, thread_support, false), &err);

	if (p_preset->get("custom_template/debug") != "") {
		dvalid = FileAccess::exists(p_preset->get("custom_template/debug"));
		if (!dvalid) {
			err += TTR("Custom debug template not found.") + "\n";
		}
	}
	if (p_preset->get("custom_template/release") != "") {
		rvalid = FileAccess::exists(p_preset->get("custom_template/release"));
		if (!rvalid) {
			err += TTR("Custom release template not found.") + "\n";
		}
	}

	valid = dvalid || rvalid;
	r_missing_templates = !valid;

	if (!err.is_empty()) {
		r_error = err;
	}

	return valid;
#endif // !MODULE_MONO_ENABLED
}

bool EditorExportPlatformWeb::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	String err;
	bool valid = true;

	// Validate the project configuration.

	if (p_preset->get("vram_texture_compression/for_mobile")) {
		if (!ResourceImporterTextureSettings::should_import_etc2_astc()) {
			if (EditorNode::is_cmdline_mode()) {
				err += TTR("ETC2/ASTC texture compression is required for Web export. In the Project Settings, search for 'ETC2' in the search field, or enable 'Advanced Settings', and go to Rendering > Textures > VRAM Compression to enable 'Import ETC2 ASTC'.") + "\n";
			}
			valid = false;
		}
	}

	// Warn if WebGPU rendering is selected.
	String rendering_method = get_project_setting(p_preset, "rendering/renderer/rendering_method.web");
	if (rendering_method == "forward_plus" || rendering_method == "mobile") {
		err += TTR("WebGPU rendering is experimental. Ensure your target browsers support WebGPU.") + "\n";

		// Without shader baking, every specialized (spec-constant-patched) shader
		// variant -- lighting, shadow, and material feature combinations, which
		// Godot's 3D pipeline uses heavily -- gets translated from SPIR-V to WGSL
		// on the *player's* machine, synchronously on the main thread, the
		// first time each variant is drawn. This is the dominant cause of the
		// multi-second startup stall/jank documented in
		// webgpu_notes/TASKS.md §4.21. Baking bakes ahead of time everything
		// it can (see drivers/webgpu/README.md and webgpu_notes/TASKS.md's shader
		// baking task), and since Task 25 it covers specialization-constant
		// pipeline variants too: those no longer produce their own shader
		// modules at all, so they reuse the baked base module through WebGPU
		// pipeline constants. What baking still cannot cover is a shader that
		// does not exist at export time, e.g. one built at runtime from
		// Shader.new() + set_code().
		if (!p_preset->get("shader_baker/enabled").operator bool()) {
			err += TTR("\"Shader Baker\" (shader_baker/enabled) is disabled. Exported WebGPU games will do significant shader-compilation work on the player's machine at load time, which can look like a freeze on first launch. Enabling it moves most of that work to export time instead.") + "\n";
		} else if (RendererSceneRenderRD::get_singleton() == nullptr) {
			// ShaderBakerExportPlugin::_is_active() silently requires the *editor's own
			// currently running* renderer to be RD-based (Vulkan/Metal/D3D12) -- if the
			// editor itself is running under GL Compatibility, exporting bakes zero
			// shaders with no error at all (see webgpu_notes/TASKS.md's shader baking
			// task for how easy this was to hit and how confusing the silent failure was).
			err += TTR("\"Shader Baker\" is enabled, but the editor is not currently running with a RenderingDevice-based renderer (Vulkan/Metal/D3D12). Baking will silently produce zero baked shaders. Restart the editor with --rendering-driver vulkan (or switch Advanced Settings > Rendering > Renderer to Forward+/Mobile) before exporting.") + "\n";
		}
	}

	if (!err.is_empty()) {
		r_error = err;
	}

	return valid;
}

List<String> EditorExportPlatformWeb::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> list;
	list.push_back("html");
	return list;
}

Error EditorExportPlatformWeb::_save_shader_pack(const Ref<EditorExportPreset> &p_preset, const String &p_path, bool &r_written) {
	r_written = false;

	// The shader baker is one export plugin among the registered ones; when the
	// preset asked for a separate pack it kept its files instead of add_file()-ing
	// them into the main pack, and hands them over exactly once here.
	Vector<ShaderBakerExportPlugin::SeparatePackFile> files;
	for (const Ref<EditorExportPlugin> &plugin : EditorExport::get_singleton()->get_export_plugins()) {
		ShaderBakerExportPlugin *baker = Object::cast_to<ShaderBakerExportPlugin>(plugin.ptr());
		if (baker != nullptr) {
			files.append_array(baker->take_separate_pack_files());
		}
	}
	if (files.is_empty()) {
		return OK;
	}

	// Sorted by path so the pack's bytes depend only on its contents: a deploy
	// that compares or hashes whole files sees the same file for the same shaders.
	struct ByPath {
		bool operator()(const ShaderBakerExportPlugin::SeparatePackFile &p_a, const ShaderBakerExportPlugin::SeparatePackFile &p_b) const {
			return p_a.path < p_b.path;
		}
	};
	files.sort_custom<ByPath>();

	Ref<PCKPacker> packer;
	packer.instantiate();
	Error err = packer->pck_start(p_path);
	if (err != OK) {
		return err;
	}
	for (const ShaderBakerExportPlugin::SeparatePackFile &file : files) {
		err = packer->add_file_from_buffer(file.path, file.data);
		if (err != OK) {
			return err;
		}
	}
	err = packer->flush(false);
	if (err != OK) {
		return err;
	}

	r_written = true;
	print_verbose(vformat("Web export: wrote %d baked shader cache files to '%s'.", files.size(), p_path));
	return OK;
}

// Set for the duration of one save_pack() that splits textures out.
struct WebTexturePackFiles {
	Vector<ShaderBakerExportPlugin::SeparatePackFile> bc;
	Vector<ShaderBakerExportPlugin::SeparatePackFile> etc2_astc;
};
static WebTexturePackFiles *web_texture_pack_files = nullptr;

bool EditorExportPlatformWeb::_uses_texture_packs(const Ref<EditorExportPreset> &p_preset) const {
	// Not with an encrypted pack: the side packs are written unencrypted, the
	// same rule as the separate shader pack.
	return p_preset->get("vram_texture_compression/for_desktop").operator bool() && p_preset->get("vram_texture_compression/for_mobile").operator bool() && p_preset->get("vram_texture_compression/separate_packs").operator bool() && !p_preset->get_enc_pck();
}

Error EditorExportPlatformWeb::_save_pack_file_split_textures(const Ref<EditorExportPreset> &p_preset, void *p_userdata, const SaveFileInfo &p_info, const Vector<uint8_t> &p_data) {
	if (web_texture_pack_files != nullptr) {
		// Imported files carry their compression family before the extension:
		// res://.godot/imported/<name>-<hash>.s3tc.ctex (also .ctexarray, .ctex3d, ...).
		const String path = simplify_path(p_info.path);
		if (path.trim_prefix("res://").begins_with(".godot/imported/")) {
			const String family = path.get_basename().get_extension();
			Vector<ShaderBakerExportPlugin::SeparatePackFile> *target = nullptr;
			if (family == "s3tc" || family == "bptc") {
				target = &web_texture_pack_files->bc;
			} else if (family == "etc2" || family == "astc") {
				target = &web_texture_pack_files->etc2_astc;
			}
			if (target != nullptr) {
				ShaderBakerExportPlugin::SeparatePackFile file;
				file.path = path;
				file.data = p_data;
				target->push_back(file);
				return OK;
			}
		}
	}
	return _save_pack_file(p_preset, p_userdata, p_info, p_data);
}

Error EditorExportPlatformWeb::_save_texture_packs(const String &p_base_path, Dictionary &r_texture_packs) {
	ERR_FAIL_NULL_V(web_texture_pack_files, ERR_BUG);
	struct Family {
		const char *key;
		const char *suffix;
		Vector<ShaderBakerExportPlugin::SeparatePackFile> *files;
	};
	const Family families[] = {
		{ "bc", ".textures_bc.pck", &web_texture_pack_files->bc },
		{ "etc2_astc", ".textures_etc2_astc.pck", &web_texture_pack_files->etc2_astc },
	};
	for (const Family &family : families) {
		const String pack_path = p_base_path + family.suffix;
		if (FileAccess::exists(pack_path)) {
			DirAccess::remove_absolute(pack_path);
		}
		if (family.files->is_empty()) {
			continue;
		}
		Ref<PCKPacker> packer;
		packer.instantiate();
		Error err = packer->pck_start(pack_path);
		if (err != OK) {
			return err;
		}
		for (const ShaderBakerExportPlugin::SeparatePackFile &file : *family.files) {
			err = packer->add_file_from_buffer(file.path, file.data);
			if (err != OK) {
				return err;
			}
		}
		err = packer->flush(false);
		if (err != OK) {
			return err;
		}
		r_texture_packs[family.key] = pack_path.get_file();
		print_verbose(vformat("Web export: wrote %d imported textures to '%s'.", family.files->size(), pack_path));
	}
	return OK;
}

Error EditorExportPlatformWeb::_save_shader_extras(const String &p_path, bool &r_written) {
	r_written = false;

	Vector<ShaderBakerExportPlugin::SeparatePackFile> files;
	for (const Ref<EditorExportPlugin> &plugin : EditorExport::get_singleton()->get_export_plugins()) {
		ShaderBakerExportPlugin *baker = Object::cast_to<ShaderBakerExportPlugin>(plugin.ptr());
		if (baker != nullptr) {
			files.append_array(baker->take_extras_pack_files());
		}
	}
	if (files.is_empty()) {
		return OK;
	}

	// Keyed by the path relative to the shader cache, which is what the runtime
	// looks up (ShaderRD::_open_from_extras()); sorted for stable bytes.
	const String cache_prefix = ShaderRD::get_shader_cache_user_dir() + "/";
	Vector<Pair<String, Vector<uint8_t>>> entries;
	for (const ShaderBakerExportPlugin::SeparatePackFile &file : files) {
		ERR_CONTINUE_MSG(!file.path.begins_with(cache_prefix), vformat("Shader extras file '%s' is outside the shader cache.", file.path));
		entries.push_back(Pair<String, Vector<uint8_t>>(file.path.trim_prefix(cache_prefix), file.data));
	}
	struct ByPath {
		bool operator()(const Pair<String, Vector<uint8_t>> &p_a, const Pair<String, Vector<uint8_t>> &p_b) const {
			return p_a.first < p_b.first;
		}
	};
	entries.sort_custom<ByPath>();

	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_null()) {
		return ERR_CANT_CREATE;
	}
	f->store_buffer(ShaderRD::build_shader_cache_extras(entries));
	r_written = true;
	print_verbose(vformat("Web export: wrote %d deferred shader cache files to '%s'.", entries.size(), p_path));
	return OK;
}

Error EditorExportPlatformWeb::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags, bool p_notify) {
	ExportNotifier notifier(*this, p_preset, p_debug, p_path, p_flags, p_notify);

	const String custom_debug = p_preset->get("custom_template/debug");
	const String custom_release = p_preset->get("custom_template/release");
	const String custom_html = p_preset->get("html/custom_html_shell");
	const bool export_icon = p_preset->get("html/export_icon");
	const bool pwa = p_preset->get("progressive_web_app/enabled");

	const String base_dir = p_path.get_base_dir();
	const String base_path = p_path.get_basename();
	const String base_name = p_path.get_file().get_basename();

	if (!DirAccess::exists(base_dir)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Target folder does not exist or is inaccessible: \"%s\""), base_dir));
		return ERR_FILE_BAD_PATH;
	}

	// Find the correct template
	String template_path = p_debug ? custom_debug : custom_release;
	template_path = template_path.strip_edges();
	if (template_path.is_empty()) {
		bool extensions = (bool)p_preset->get("variant/extensions_support");
		bool thread_support = (bool)p_preset->get("variant/thread_support");
		template_path = find_export_template(_get_template_name(extensions, thread_support, p_debug));
	}

	if (!template_path.is_empty() && !FileAccess::exists(template_path)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Template file not found: \"%s\"."), template_path));
		return ERR_FILE_NOT_FOUND;
	}

	// Export pck and shared objects
	Vector<SharedObject> shared_objects;
	String pck_path = base_path + ".pck";
	Dictionary texture_packs;
	Error error;
	if (_uses_texture_packs(p_preset)) {
		WebTexturePackFiles split_files;
		web_texture_pack_files = &split_files;
		error = save_pack(p_preset, p_debug, pck_path, &shared_objects, _save_pack_file_split_textures);
		if (error == OK) {
			error = _save_texture_packs(base_path, texture_packs);
		}
		web_texture_pack_files = nullptr;
	} else {
		for (const char *suffix : { ".textures_bc.pck", ".textures_etc2_astc.pck" }) {
			if (FileAccess::exists(base_path + suffix)) {
				DirAccess::remove_absolute(base_path + suffix);
			}
		}
		error = save_pack(p_preset, p_debug, pck_path, &shared_objects);
	}
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), pck_path));
		return error;
	}

	// The baked shader cache, when the preset keeps it out of the main pack.
	// A stale one from an earlier export is removed either way: the shell only
	// preloads what this export's config lists, but a leftover file next to a
	// pack that now contains the shaders itself is a 35 MB trap for a deploy.
	const String shader_pack_path = base_path + ".shaders.pck";
	if (FileAccess::exists(shader_pack_path)) {
		DirAccess::remove_absolute(shader_pack_path);
	}
	bool shader_pack_written = false;
	error = _save_shader_pack(p_preset, shader_pack_path, shader_pack_written);
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), shader_pack_path));
		return error;
	}

	// The shader groups the baker deferred, fetched by the shell after start.
	const String shader_extras_path = base_path + ".shaders_extra.bin";
	if (FileAccess::exists(shader_extras_path)) {
		DirAccess::remove_absolute(shader_extras_path);
	}
	bool shader_extras_written = false;
	error = _save_shader_extras(shader_extras_path, shader_extras_written);
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), shader_extras_path));
		return error;
	}

	{
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		for (int i = 0; i < shared_objects.size(); i++) {
			String dst = base_dir.path_join(shared_objects[i].path.get_file());
			error = da->copy(shared_objects[i].path, dst);
			if (error != OK) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), shared_objects[i].path.get_file()));
				return error;
			}
		}
	}

	// Extract templates.
	error = _extract_template(template_path, base_dir, base_name, pwa);
	if (error) {
		// Message is supplied by the subroutine method.
		return error;
	}

	// A template built before the shell learned to preload the shader pack
	// would start the game without it: nothing fails, the shaders are simply
	// not found and every one is compiled from source at load, which is the
	// slow start the bake exists to prevent. The shell's config key is the
	// cheapest proof the template is new enough; the engine's own mount code
	// shipped in the same change, so one check covers both.
	if (shader_pack_written) {
		String shell_js = FileAccess::get_file_as_string(base_path + ".js");
		if (!shell_js.is_empty() && !shell_js.contains("shaderPack")) {
			add_message(EXPORT_MESSAGE_WARNING, TTR("Export"), TTR("The export template predates separate shader packs, so the baked shaders in \"index.shaders.pck\" will not be loaded at runtime and every shader will be compiled from source on first launch. Rebuild the web export templates from this editor's commit, or disable \"shader_baker/separate_pack\".").replace("index.shaders.pck", shader_pack_path.get_file()));
		}
	}

	// Sounds on the web default to Sample playback, which the browser plays outside AudioServer's mixer, so bus
	// effects never process them. Say so at export rather than leave a missing reverb to be reported as a bug.
	if (int(GLOBAL_GET("audio/general/default_playback_type.web")) == 1) { // 1: Sample.
		AudioServer *audio_server = AudioServer::get_singleton();
		PackedStringArray effect_buses;
		for (int i = 0; i < audio_server->get_bus_count(); i++) {
			if (audio_server->is_bus_bypassing_effects(i)) {
				continue;
			}
			for (int j = 0; j < audio_server->get_bus_effect_count(i); j++) {
				if (audio_server->is_bus_effect_enabled(i, j)) {
					effect_buses.push_back("\"" + audio_server->get_bus_name(i) + "\"");
					break;
				}
			}
		}
		if (!effect_buses.is_empty()) {
			add_message(EXPORT_MESSAGE_WARNING, TTR("Audio"), vformat(TTR("The effects on bus %s will not apply to sounds that use Sample playback, the web default (\"audio/general/default_playback_type.web\"). Set \"playback_type\" to Stream on the players that need them; Stream playback mixes them as on desktop."), String(", ").join(effect_buses)));
		}
	}

	// Parse generated file sizes (pck and wasm, to help show a meaningful loading bar).
	Dictionary file_sizes;
	Ref<FileAccess> f = FileAccess::open(pck_path, FileAccess::READ);
	if (f.is_valid()) {
		file_sizes[pck_path.get_file()] = (uint64_t)f->get_length();
	}
	if (shader_pack_written) {
		f = FileAccess::open(shader_pack_path, FileAccess::READ);
		if (f.is_valid()) {
			file_sizes[shader_pack_path.get_file()] = (uint64_t)f->get_length();
		}
	}
	if (shader_extras_written) {
		f = FileAccess::open(shader_extras_path, FileAccess::READ);
		if (f.is_valid()) {
			file_sizes[shader_extras_path.get_file()] = (uint64_t)f->get_length();
		}
	}
	for (const Variant &texture_pack : texture_packs.values()) {
		f = FileAccess::open(base_dir.path_join(texture_pack), FileAccess::READ);
		if (f.is_valid()) {
			file_sizes[texture_pack] = (uint64_t)f->get_length();
		}
	}
	f = FileAccess::open(base_path + ".wasm", FileAccess::READ);
	if (f.is_valid()) {
		file_sizes[base_name + ".wasm"] = (uint64_t)f->get_length();
	}

	// Read the HTML shell file (custom or from template).
	const String html_path = custom_html.is_empty() ? base_path + ".html" : custom_html;
	Vector<uint8_t> html;
	f = FileAccess::open(html_path, FileAccess::READ);
	if (f.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not read HTML shell: \"%s\"."), html_path));
		return ERR_FILE_CANT_READ;
	}
	html.resize(f->get_length());
	f->get_buffer(html.ptrw(), html.size());
	f.unref(); // close file.

	// Generate HTML file with replaced strings.
	_fix_html(html, p_preset, base_name, p_debug, p_flags, shared_objects, file_sizes, texture_packs);
	Error err = _write_or_error(html.ptr(), html.size(), p_path);
	if (err != OK) {
		// Message is supplied by the subroutine method.
		return err;
	}
	html.resize(0);

	// Export splash (why?)
	Ref<Image> splash = _get_project_splash(p_preset);
	const String splash_png_path = base_path + ".png";
	if (splash->save_png(splash_png_path) != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), splash_png_path));
		return ERR_FILE_CANT_WRITE;
	}

	// Save a favicon that can be accessed without waiting for the project to finish loading.
	// This way, the favicon can be displayed immediately when loading the page.
	if (export_icon) {
		Ref<Image> favicon = _get_project_icon(p_preset);
		const String favicon_png_path = base_path + ".icon.png";
		if (favicon->save_png(favicon_png_path) != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), favicon_png_path));
			return ERR_FILE_CANT_WRITE;
		}
		favicon->resize(180, 180);
		const String apple_icon_png_path = base_path + ".apple-touch-icon.png";
		if (favicon->save_png(apple_icon_png_path) != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), apple_icon_png_path));
			return ERR_FILE_CANT_WRITE;
		}
	}

	// Generate the PWA worker and manifest
	if (pwa) {
		err = _build_pwa(p_preset, p_path, shared_objects);
		if (err != OK) {
			// Message is supplied by the subroutine method.
			return err;
		}
	}

	return OK;
}

bool EditorExportPlatformWeb::poll_export() {
	Ref<EditorExportPreset> preset = EditorExport::get_singleton()->get_runnable_preset_for_platform(this);

	RemoteDebugState prev_remote_debug_state = remote_debug_state;
	remote_debug_state = REMOTE_DEBUG_STATE_UNAVAILABLE;

	if (preset.is_valid()) {
		const bool debug = true;
		// Throwaway variables to pass to validation functions.
		String err;
		bool missing_templates;

		bool valid = has_valid_export_configuration(preset, err, missing_templates, debug) &&
				has_valid_project_configuration(preset, err);

		if (valid) {
			if (server->is_listening()) {
				remote_debug_state = REMOTE_DEBUG_STATE_SERVING;
			} else {
				remote_debug_state = REMOTE_DEBUG_STATE_AVAILABLE;
			}
		}
	}

	if (remote_debug_state != REMOTE_DEBUG_STATE_SERVING && server->is_listening()) {
		server->stop();
	}

	return remote_debug_state != prev_remote_debug_state;
}

Ref<Texture2D> EditorExportPlatformWeb::get_option_icon(int p_index) const {
	Ref<Texture2D> play_icon = EditorExportPlatform::get_option_icon(p_index);

	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE: {
			return nullptr;
		} break;

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			switch (p_index) {
				case 0:
				case 1:
					return play_icon;
				default:
					ERR_FAIL_V(nullptr);
			}
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			switch (p_index) {
				case 0:
					return play_icon;
				case 1:
					return restart_icon;
				case 2:
					return stop_icon;
				default:
					ERR_FAIL_V(nullptr);
			}
		} break;
	}

	return nullptr;
}

int EditorExportPlatformWeb::get_options_count() const {
	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE: {
			return 0;
		} break;

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			return 2;
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			return 3;
		} break;
	}

	return 0;
}

String EditorExportPlatformWeb::get_option_label(int p_index) const {
	String run_in_browser = TTR("Run in Browser");
	String start_http_server = TTR("Start HTTP Server");
	String reexport_project = TTR("Re-export Project");
	String stop_http_server = TTR("Stop HTTP Server");

	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE:
			return "";

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			switch (p_index) {
				case 0:
					return run_in_browser;
				case 1:
					return start_http_server;
				default:
					ERR_FAIL_V("");
			}
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			switch (p_index) {
				case 0:
					return run_in_browser;
				case 1:
					return reexport_project;
				case 2:
					return stop_http_server;
				default:
					ERR_FAIL_V("");
			}
		} break;
	}

	return "";
}

String EditorExportPlatformWeb::get_option_tooltip(int p_index) const {
	String run_in_browser = TTR("Run exported HTML in the system's default browser.");
	String start_http_server = TTR("Start the HTTP server.");
	String reexport_project = TTR("Export project again to account for updates.");
	String stop_http_server = TTR("Stop the HTTP server.");

	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE:
			return "";

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			switch (p_index) {
				case 0:
					return run_in_browser;
				case 1:
					return start_http_server;
				default:
					ERR_FAIL_V("");
			}
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			switch (p_index) {
				case 0:
					return run_in_browser;
				case 1:
					return reexport_project;
				case 2:
					return stop_http_server;
				default:
					ERR_FAIL_V("");
			}
		} break;
	}

	return "";
}

Error EditorExportPlatformWeb::run(const Ref<EditorExportPreset> &p_preset, int p_option, BitField<EditorExportPlatform::DebugFlags> p_debug_flags) {
	const uint16_t bind_port = EDITOR_GET("export/web/http_port");
	// Resolve host if needed.
	const String bind_host = EDITOR_GET("export/web/http_host");
	const bool use_tls = EDITOR_GET("export/web/use_tls");

	switch (remote_debug_state) {
		case REMOTE_DEBUG_STATE_UNAVAILABLE: {
			return FAILED;
		} break;

		case REMOTE_DEBUG_STATE_AVAILABLE: {
			switch (p_option) {
				// Run in Browser.
				case 0: {
					RETURN_IF_ERROR(_export_project(p_preset, p_debug_flags));
					RETURN_IF_ERROR(_start_server(bind_host, bind_port, use_tls));
					return _launch_browser(bind_host, bind_port, use_tls);
				} break;

				// Start HTTP Server.
				case 1: {
					RETURN_IF_ERROR(_export_project(p_preset, p_debug_flags));
					return _start_server(bind_host, bind_port, use_tls);
				} break;

				default: {
					ERR_FAIL_V_MSG(FAILED, vformat(R"(Invalid option "%s" for the current state.)", p_option));
				}
			}
		} break;

		case REMOTE_DEBUG_STATE_SERVING: {
			switch (p_option) {
				// Run in Browser.
				case 0: {
					RETURN_IF_ERROR(_export_project(p_preset, p_debug_flags));
					return _launch_browser(bind_host, bind_port, use_tls);
				} break;

				// Re-export Project.
				case 1: {
					return _export_project(p_preset, p_debug_flags);
				} break;

				// Stop HTTP Server.
				case 2: {
					return _stop_server();
				} break;

				default: {
					ERR_FAIL_V_MSG(FAILED, vformat(R"(Invalid option "%s" for the current state.)", p_option));
				}
			}
		} break;
	}

	return FAILED;
}

Error EditorExportPlatformWeb::_export_project(const Ref<EditorExportPreset> &p_preset, int p_debug_flags) {
	const String dest = EditorPaths::get_singleton()->get_temp_dir().path_join("web");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (!da->dir_exists(dest)) {
		Error err = da->make_dir_recursive(dest);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Could not create HTTP server directory: %s."), dest));
			return err;
		}
	}

	const String basepath = dest.path_join("tmp_js_export");
	Error err = export_project(p_preset, true, basepath + ".html", p_debug_flags);
	if (err != OK) {
		// Export generates several files, clean them up on failure.
		DirAccess::remove_file_or_error(basepath + ".html");
		DirAccess::remove_file_or_error(basepath + ".offline.html");
		DirAccess::remove_file_or_error(basepath + ".js");
		DirAccess::remove_file_or_error(basepath + ".audio.worklet.js");
		DirAccess::remove_file_or_error(basepath + ".service.worker.js");
		DirAccess::remove_file_or_error(basepath + ".pck");
		DirAccess::remove_file_or_error(basepath + ".shaders.pck");
		DirAccess::remove_file_or_error(basepath + ".shaders_extra.bin");
		DirAccess::remove_file_or_error(basepath + ".textures_bc.pck");
		DirAccess::remove_file_or_error(basepath + ".textures_etc2_astc.pck");
		DirAccess::remove_file_or_error(basepath + ".png");
		DirAccess::remove_file_or_error(basepath + ".side.wasm");
		DirAccess::remove_file_or_error(basepath + ".wasm");
		DirAccess::remove_file_or_error(basepath + ".icon.png");
		DirAccess::remove_file_or_error(basepath + ".apple-touch-icon.png");
	}
	return err;
}

Error EditorExportPlatformWeb::_launch_browser(const String &p_bind_host, const uint16_t p_bind_port, const bool p_use_tls) {
	OS::get_singleton()->shell_open(String((p_use_tls ? "https://" : "http://") + p_bind_host + ":" + itos(p_bind_port) + "/tmp_js_export.html"));
	// FIXME: Find out how to clean up export files after running the successfully
	// exported game. Might not be trivial.
	return OK;
}

Error EditorExportPlatformWeb::_start_server(const String &p_bind_host, const uint16_t p_bind_port, const bool p_use_tls) {
	IPAddress bind_ip;
	if (p_bind_host.is_valid_ip_address()) {
		bind_ip = p_bind_host;
	} else {
		bind_ip = IP::get_singleton()->resolve_hostname(p_bind_host);
	}
	ERR_FAIL_COND_V_MSG(!bind_ip.is_valid(), ERR_INVALID_PARAMETER, "Invalid editor setting 'export/web/http_host': '" + p_bind_host + "'. Try using '127.0.0.1'.");

	const String tls_key = EDITOR_GET("export/web/tls_key");
	const String tls_cert = EDITOR_GET("export/web/tls_certificate");

	// Restart server.
	server->stop();
	Error err = server->listen(p_bind_port, bind_ip, p_use_tls, tls_key, tls_cert);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Error starting HTTP server: %d."), err));
	}
	return err;
}

Error EditorExportPlatformWeb::_stop_server() {
	server->stop();
	return OK;
}

Ref<Texture2D> EditorExportPlatformWeb::get_run_icon() const {
	return run_icon;
}

String EditorExportPlatformWeb::get_debug_protocol() const {
#ifdef WEB_ENABLED
	return "messageport://";
#else
	return "ws://";
#endif
}

void EditorExportPlatformWeb::initialize() {
	if (EditorNode::get_singleton()) {
		server.instantiate();

		Ref<Image> img = memnew(Image);
		const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);

		ImageLoaderSVG::create_image_from_string(img, _web_logo_svg, EDSCALE, upsample, HashMap<Color, Color>());
		logo = ImageTexture::create_from_image(img);

		ImageLoaderSVG::create_image_from_string(img, _web_run_icon_svg, EDSCALE, upsample, HashMap<Color, Color>());
		run_icon = ImageTexture::create_from_image(img);

		Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		if (theme.is_valid()) {
			stop_icon = theme->get_icon(SNAME("Stop"), EditorStringName(EditorIcons));
			restart_icon = theme->get_icon(SNAME("Reload"), EditorStringName(EditorIcons));
		} else {
			stop_icon.instantiate();
			restart_icon.instantiate();
		}
	}
}

EditorExportPlatformWeb::~EditorExportPlatformWeb() {
}
