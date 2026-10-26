/**************************************************************************/
/*  engine_update_label.cpp                                               */
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

#include "engine_update_label.h"

#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/version.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "scene/main/http_request.h"

bool EngineUpdateLabel::_can_check_updates() const {
	return int(EDITOR_GET("network/connection/network_mode")) == EditorSettings::NETWORK_ONLINE &&
			UpdateMode(int(EDITOR_GET("network/connection/check_for_updates"))) != UpdateMode::DISABLED;
}

void EngineUpdateLabel::_check_update() {
	checked_update = true;
	_set_status(UpdateStatus::BUSY);
	// WebGPU fork: this fork's releases, see FORK_RELEASES_REPO.
	if (FORK_RELEASES_REPO[0] != '\0') {
		_request_fork_releases();
		return;
	}
	http->request("https://godotengine.org/versions.json");
}

// WebGPU fork: the update check against this fork's GitHub releases. Two requests: the release list,
// then the date of the newest acceptable release's commit, which the list does not carry. The second is
// skipped when that commit is this build's own. Unauthenticated, GitHub allows 60 requests an hour per
// address, far more than one check per editor start needs.
static PackedStringArray _github_api_headers() {
	PackedStringArray headers;
	headers.push_back("Accept: application/vnd.github+json");
	headers.push_back("X-GitHub-Api-Version: 2022-11-28");
	return headers;
}

void EngineUpdateLabel::_request_fork_releases() {
	fetching_fork_commit = false;
	fork_release_name = String();
	fork_release_commit = String();
	fork_release_url = String();
	http->request(vformat("https://api.github.com/repos/%s/releases?per_page=30", FORK_RELEASES_REPO), _github_api_headers());
}

void EngineUpdateLabel::_get_own_build(String &r_hash, int64_t &r_timestamp) const {
	r_hash = GODOT_VERSION_HASH;
	r_timestamp = int64_t(GODOT_VERSION_TIMESTAMP);
#ifdef DEV_ENABLED
	// Lets a dev build pose as another build, to see the label's states against the live API without
	// building an old commit: GODOT_UPDATE_CHECK_AS=<hash>:<commit UNIX time>.
	const String as = OS::get_singleton()->get_environment("GODOT_UPDATE_CHECK_AS");
	if (as.contains_char(':')) {
		r_hash = as.get_slicec(':', 0);
		r_timestamp = as.get_slicec(':', 1).to_int();
	}
#endif
}

void EngineUpdateLabel::_fork_releases_received(const Variant &p_result) {
	if (p_result.get_type() != Variant::ARRAY) {
		_set_status(UpdateStatus::ERROR);
		_set_message(TTRC("Received JSON data is not a valid version array."), theme_cache.error_color);
		return;
	}

	UpdateMode update_mode = UpdateMode(int(EDITOR_GET("network/connection/check_for_updates")));
	if (update_mode == UpdateMode::AUTO) {
		update_mode = _get_version_type(GODOT_VERSION_STATUS) == VersionType::STABLE ? UpdateMode::NEWEST_STABLE : UpdateMode::NEWEST_UNSTABLE;
	}

	const Array releases = p_result;
	const int index = pick_fork_release(releases, update_mode, GODOT_VERSION_MAJOR, GODOT_VERSION_MINOR);
	if (index < 0) {
		_set_status(UpdateStatus::UP_TO_DATE);
		return;
	}

	const Dictionary release = releases[index];
	fork_release_name = get_fork_release_display_name(release);
	fork_release_commit = get_fork_release_commit(release);
	fork_release_url = release.get("html_url", "");

	String own_hash;
	int64_t own_timestamp;
	_get_own_build(own_hash, own_timestamp);
	if (own_hash.length() >= 7 && fork_release_commit.begins_with(own_hash.to_lower())) {
		// This build is the release itself; no need for the commit's date.
		_set_status(UpdateStatus::UP_TO_DATE);
		return;
	}

	fetching_fork_commit = true;
	http->request(vformat("https://api.github.com/repos/%s/commits/%s", FORK_RELEASES_REPO, fork_release_commit), _github_api_headers());
}

