extends RefCounted

static func run() -> Dictionary:
	var report := {"checks": [], "errors": []}
	for pair in [["0.1.0-alpha.2", "0.1.0-alpha.10"], ["0.1.0-alpha.10", "0.1.0-beta.1"], ["0.1.0-beta.9", "0.1.0-rc.1"], ["0.1.0-rc.9", "0.1.0"], ["0.1.9", "0.2.0-alpha.1"], ["0.9.0", "1.0.0"]]:
		_expect(ROMEUpdateManager.compare_versions(pair[0], pair[1]) < 0, "Update ordering: " + str(pair), report)
		_expect(ROMEUpdateManager.compare_versions(pair[1], pair[0]) > 0, "Reverse update ordering: " + str(pair), report)
	_expect(ROMEUpdateManager.compare_versions("v0.1.0+build.1", "0.1.0+build.2") == 0, "Build metadata does not change version precedence", report)
	for invalid in ["", "../1.0.0", "0.1", "1.0.0-alpha.01", "1.0.0-", "1.0.0<script>"]:
		_expect(not ROMEUpdateManager.is_version_valid(invalid), "Malformed release tag rejected: " + invalid, report)
	var releases := [_release("v0.1.0-alpha.2"), _release("v0.1.0-alpha.10"), _release("v0.2.0")]
	releases[-1].draft = true
	var selected := ROMEUpdateManager.select_rome_release(releases, "windows-x86_64")
	_expect(selected.get("version", "") == "0.1.0-alpha.10", "Latest public alpha selected; drafts excluded", report)
	_expect(selected.get("asset", {}).get("sha256", "") == "a".repeat(64), "Update asset has a verified SHA-256 expectation", report)
	_expect(selected.get("notes", "") == "Notas de v0.1.0-alpha.10", "Release notes travel with the selected update", report)
	# GitHub answers JSON, and Godot parses every JSON number as a float.
	var from_github: Array = JSON.parse_string(JSON.stringify(releases))
	_expect(ROMEUpdateManager.select_rome_release(from_github, "windows-x86_64").get("asset", {}).get("size", 0) == 1000000, "Asset sizes parsed from GitHub JSON are accepted", report)
	var bad := _release("v0.2.0")
	bad.assets[0].browser_download_url = "https://example.com/untrusted.exe"
	_expect(ROMEUpdateManager.select_rome_release([bad], "windows-x86_64").get("asset", {}).is_empty(), "Off-repository download URL rejected", report)
	bad = _release("v0.2.0")
	bad.assets[0].digest = "sha256:bad"
	_expect(ROMEUpdateManager.select_rome_release([bad], "windows-x86_64").get("asset", {}).is_empty(), "Malformed checksum rejected", report)
	bad = _release("v0.2.0")
	bad.assets[0].size = 1000000.5
	_expect(ROMEUpdateManager.select_rome_release([bad], "windows-x86_64").get("asset", {}).is_empty(), "Fractional asset size rejected", report)
	_expect(ROMEUpdateManager.select_rome_release(releases, "linux-x86_64").get("asset", {}).is_empty(), "Windows package is never offered to Linux", report)
	var channel := [_release("v0.1.0"), _release("v0.2.0-beta.1"), _release("v0.3.0")]
	channel[0].prerelease = false
	_expect(ROMEUpdateManager.select_rome_release(channel, "windows-x86_64", true).get("version", "") == "0.3.0", "Alpha and beta channel takes the newest release", report)
	_expect(ROMEUpdateManager.select_rome_release(channel, "windows-x86_64", false).get("version", "") == "0.1.0", "Stable channel skips prereleases, also those marked only on GitHub", report)
	var godot := [{"tag_name": "4.7.2-stable", "prerelease": false}, {"tag_name": "4.8-beta1", "prerelease": true}, {"tag_name": "4.8-rc1", "prerelease": false}, {"tag_name": "4.8-stable", "prerelease": false}, {"tag_name": "4.9-stable", "prerelease": true}, {"tag_name": "5.0-stable", "draft": true}]
	_expect(ROMEUpdateManager.select_godot_release(godot).get("version", "") == "4.8.0", "Godot selection accepts only published stable releases", report)
	return report

static func _release(tag: String) -> Dictionary:
	var version := tag.trim_prefix("v")
	var name := "ROME-Studio-" + version + "-windows-x86_64.exe"
	return {"tag_name": tag, "draft": false, "prerelease": true, "body": "Notas de " + tag, "assets": [{"name": name, "browser_download_url": "https://github.com/Pimpoli/ROME-Engine/releases/download/" + tag + "/" + name, "digest": "sha256:" + "a".repeat(64), "size": 1000000}]}

static func _expect(value: bool, label: String, report: Dictionary) -> void:
	if value:
		report.checks.append(label)
	else:
		report.errors.append(label)
