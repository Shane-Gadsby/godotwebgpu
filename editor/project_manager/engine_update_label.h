/**************************************************************************/
/*  engine_update_label.h                                                 */
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

#include "scene/gui/link_button.h"

class HTTPRequest;

class EngineUpdateLabel : public LinkButton {
	GDCLASS(EngineUpdateLabel, LinkButton);

public:
	enum class UpdateMode {
		DISABLED,
		AUTO,
		NEWEST_UNSTABLE,
		NEWEST_STABLE,
		NEWEST_PATCH,
	};

	// WebGPU fork: the update check asks this GitHub repository's releases instead of godotengine.org's
	// versions.json, because this fork's users want this fork's releases, not stock Godot's. An empty
	// string restores upstream's check unchanged. Releases are tagged `main-<commit>` by the release
	// workflow (.github/workflows/runner.yml, draft-release) and carry no machine-readable fork version,
	// so a release is newer when its commit is not this build's and was committed later (see
	// is_fork_release_newer()).
	static constexpr const char *FORK_RELEASES_REPO = "Shane-Gadsby/godotwebgpu";

	// WebGPU fork: the parsing and comparison steps of the fork check, static so tests can call them.
	// Index in p_releases (a GitHub "list releases" response) of the newest release p_mode accepts, or -1.
	// p_mode must already be resolved (not AUTO or DISABLED).
	static int pick_fork_release(const Array &p_releases, UpdateMode p_mode, int p_major, int p_minor);
	// The commit a release was built from: its `main-<sha>` tag, else a full SHA in target_commitish, else "".
	static String get_fork_release_commit(const Dictionary &p_release);
	// The engine version a release's assets are named with (godot-webgpu-export-templates-4.8.dev.tpz,
	// godot-webgpu-editor-4.8.dev-linux.zip), e.g. "4.8.dev", or "" when none is.
	static String get_fork_release_engine_version(const Dictionary &p_release);
	// The release's title without the redundant product prefix, for the label.
	static String get_fork_release_display_name(const Dictionary &p_release);
	// Whether a release built from p_release_commit, committed at p_release_commit_time (UNIX seconds),
	// is newer than a build of p_own_hash committed at p_own_timestamp. False when either time is unknown
	// (0 or less): an unknown build is never told to update.
	static bool is_fork_release_newer(const String &p_release_commit, int64_t p_release_commit_time, const String &p_own_hash, int64_t p_own_timestamp);

private:
	static constexpr int DEV_VERSION = 9999; // Version index for unnumbered builds (assumed to always be newest).

	enum class VersionType {
		STABLE,
		RC,
		BETA,
		ALPHA,
		DEV,
		UNKNOWN,
	};

	enum class UpdateStatus {
		NONE,
		OFFLINE,
		BUSY,
		ERROR,
		UPDATE_AVAILABLE,
		UP_TO_DATE,
	};

	struct ThemeCache {
		Color default_color;
		Color disabled_color;
		Color error_color;
		Color update_color;
	} theme_cache;

	HTTPRequest *http = nullptr;

	UpdateStatus status = UpdateStatus::NONE;
	bool checked_update = false;
	String available_newer_version;

	// WebGPU fork: the release picked from the list while its commit's date is fetched, and the page the
	// label opens.
	bool fetching_fork_commit = false;
	String fork_release_name;
	String fork_release_commit;
	String fork_release_url;

	void _request_fork_releases();
	void _fork_releases_received(const Variant &p_result);
	void _fork_commit_received(const Variant &p_result);
	void _get_own_build(String &r_hash, int64_t &r_timestamp) const;

	String current_message;
	Variant message_argument;

	bool _can_check_updates() const;
	void _check_update();
	void _http_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);

	void _set_message(const String &p_message, const Color &p_color, const Variant &p_message_argument = Variant());
	void _set_status(UpdateStatus p_status);
	void _update_message();

	VersionType _get_version_type(const String &p_string, int *r_index = nullptr) const;
	String _extract_sub_string(const String &p_line) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

	virtual void pressed() override;

public:
	EngineUpdateLabel();
};