void EngineUpdateLabel::_fork_commit_received(const Variant &p_result) {
	fetching_fork_commit = false;
	if (p_result.get_type() != Variant::DICTIONARY) {
		_set_status(UpdateStatus::ERROR);
		_set_message(TTRC("Failed to parse version JSON."), theme_cache.error_color);
		return;
	}

	// The committer date, which is what GODOT_VERSION_TIMESTAMP is (`git log --pretty=format:%ct`).
	const Dictionary commit = Dictionary(p_result).get("commit", Dictionary());
	const Dictionary committer = commit.get("committer", Dictionary());
	const String date = String(committer.get("date", "")).trim_suffix("Z");
	const int64_t release_time = date.is_empty() ? 0 : Time::get_singleton()->get_unix_time_from_datetime_string(date);

	String own_hash;
	int64_t own_timestamp;
	_get_own_build(own_hash, own_timestamp);
	if (is_fork_release_newer(fork_release_commit, release_time, own_hash, own_timestamp)) {
		available_newer_version = fork_release_name;
		_set_status(UpdateStatus::UPDATE_AVAILABLE);
		_set_message(TTRC("Update available: %s."), theme_cache.update_color, available_newer_version);
	} else {
		available_newer_version = String();
		_set_status(UpdateStatus::UP_TO_DATE);
	}
}

static bool _is_commit_sha(const String &p_string) {
	if (p_string.length() != 40) {
		return false;
	}
	for (int i = 0; i < p_string.length(); i++) {
		if (!is_hex_digit(p_string[i])) {
			return false;
		}
	}
	return true;
}

String EngineUpdateLabel::get_fork_release_commit(const Dictionary &p_release) {
	const String tag = p_release.get("tag_name", "");
	if (tag.begins_with("main-") && _is_commit_sha(tag.trim_prefix("main-"))) {
		return tag.trim_prefix("main-").to_lower();
	}
	const String target = p_release.get("target_commitish", "");
	if (_is_commit_sha(target)) {
		return target.to_lower();
	}
	return String();
}

String EngineUpdateLabel::get_fork_release_engine_version(const Dictionary &p_release) {
	const Array assets = p_release.get("assets", Array());
	for (const Variant &asset_variant : assets) {
		const Dictionary asset = asset_variant;
		const String name = asset.get("name", "");
		if (name.begins_with("godot-webgpu-export-templates-") && name.ends_with(".tpz")) {
			return name.trim_prefix("godot-webgpu-export-templates-").trim_suffix(".tpz");
		}
		if (name.begins_with("godot-webgpu-editor-") && name.ends_with(".zip")) {
			// godot-webgpu-editor-<version>-<platform>.zip; the version has no '-' (4.8.dev, 4.7.2.stable).
			return name.trim_prefix("godot-webgpu-editor-").get_slicec('-', 0);
		}
	}
	return String();
}

String EngineUpdateLabel::get_fork_release_display_name(const Dictionary &p_release) {
	String name = String(p_release.get("name", "")).strip_edges();
	if (name.is_empty()) {
		return p_release.get("tag_name", "");
	}
	if (name.begins_with("Godot WebGPU ") && name.length() > 13) {
		name = name.substr(13);
	}
	return name;
}

