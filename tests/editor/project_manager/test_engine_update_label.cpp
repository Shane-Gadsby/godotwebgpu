/**************************************************************************/
/*  test_engine_update_label.cpp                                          */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_engine_update_label)

#ifdef TOOLS_ENABLED

#include "core/os/time.h"
#include "editor/project_manager/engine_update_label.h"

// WebGPU fork: the parsing and comparison steps of the update check against this fork's GitHub releases.
namespace TestEngineUpdateLabel {

using UpdateMode = EngineUpdateLabel::UpdateMode;

static Dictionary make_release(const String &p_name, const String &p_tag, const String &p_published_at, const String &p_asset, bool p_prerelease = false, bool p_draft = false) {
	Dictionary release;
	release["name"] = p_name;
	release["tag_name"] = p_tag;
	release["target_commitish"] = "main";
	release["published_at"] = p_published_at.is_empty() ? Variant() : Variant(p_published_at);
	release["prerelease"] = p_prerelease;
	release["draft"] = p_draft;
	release["html_url"] = "https://github.com/Shane-Gadsby/godotwebgpu/releases/tag/" + p_tag;
	Array assets;
	if (!p_asset.is_empty()) {
		Dictionary asset;
		asset["name"] = p_asset;
		assets.push_back(asset);
	}
	release["assets"] = assets;
	return release;
}

static const String SHA_151 = "f184b220602d64d04d86a48f82e7b3cf7a4eb25a";
static const String SHA_150 = "44f0cc3a5a5b1036d506968504c2dee515842041";
static const String SHA_100 = "f2036d700ed509247494bdbd35a805eab816b71d";

// The live list on 2026-10-26, newest first, as the API orders it.
static Array make_live_releases() {
	Array releases;
	releases.push_back(make_release("Godot WebGPU 4.8.dev 1.5.1 - OSX fix", "main-" + SHA_151, "2026-10-20T23:10:25Z", "godot-webgpu-editor-4.8.dev-linux.zip"));
	releases.push_back(make_release("Godot WebGPU Forward+ v1.5", "main-" + SHA_150, "2026-10-19T04:17:56Z", "godot-webgpu-export-templates-4.8.dev.tpz"));
	releases.push_back(make_release("Godot WebGPU Forward+ v1.0", "main-" + SHA_100, "2026-09-28T09:14:50Z", "godot-webgpu-export-templates-4.7.2.stable.tpz"));
	return releases;
}

TEST_CASE("[EngineUpdateLabel] Fork release commit and engine version") {
	const Array releases = make_live_releases();
	CHECK(EngineUpdateLabel::get_fork_release_commit(releases[0]) == SHA_151);
	CHECK(EngineUpdateLabel::get_fork_release_engine_version(releases[0]) == "4.8.dev");
	CHECK(EngineUpdateLabel::get_fork_release_engine_version(releases[1]) == "4.8.dev");
	CHECK(EngineUpdateLabel::get_fork_release_engine_version(releases[2]) == "4.7.2.stable");
	CHECK(EngineUpdateLabel::get_fork_release_display_name(releases[1]) == "Forward+ v1.5");

	// A tag that is not main-<sha> falls back to a full SHA in target_commitish, else no commit.
	Dictionary other = make_release("v2.0", "v2.0", "2026-11-01T00:00:00Z", "");
	CHECK(EngineUpdateLabel::get_fork_release_commit(other).is_empty());
	other["target_commitish"] = SHA_150.to_upper();
	CHECK(EngineUpdateLabel::get_fork_release_commit(other) == SHA_150);
	CHECK(EngineUpdateLabel::get_fork_release_commit(make_release("x", "main-1234", "2026-11-01T00:00:00Z", "")).is_empty());
	CHECK(EngineUpdateLabel::get_fork_release_engine_version(other).is_empty());
	CHECK(EngineUpdateLabel::get_fork_release_display_name(make_release("", "main-" + SHA_150, "", "")) == "main-" + SHA_150);
}

TEST_CASE("[EngineUpdateLabel] Picking the fork release to compare with") {
	Array releases = make_live_releases();
	CHECK(EngineUpdateLabel::pick_fork_release(releases, UpdateMode::NEWEST_UNSTABLE, 4, 8) == 0);
	CHECK(EngineUpdateLabel::pick_fork_release(releases, UpdateMode::NEWEST_STABLE, 4, 8) == 0);
	CHECK(EngineUpdateLabel::pick_fork_release(releases, UpdateMode::NEWEST_PATCH, 4, 8) == 0);
	// Another major version is never offered; Check Newest Patch stays on the minor version.
	CHECK(EngineUpdateLabel::pick_fork_release(releases, UpdateMode::NEWEST_STABLE, 5, 0) == -1);
	CHECK(EngineUpdateLabel::pick_fork_release(releases, UpdateMode::NEWEST_PATCH, 4, 7) == 2);
	CHECK(EngineUpdateLabel::pick_fork_release(releases, UpdateMode::NEWEST_STABLE, 4, 7) == 0);

	// Drafts never count; prereleases only for Check Newest Preview. Ordered by publication, not list order.
	releases.push_front(make_release("Godot WebGPU 4.8.dev @ abc", "main-" + SHA_100, "", "", false, true));
	releases.push_back(make_release("Godot WebGPU 1.6 beta", "main-" + SHA_150, "2026-10-25T00:00:00Z", "", true));
	CHECK(EngineUpdateLabel::pick_fork_release(releases, UpdateMode::NEWEST_UNSTABLE, 4, 8) == 4);
	CHECK(EngineUpdateLabel::pick_fork_release(releases, UpdateMode::NEWEST_STABLE, 4, 8) == 1);

	CHECK(EngineUpdateLabel::pick_fork_release(Array(), UpdateMode::NEWEST_UNSTABLE, 4, 8) == -1);
	Array junk;
	junk.push_back(42);
	junk.push_back(make_release("no commit", "v1", "2026-10-25T00:00:00Z", ""));
	CHECK(EngineUpdateLabel::pick_fork_release(junk, UpdateMode::NEWEST_UNSTABLE, 4, 8) == -1);
}

TEST_CASE("[EngineUpdateLabel] Whether a fork release is newer than this build") {
	// f184b22's committer date, 2026-10-20T21:14:35Z, as the commits API reports it.
	const int64_t release_time = Time::get_singleton()->get_unix_time_from_datetime_string("2026-10-20T21:14:35");
	CHECK(release_time == 1792530875);

	// The release's own build, by full or abbreviated hash, whatever the times say.
	CHECK_FALSE(EngineUpdateLabel::is_fork_release_newer(SHA_151, release_time, SHA_151, 0));
	CHECK_FALSE(EngineUpdateLabel::is_fork_release_newer(SHA_151, release_time, "F184B2206", 1));
	// An older build is told; a later one (e.g. webgpu-4.8 after the release) is not.
	CHECK(EngineUpdateLabel::is_fork_release_newer(SHA_151, release_time, SHA_150, 1792300000));
	CHECK_FALSE(EngineUpdateLabel::is_fork_release_newer(SHA_151, release_time, SHA_150, 1793006742));
	CHECK_FALSE(EngineUpdateLabel::is_fork_release_newer(SHA_151, release_time, SHA_150, release_time));
	// Unknown times (a build without git, a failed date parse) never report an update.
	CHECK_FALSE(EngineUpdateLabel::is_fork_release_newer(SHA_151, release_time, "", 0));
	CHECK_FALSE(EngineUpdateLabel::is_fork_release_newer(SHA_151, -1, SHA_150, 1792300000));
	// A hash too short to mean anything is not taken as a match.
	CHECK(EngineUpdateLabel::is_fork_release_newer(SHA_151, release_time, "f18", 1792300000));
}

} // namespace TestEngineUpdateLabel

#endif // TOOLS_ENABLED