int EngineUpdateLabel::pick_fork_release(const Array &p_releases, UpdateMode p_mode, int p_major, int p_minor) {
	const bool stable_only = p_mode == UpdateMode::NEWEST_STABLE || p_mode == UpdateMode::NEWEST_PATCH;
	int picked = -1;
	String picked_published_at;
	for (int i = 0; i < p_releases.size(); i++) {
		if (p_releases[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary release = p_releases[i];
		// The API lists drafts only to the repository's writers, and they have no published date.
		const String published_at = release.get("published_at", "");
		if (bool(release.get("draft", false)) || published_at.is_empty()) {
			continue;
		}
		if (stable_only && bool(release.get("prerelease", false))) {
			continue;
		}
		if (get_fork_release_commit(release).is_empty()) {
			continue;
		}

		// Like upstream's check, never offer another major version, and Check Newest Patch stays on this
		// minor version. A release whose assets name no engine version is not filtered.
		const PackedStringArray engine_bits = get_fork_release_engine_version(release).split(".");
		if (engine_bits.size() >= 2) {
			if (engine_bits[0].to_int() != p_major) {
				continue;
			}
			if (p_mode == UpdateMode::NEWEST_PATCH && engine_bits[1].to_int() != p_minor) {
				continue;
			}
		}

		// ISO 8601 in UTC compares as text. The list is ordered by creation, not publication.
		if (picked < 0 || picked_published_at < published_at) {
			picked = i;
			picked_published_at = published_at;
		}
	}
	return picked;
}

bool EngineUpdateLabel::is_fork_release_newer(const String &p_release_commit, int64_t p_release_commit_time, const String &p_own_hash, int64_t p_own_timestamp) {
	const String own_hash = p_own_hash.to_lower();
	if (own_hash.length() >= 7 && p_release_commit.to_lower().begins_with(own_hash)) {
		return false; // This build is the release.
	}
	if (p_release_commit_time <= 0 || p_own_timestamp <= 0) {
		return false;
	}
	return p_release_commit_time > p_own_timestamp;
}

void EngineUpdateLabel::_http_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (p_result != OK) {
		_set_status(UpdateStatus::ERROR);
		_set_message(TTRC("Failed to check for updates. Error: %d."), theme_cache.error_color, p_result);
		return;
	}

	if (p_response_code != 200) {
		_set_status(UpdateStatus::ERROR);
		_set_message(TTRC("Failed to check for updates. Response code: %d."), theme_cache.error_color, p_response_code);
		return;
	}

	// WebGPU fork: this fork's releases, see FORK_RELEASES_REPO.
	if (FORK_RELEASES_REPO[0] != '\0') {
		const Variant result = JSON::parse_string(String::utf8((const char *)p_body.ptr(), p_body.size()));
		if (fetching_fork_commit) {
			_fork_commit_received(result);
		} else {
			_fork_releases_received(result);
		}
		return;
	}

	Array version_array;
	{
		const uint8_t *r = p_body.ptr();
		String s = String::utf8((const char *)r, p_body.size());

		Variant result = JSON::parse_string(s);
		if (result == Variant()) {
			_set_status(UpdateStatus::ERROR);
			_set_message(TTRC("Failed to parse version JSON."), theme_cache.error_color);
			return;
		}
		if (result.get_type() != Variant::ARRAY) {
			_set_status(UpdateStatus::ERROR);
			_set_message(TTRC("Received JSON data is not a valid version array."), theme_cache.error_color);
			return;
		}
		version_array = result;
	}

	UpdateMode update_mode = UpdateMode(int(EDITOR_GET("network/connection/check_for_updates")));
	if (update_mode == UpdateMode::AUTO) {
		if (_get_version_type(GODOT_VERSION_STATUS) == VersionType::STABLE) {
			update_mode = UpdateMode::NEWEST_STABLE;
		} else {
			update_mode = UpdateMode::NEWEST_UNSTABLE;
		}
	}
	bool stable_only = update_mode == UpdateMode::NEWEST_STABLE || update_mode == UpdateMode::NEWEST_PATCH;

	available_newer_version = String();
	for (const Variant &data_bit : version_array) {
		const Dictionary version_info = data_bit;

		const String base_version_string = version_info.get("name", "");
		const PackedStringArray version_bits = base_version_string.split(".");

		if (version_bits.size() < 2) {
			continue;
		}

		int minor = version_bits[1].to_int();
		if (version_bits[0].to_int() != GODOT_VERSION_MAJOR || minor < GODOT_VERSION_MINOR) {
			continue;
		}

		int patch = 0;
		if (version_bits.size() >= 3) {
			patch = version_bits[2].to_int();
		}

		if (minor == GODOT_VERSION_MINOR && patch < GODOT_VERSION_PATCH) {
			continue;
		}

		if (update_mode == UpdateMode::NEWEST_PATCH && minor > GODOT_VERSION_MINOR) {
			continue;
		}

		const Array releases = version_info.get("releases", Array());
		if (releases.is_empty()) {
			continue;
		}

		const Dictionary newest_release = releases[0];
		const String release_string = newest_release.get("name", "unknown");

		int release_index;
		VersionType release_type = _get_version_type(release_string, &release_index);

		if (minor > GODOT_VERSION_MINOR || patch > GODOT_VERSION_PATCH) {
			if (stable_only && release_type != VersionType::STABLE) {
				continue;
			}

			available_newer_version = vformat("%s-%s", base_version_string, release_string);
			break;
		}

		int current_version_index;
		VersionType current_version_type = _get_version_type(GODOT_VERSION_STATUS, &current_version_index);

		if (int(release_type) > int(current_version_type)) {
			break;
		}

		if (int(release_type) == int(current_version_type) && release_index <= current_version_index) {
			break;
		}

		available_newer_version = vformat("%s-%s", base_version_string, release_string);
		break;
	}

	if (!available_newer_version.is_empty()) {
		_set_status(UpdateStatus::UPDATE_AVAILABLE);
		_set_message(TTRC("Update available: %s."), theme_cache.update_color, available_newer_version);
	} else if (available_newer_version.is_empty()) {
		_set_status(UpdateStatus::UP_TO_DATE);
	}
}

void EngineUpdateLabel::_set_message(const String &p_message, const Color &p_color, const Variant &p_message_argument) {
	if (is_disabled()) {
		add_theme_color_override("font_disabled_color", p_color);
	} else {
		add_theme_color_override(SceneStringName(font_color), p_color);
	}
	current_message = p_message;
	message_argument = p_message_argument;
	_update_message();
}

void EngineUpdateLabel::_set_status(UpdateStatus p_status) {
	status = p_status;
	if (status == UpdateStatus::BUSY || status == UpdateStatus::UP_TO_DATE) {
		// Hide the label to prevent unnecessary distraction.
		hide();
		return;
	} else {
		show();
	}

	switch (status) {
		case UpdateStatus::OFFLINE: {
			set_disabled(false);
			if (int(EDITOR_GET("network/connection/network_mode")) == EditorSettings::NETWORK_OFFLINE) {
				_set_message(TTRC("Offline mode, update checks disabled."), theme_cache.disabled_color);
			} else {
				_set_message(TTRC("Update checks disabled."), theme_cache.disabled_color);
			}
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_OFF);
			set_tooltip_text("");
			break;
		}

		case UpdateStatus::ERROR: {
			set_disabled(false);
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_POLITE);
			set_tooltip_text(TTR("An error has occurred. Click to try again."));
		} break;

		case UpdateStatus::UPDATE_AVAILABLE: {
			set_disabled(false);
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_POLITE);
			set_tooltip_text(TTR("Click to open download page."));
		} break;

		default: {
		}
	}
}

void EngineUpdateLabel::_update_message() {
	if (message_argument.get_type() == Variant::NIL) {
		set_text(TTR(current_message));
	} else {
		set_text(vformat(TTR(current_message), message_argument));
	}
}

EngineUpdateLabel::VersionType EngineUpdateLabel::_get_version_type(const String &p_string, int *r_index) const {
	VersionType type = VersionType::UNKNOWN;
	String index_string;

	static HashMap<String, VersionType> type_map;
	if (type_map.is_empty()) {
		type_map["stable"] = VersionType::STABLE;
		type_map["rc"] = VersionType::RC;
		type_map["beta"] = VersionType::BETA;
		type_map["alpha"] = VersionType::ALPHA;
		type_map["dev"] = VersionType::DEV;
	}

	for (const KeyValue<String, VersionType> &kv : type_map) {
		if (p_string.begins_with(kv.key)) {
			index_string = p_string.trim_prefix(kv.key);
			type = kv.value;
			break;
		}
	}

	if (r_index) {
		if (index_string.is_empty()) {
			*r_index = DEV_VERSION;
		} else {
			*r_index = index_string.to_int();
		}
	}
	return type;
}

String EngineUpdateLabel::_extract_sub_string(const String &p_line) const {
	int j = p_line.find_char('"') + 1;
	return p_line.substr(j, p_line.find_char('"', j) - j);
}

void EngineUpdateLabel::_notification(int p_what) {
	switch (p_what) {
		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (!EditorSettings::get_singleton()->check_changed_settings_in_group("network/connection")) {
				break;
			}

			if (_can_check_updates()) {
				_check_update();
			} else {
				http->cancel_request();
				_set_status(UpdateStatus::OFFLINE);
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			theme_cache.default_color = get_theme_color(SceneStringName(font_color), "Button");
			theme_cache.disabled_color = get_theme_color("font_disabled_color", "Button");
			theme_cache.error_color = get_theme_color("error_color", EditorStringName(Editor));
			theme_cache.update_color = get_theme_color("warning_color", EditorStringName(Editor));
		} break;

		case NOTIFICATION_TRANSLATION_CHANGED: {
			if (!current_message.is_empty()) {
				_update_message();
			}
		} break;

		case NOTIFICATION_READY: {
			if (_can_check_updates()) {
				_check_update();
			} else {
				_set_status(UpdateStatus::OFFLINE);
			}
		} break;
	}
}

void EngineUpdateLabel::_bind_methods() {
	ADD_SIGNAL(MethodInfo("offline_clicked"));
}

void EngineUpdateLabel::pressed() {
	switch (status) {
		case UpdateStatus::OFFLINE: {
			emit_signal("offline_clicked");
		} break;

		case UpdateStatus::ERROR: {
			_check_update();
		} break;

		case UpdateStatus::UPDATE_AVAILABLE: {
			// WebGPU fork: the release's page on GitHub.
			if (FORK_RELEASES_REPO[0] != '\0') {
				if (fork_release_url.begins_with("https://github.com/")) {
					OS::get_singleton()->shell_open(fork_release_url);
				}
				break;
			}
			OS::get_singleton()->shell_open("https://godotengine.org/download/archive/" + available_newer_version);
		} break;

		default: {
		}
	}
}

EngineUpdateLabel::EngineUpdateLabel() {
	set_underline_mode(UNDERLINE_MODE_ON_HOVER);
	set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);

	http = memnew(HTTPRequest);
	http->set_https_proxy(EDITOR_GET("network/http_proxy/host"), EDITOR_GET("network/http_proxy/port"));
	http->set_timeout(10.0);
	add_child(http);
	http->connect("request_completed", callable_mp(this, &EngineUpdateLabel::_http_request_completed));
}
