#include "rome_update_manager.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_node.h"
#include "editor/gui/editor_toaster.h"
#include "editor/project_manager/rome_project_template.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/separator.h"
#include "scene/main/http_request.h"
#include "servers/display/display_server.h"

namespace {

const char *ROME_RELEASE_API = "https://api.github.com/repos/Pimpoli/ROME-Engine/releases?per_page=20";
const char *GODOT_STABLE_API = "https://api.github.com/repos/godotengine/godot-builds/releases/latest";
const char *ROME_DOWNLOAD_ROOT = "https://github.com/Pimpoli/ROME-Engine/releases/download/";
const char *STARTUP_SETTING = "rome/updates/check_on_startup";
const char *DOWNLOAD_SETTING = "rome/updates/download_automatically";
const char *INSTALL_ON_EXIT_SETTING = "rome/updates/install_on_exit";
const char *PRERELEASE_SETTING = "rome/updates/include_prereleases";
const char *FAILED_SETTING = "rome/updates/failed_version";
constexpr int MAX_DOWNLOAD_BYTES = 1024 * 1024 * 1024;
// Automatic checks reuse the last answer for a while: GitHub allows 60 anonymous
// queries per hour and address, and the hub and the editor each check on start.
constexpr double CHECK_INTERVAL_SECONDS = 600.0;
constexpr uint64_t DOWNLOAD_STALL_MSEC = 60000;
// Consecutive attempts that bring no data before the download waits for the user.
constexpr int DOWNLOAD_RETRIES = 8;
constexpr uint64_t HELPER_READY_MSEC = 90000;
constexpr int MAX_NOTES_LENGTH = 6000;

struct UpdateVersion {
	bool valid = false;
	String version;
	Vector<String> core;
	Vector<String> prerelease;
};

bool numeric_identifier(const String &p_value) {
	if (p_value.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_value.length(); i++) {
		if (p_value[i] < '0' || p_value[i] > '9') {
			return false;
		}
	}
	return true;
}

bool valid_identifier(const String &p_value) {
	if (p_value.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_value.length(); i++) {
		const char32_t c = p_value[i];
		if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-')) {
			return false;
		}
	}
	return true;
}

UpdateVersion parse_version(const String &p_value) {
	UpdateVersion result;
	String value = p_value.trim_prefix("v");
	if (value.is_empty() || value.length() > 200 || value != value.strip_edges()) {
		return result;
	}
	result.version = value;
	const int plus = value.find_char('+');
	if (plus >= 0) {
		const Vector<String> metadata = value.substr(plus + 1).split(".", true);
		for (const String &part : metadata) {
			if (!valid_identifier(part)) {
				return result;
			}
		}
		value = value.left(plus);
	}
	const int dash = value.find_char('-');
	if (dash >= 0) {
		result.prerelease = value.substr(dash + 1).split(".", true);
		for (const String &part : result.prerelease) {
			if (!valid_identifier(part) || (numeric_identifier(part) && part.length() > 1 && part[0] == '0')) {
				return result;
			}
		}
		value = value.left(dash);
	}
	result.core = value.split(".", true);
	if (result.core.size() != 3) {
		return result;
	}
	for (const String &part : result.core) {
		if (!numeric_identifier(part) || (part.length() > 1 && part[0] == '0')) {
			return result;
		}
	}
	result.valid = true;
	return result;
}

int compare_number(const String &p_a, const String &p_b) {
	if (p_a.length() != p_b.length()) {
		return p_a.length() > p_b.length() ? 1 : -1;
	}
	return p_a == p_b ? 0 : p_a > p_b ? 1 : -1;
}

bool valid_sha256(const String &p_hash) {
	if (p_hash.length() != 64) {
		return false;
	}
	for (int i = 0; i < p_hash.length(); i++) {
		const char32_t c = p_hash[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
			return false;
		}
	}
	return true;
}

String current_godot_number() {
	return itos(GODOT_VERSION_MAJOR) + "." + itos(GODOT_VERSION_MINOR) + "." + itos(GODOT_VERSION_PATCH);
}

Vector<String> api_headers() {
	return { "Accept: application/vnd.github+json", "User-Agent: ROME-Engine/" ROME_VERSION, "X-GitHub-Api-Version: 2022-11-28" };
}

String cache_root() {
	return OS::get_singleton()->get_cache_path().path_join("ROMEEngine");
}

String release_cache_path() {
	return cache_root().path_join("updates/releases.json");
}

Dictionary read_json_file(const String &p_path) {
	if (p_path.is_empty() || !FileAccess::exists(p_path)) {
		return Dictionary();
	}
	JSON json;
	if (json.parse(FileAccess::get_file_as_string(p_path)) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return json.get_data();
}

void write_json_file(const String &p_path, const Dictionary &p_data) {
	if (DirAccess::make_dir_recursive_absolute(p_path.get_base_dir()) != OK) {
		return;
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_valid()) {
		file->store_string(JSON::stringify(p_data));
	}
}

String header_value(const PackedStringArray &p_headers, const String &p_name) {
	const String prefix = p_name.to_lower() + ":";
	for (const String &header : p_headers) {
		if (header.to_lower().begins_with(prefix)) {
			return header.substr(prefix.length()).strip_edges();
		}
	}
	return String();
}

// Removes a folder this manager created in the cache. Links found inside are
// removed themselves; their targets are never visited.
bool remove_tree(const String &p_path) {
	Ref<DirAccess> folder = DirAccess::open(p_path);
	if (folder.is_null()) {
		return false;
	}
	folder->set_include_hidden(true);
	bool removed = true;
	for (const String &name : folder->get_files()) {
		removed = DirAccess::remove_absolute(p_path.path_join(name)) == OK && removed;
	}
	for (const String &name : folder->get_directories()) {
		const String child = p_path.path_join(name);
		removed = (folder->is_link(child) ? DirAccess::remove_absolute(child) == OK : remove_tree(child)) && removed;
	}
	folder.unref();
	return DirAccess::remove_absolute(p_path) == OK && removed;
}

// get_*_at() report an error for a missing folder, and most users have no
// downloads, runtimes of older versions or backups yet.
PackedStringArray folders_in(const String &p_path) {
	return DirAccess::dir_exists_absolute(p_path) ? DirAccess::get_directories_at(p_path) : PackedStringArray();
}

PackedStringArray files_in(const String &p_path) {
	return DirAccess::dir_exists_absolute(p_path) ? DirAccess::get_files_at(p_path) : PackedStringArray();
}

bool older_than(const String &p_path, double p_seconds) {
	return OS::get_singleton()->get_unix_time() - double(FileAccess::get_modified_time(p_path)) > p_seconds;
}

// A version whose installation failed is not retried automatically on every
// exit; the dialog still offers it.
String failed_version() {
	EditorSettings *settings = EditorSettings::get_singleton();
	return settings && settings->has_setting(FAILED_SETTING) ? String(settings->get_setting(FAILED_SETTING)) : String();
}

void set_failed_version(const String &p_version) {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings && failed_version() != p_version) {
		settings->set_setting(FAILED_SETTING, p_version);
		EditorSettings::save();
	}
}

Dictionary release_asset(const Dictionary &p_release, const String &p_version, const String &p_tag, const String &p_platform) {
	String expected;
	if (p_platform == "windows-x86_64") {
		expected = "ROME-Studio-" + p_version + "-windows-x86_64.exe";
	} else if (p_platform == "linux-x86_64") {
		expected = "ROME-" + p_version + "-linux-x86_64";
	} else {
		return Dictionary();
	}
	const Variant assets_value = p_release.get("assets", Array());
	if (assets_value.get_type() != Variant::ARRAY) {
		return Dictionary();
	}
	const Array assets = assets_value;
	for (int i = 0; i < assets.size(); i++) {
		if (assets[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary asset = assets[i];
		// JSON numbers arrive as floats; only whole byte counts are accepted.
		const Variant size_value = asset.get("size", Variant());
		if (asset.get("name", Variant()).get_type() != Variant::STRING || asset.get("browser_download_url", Variant()).get_type() != Variant::STRING || asset.get("digest", Variant()).get_type() != Variant::STRING || (size_value.get_type() != Variant::INT && size_value.get_type() != Variant::FLOAT)) {
			continue;
		}
		const double size_number = size_value;
		if (!(size_number > 0.0 && size_number <= double(MAX_DOWNLOAD_BYTES))) {
			continue;
		}
		const int64_t size = int64_t(size_number);
		const String name = asset.get("name", "");
		const String url = asset.get("browser_download_url", "");
		const String digest = asset.get("digest", "");
		const String expected_url = String(ROME_DOWNLOAD_ROOT) + p_tag + "/" + expected;
		const String encoded_url = String(ROME_DOWNLOAD_ROOT) + p_tag.uri_encode() + "/" + expected.uri_encode();
		if (name != expected || (url != expected_url && url != encoded_url) || !digest.begins_with("sha256:") || !valid_sha256(digest.substr(7)) || double(size) != size_number || String(asset.get("state", "uploaded")) != "uploaded") {
			continue;
		}
		Dictionary selected;
		selected["name"] = name;
		selected["url"] = url;
		selected["sha256"] = digest.substr(7).to_lower();
		selected["size"] = size;
		return selected;
	}
	return Dictionary();
}

} // namespace

bool ROMEUpdateManager::is_version_valid(const String &p_version) {
	return parse_version(p_version).valid;
}

int ROMEUpdateManager::compare_versions(const String &p_a, const String &p_b) {
	const UpdateVersion a = parse_version(p_a);
	const UpdateVersion b = parse_version(p_b);
	if (!a.valid || !b.valid) {
		return a.valid == b.valid ? 0 : a.valid ? 1 : -1;
	}
	for (int i = 0; i < 3; i++) {
		const int difference = compare_number(a.core[i], b.core[i]);
		if (difference != 0) {
			return difference;
		}
	}
	if (a.prerelease.is_empty() || b.prerelease.is_empty()) {
		return a.prerelease.is_empty() == b.prerelease.is_empty() ? 0 : a.prerelease.is_empty() ? 1 : -1;
	}
	for (int i = 0; i < MIN(a.prerelease.size(), b.prerelease.size()); i++) {
		const String &left = a.prerelease[i];
		const String &right = b.prerelease[i];
		if (left == right) {
			continue;
		}
		const bool left_numeric = numeric_identifier(left);
		const bool right_numeric = numeric_identifier(right);
		if (left_numeric && right_numeric) {
			return compare_number(left, right);
		}
		if (left_numeric != right_numeric) {
			return left_numeric ? -1 : 1;
		}
		return left > right ? 1 : -1;
	}
	return a.prerelease.size() == b.prerelease.size() ? 0 : a.prerelease.size() > b.prerelease.size() ? 1 : -1;
}

Dictionary ROMEUpdateManager::select_rome_release(const Array &p_releases, const String &p_platform, bool p_include_prereleases) {
	Dictionary selected;
	for (int i = 0; i < p_releases.size(); i++) {
		if (p_releases[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary release = p_releases[i];
		if (release.get("tag_name", Variant()).get_type() != Variant::STRING) {
			continue;
		}
		const String tag = release.get("tag_name", "");
		const UpdateVersion version = parse_version(tag);
		const bool prerelease = bool(release.get("prerelease", false)) || !version.prerelease.is_empty();
		if (bool(release.get("draft", false)) || !version.valid || (prerelease && !p_include_prereleases) || (!selected.is_empty() && compare_versions(version.version, selected["version"]) <= 0)) {
			continue;
		}
		selected.clear();
		selected["version"] = version.version;
		selected["tag_name"] = tag;
		selected["prerelease"] = prerelease;
		selected["release_url"] = "https://github.com/Pimpoli/ROME-Engine/releases/tag/" + tag.uri_encode();
		selected["asset"] = release_asset(release, version.version, tag, p_platform);
		const Variant name = release.get("name", Variant());
		selected["name"] = name.get_type() == Variant::STRING && !String(name).strip_edges().is_empty() ? String(name).strip_edges() : "ROME " + version.version;
		const Variant notes = release.get("body", Variant());
		selected["notes"] = notes.get_type() == Variant::STRING ? String(notes).strip_edges().left(MAX_NOTES_LENGTH) : String();
	}
	return selected;
}

Dictionary ROMEUpdateManager::select_godot_release(const Array &p_releases) {
	Dictionary selected;
	for (int i = 0; i < p_releases.size(); i++) {
		if (p_releases[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary release = p_releases[i];
		if (release.get("tag_name", Variant()).get_type() != Variant::STRING) {
			continue;
		}
		const String tag = release.get("tag_name", "");
		if (bool(release.get("draft", false)) || bool(release.get("prerelease", false)) || !tag.ends_with("-stable")) {
			continue;
		}
		String version = tag.trim_suffix("-stable");
		if (version.split(".").size() == 2) {
			version += ".0";
		}
		const UpdateVersion parsed = parse_version(version);
		if (!parsed.valid || !parsed.prerelease.is_empty() || (!selected.is_empty() && compare_versions(version, selected["version"]) <= 0)) {
			continue;
		}
		selected["version"] = version;
		selected["tag_name"] = tag;
	}
	return selected;
}

String ROMEUpdateManager::get_platform() {
	if (Engine::get_singleton()->get_architecture_name() != "x86_64") {
		return "unsupported";
	}
	const String os_name = OS::get_singleton()->get_name();
	return os_name == "Windows" ? "windows-x86_64" : os_name == "Linux" ? "linux-x86_64" : "unsupported";
}

ROMEUpdateManager *ROMEUpdateManager::install(Control *p_ui_base) {
	ERR_FAIL_NULL_V(p_ui_base, nullptr);
	if (singleton != nullptr) {
		return singleton;
	}
	singleton = memnew(ROMEUpdateManager);
	singleton->ui_base = p_ui_base;
	p_ui_base->add_child(singleton);
	return singleton;
}

bool ROMEUpdateManager::_preference(const String &p_key, bool p_default) const {
	EditorSettings *settings = EditorSettings::get_singleton();
	return settings && settings->has_setting(p_key) ? bool(settings->get_setting(p_key)) : p_default;
}

void ROMEUpdateManager::_set_preference(bool p_enabled, const String &p_key) {
	EditorSettings *settings = EditorSettings::get_singleton();
	if (settings) {
		settings->set_setting(p_key, p_enabled);
		EditorSettings::save();
	}
	if (p_key == PRERELEASE_SETTING && !checking) {
		check_updates(true);
	}
}

bool ROMEUpdateManager::_include_prereleases() const {
	// Alpha and beta builds follow their channel; stable builds stay on stable.
	return _preference(PRERELEASE_SETTING, !parse_version(ROME_VERSION).prerelease.is_empty());
}

void ROMEUpdateManager::_startup_check() {
	if (DisplayServer::get_singleton()->get_name() != "headless" && _preference(STARTUP_SETTING, true)) {
		check_updates(false);
	}
}

void ROMEUpdateManager::check_updates(bool p_force) {
	if (DisplayServer::get_singleton()->get_name() == "headless") {
		status["rome_status"] = "headless";
		status["rome_message"] = String::utf8("Comprobación omitida en modo sin interfaz.");
		_refresh_dialog();
		return;
	}
	if (checking || downloading || retry_msec != 0 || helper_started || helper_preparing || (!p_force && !_preference(STARTUP_SETTING, true))) {
		return;
	}
	automatic_check = !p_force;
	checking = true;
	rome_done = false;
	// The Godot base is informational: it is only queried for the open dialog.
	godot_done = !p_force;
	status["rome_status"] = "checking";
	status["rome_message"] = String::utf8("Consultando las versiones públicas de ROME...");
	if (p_force) {
		status["godot_status"] = "checking";
		status["godot_message"] = String::utf8("Consultando la última versión estable de Godot...");
	}
	const Dictionary cached = read_json_file(release_cache_path());
	const String cached_body = cached.get("body", "");
	if (!p_force && !cached_body.is_empty() && OS::get_singleton()->get_unix_time() - double(cached.get("time", 0.0)) < CHECK_INTERVAL_SECONDS) {
		rome_done = true;
		_apply_response(cached_body, false);
		return;
	}
	Vector<String> headers = api_headers();
	const String etag = cached.get("etag", "");
	if (!cached_body.is_empty() && !etag.is_empty()) {
		// An unchanged list answers 304, which does not count against the limit.
		headers.push_back("If-None-Match: " + etag);
	}
	if (rome_request->request(ROME_RELEASE_API, headers) != OK) {
		rome_done = true;
		status["rome_status"] = "error";
		status["rome_message"] = String::utf8("No se pudo iniciar la consulta de ROME. Puedes reintentar.");
	}
	if (p_force && godot_request->request(GODOT_STABLE_API, api_headers()) != OK) {
		godot_done = true;
		status["godot_status"] = "error";
		status["godot_message"] = String::utf8("No se pudo consultar la base estable de Godot.");
	}
	_refresh_dialog();
	_finish_check();
}

void ROMEUpdateManager::_request_completed(int p_result, int p_code, const PackedStringArray &p_headers, const PackedByteArray &p_body, bool p_godot) {
	if (p_godot) {
		godot_done = true;
	} else {
		rome_done = true;
	}
	const String key = p_godot ? "godot" : "rome";
	if (!p_godot && p_result == HTTPRequest::RESULT_SUCCESS && p_code == HTTPClient::RESPONSE_NOT_MODIFIED) {
		Dictionary cached = read_json_file(release_cache_path());
		const String body = cached.get("body", "");
		if (!body.is_empty()) {
			cached["time"] = OS::get_singleton()->get_unix_time();
			write_json_file(release_cache_path(), cached);
			_apply_response(body, false);
			return;
		}
	}
	if (p_result != HTTPRequest::RESULT_SUCCESS || p_code != HTTPClient::RESPONSE_OK) {
		status[key + "_status"] = "error";
		status[key + "_message"] = p_code == 403 || p_code == 429 ? String::utf8("GitHub limitó temporalmente las consultas. Inténtalo más tarde.") : String::utf8("No se pudo comprobar la versión. Revisa la conexión y vuelve a intentarlo.");
		_refresh_dialog();
		_finish_check();
		return;
	}
	const String body = String::utf8(reinterpret_cast<const char *>(p_body.ptr()), p_body.size());
	if (!p_godot) {
		Dictionary cached;
		cached["etag"] = header_value(p_headers, "ETag");
		cached["time"] = OS::get_singleton()->get_unix_time();
		cached["body"] = body;
		write_json_file(release_cache_path(), cached);
	}
	_apply_response(body, p_godot);
}

void ROMEUpdateManager::_apply_response(const String &p_body, bool p_godot) {
	const String key = p_godot ? "godot" : "rome";
	JSON json;
	if (json.parse(p_body) != OK) {
		status[key + "_status"] = "error";
		status[key + "_message"] = String::utf8("GitHub devolvió una respuesta de versiones no válida.");
		_refresh_dialog();
		_finish_check();
		return;
	}
	Array releases;
	const Variant parsed = json.get_data();
	if (parsed.get_type() == Variant::ARRAY) {
		releases = parsed;
	} else if (p_godot && parsed.get_type() == Variant::DICTIONARY) {
		releases.push_back(parsed);
	} else {
		status[key + "_status"] = "error";
		status[key + "_message"] = String::utf8("La respuesta no contiene una lista de versiones válida.");
		_refresh_dialog();
		_finish_check();
		return;
	}
	if (p_godot) {
		const Dictionary stable = select_godot_release(releases);
		status["godot_latest"] = stable;
		if (stable.is_empty()) {
			status["godot_status"] = "unavailable";
			status["godot_message"] = String::utf8("No se encontró una versión estable publicada de Godot.");
		} else {
			const String latest = stable["version"];
			const int difference = compare_versions(latest, current_godot_number());
			const bool transitional = String(GODOT_VERSION_STATUS) != "stable";
			if (difference > 0 || (difference == 0 && transitional)) {
				status["godot_status"] = "integration_available";
				status["godot_message"] = String::utf8("Godot estable disponible: ") + String(stable["tag_name"]) + String::utf8(". Su integración se distribuye mediante una nueva versión de ROME compilada y probada.");
			} else if (difference < 0) {
				status["godot_status"] = "base_ahead";
				status["godot_message"] = String::utf8("Última estable publicada: ") + String(stable["tag_name"]) + String::utf8(". La base actual de ROME es más reciente.");
			} else {
				status["godot_status"] = "current";
				status["godot_message"] = String::utf8("La base coincide con la última versión estable publicada: ") + String(stable["tag_name"]) + ".";
			}
		}
	} else {
		const Dictionary latest = select_rome_release(releases, get_platform(), _include_prereleases());
		status["rome_latest"] = latest;
		if (latest.is_empty()) {
			update.clear();
			verified = false;
			status["rome_status"] = "unavailable";
			status["rome_message"] = String::utf8("No hay versiones públicas de ROME disponibles para comprobar.");
		} else if (compare_versions(latest["version"], ROME_VERSION) <= 0) {
			update.clear();
			verified = false;
			status["rome_status"] = "current";
			status["rome_message"] = String::utf8("Tienes la versión pública más reciente de ROME para este canal.");
		} else {
			const Dictionary previous_asset = update.get("asset", Dictionary());
			const Dictionary next_asset = latest.get("asset", Dictionary());
			if (update.get("version", "") != latest.get("version", "") || previous_asset.get("sha256", "") != next_asset.get("sha256", "")) {
				verified = false;
			}
			update = latest;
			const Dictionary asset = latest["asset"];
			status["rome_status"] = asset.is_empty() ? "no_asset" : "update_available";
			status["rome_message"] = "ROME " + String(latest["version"]) + String::utf8(" está disponible.") + (asset.is_empty() ? String::utf8(" El paquete verificado para esta plataforma todavía no está publicado.") : String::utf8(" Puedes descargarla sin cerrar tu proyecto."));
		}
	}
	_refresh_dialog();
	_finish_check();
}

void ROMEUpdateManager::_finish_check() {
	if (!checking || !rome_done || !godot_done) {
		return;
	}
	checking = false;
	_refresh_dialog();
	if (String(status.get("rome_status", "")) == "update_available") {
		const String version = update["version"];
		const bool automatic_install = _preference(DOWNLOAD_SETTING, true) && _preference(INSTALL_ON_EXIT_SETTING, true) && EditorNode::get_singleton();
		if (automatic_check && version != notified_version) {
			notified_version = version;
			if (EditorToaster::get_singleton()) {
				EditorToaster::get_singleton()->popup_str("ROME " + version + (automatic_install ? String::utf8(" está disponible: se descarga en segundo plano y se instalará al cerrar el editor. Para instalarla ya, abre Ayuda > Actualizaciones de ROME.") : String::utf8(" disponible. Abre Ayuda > Actualizaciones de ROME para actualizar.")));
			} else if (!dialog->is_visible()) {
				dialog->popup_centered(Size2i(640, 600) * EDSCALE);
			}
		}
		if (_preference(DOWNLOAD_SETTING, true) && !verified && !downloading && retry_msec == 0 && version != failed_version()) {
			_download_update();
		}
	}
	if (dialog->is_visible() && String(status.get("godot_status", "idle")) == "idle") {
		check_updates(true);
	}
	emit_signal("status_changed");
}

void ROMEUpdateManager::_set_action_error(const String &p_message) {
	status["action_error"] = p_message;
	_refresh_dialog();
}

void ROMEUpdateManager::_download_update() {
	if (downloading || retry_msec != 0 || helper_started || helper_preparing || update.is_empty()) {
		return;
	}
	const Dictionary asset = update.get("asset", Dictionary());
	if (asset.is_empty()) {
		return;
	}
	const String cache = cache_root().path_join("updates").path_join(update["version"]);
	if (DirAccess::make_dir_recursive_absolute(cache) != OK) {
		_set_action_error(String::utf8("No se pudo crear la carpeta de descarga en la caché del usuario."));
		return;
	}
	downloaded_path = cache.path_join(asset["name"]);
	partial_path = downloaded_path + ".partial";
	status["action_error"] = "";
	if (FileAccess::exists(downloaded_path) && FileAccess::get_size(downloaded_path) == uint64_t(int64_t(asset["size"])) && FileAccess::get_sha256(downloaded_path).to_lower() == String(asset["sha256"])) {
		verified = true;
		_refresh_dialog();
		emit_signal("status_changed");
		return;
	}
	verified = false;
	download_attempts = 0;
	original_hash = String();
	_request_download();
}

// GitHub may serve a release slowly and cut the transfer after about fifteen
// minutes, so a package can take several requests and several sessions: what
// arrived stays in .partial and each request asks only for the rest.
void ROMEUpdateManager::_request_download() {
	const Dictionary asset = update.get("asset", Dictionary());
	const int64_t total = asset.get("size", 0);
	const String record = partial_path + ".json";
	int64_t offset = FileAccess::exists(partial_path) ? int64_t(FileAccess::get_size(partial_path)) : 0;
	if (offset > 0 && (String(read_json_file(record).get("sha256", "")) != String(asset["sha256"]) || offset > total)) {
		// Left by another package.
		DirAccess::remove_absolute(partial_path);
		offset = 0;
	}
	if (offset == total) {
		_finish_download();
		return;
	}
	Dictionary owner;
	owner["sha256"] = asset["sha256"];
	owner["size"] = total;
	write_json_file(record, owner);
	Vector<String> headers = { "User-Agent: ROME-Engine/" ROME_VERSION };
	if (offset > 0) {
		headers.push_back("Range: bytes=" + itos(offset) + "-");
	}
	download_offset = offset;
	downloading = true;
	retry_msec = 0;
	progress_bytes = -1;
	progress_msec = OS::get_singleton()->get_ticks_msec();
	download_request->set_download_file(partial_path);
	// Appends only to a 206 answer; a server that sends the whole file rewrites it.
	download_request->set_append_to_download_file(offset > 0);
	if (download_request->request(asset["url"], headers) != OK) {
		downloading = false;
		_set_action_error(String::utf8("No se pudo iniciar la descarga. Puedes reintentar."));
	}
	_refresh_dialog();
}

void ROMEUpdateManager::_retry_download(const String &p_reason) {
	const int64_t size = FileAccess::exists(partial_path) ? int64_t(FileAccess::get_size(partial_path)) : 0;
	// Only attempts that bring nothing count towards giving up.
	if (size > download_offset) {
		download_attempts = 0;
	}
	download_attempts++;
	if (download_attempts > DOWNLOAD_RETRIES) {
		_set_action_error(p_reason + String::utf8(" Lo descargado se conserva y la descarga seguirá desde ahí; puedes reintentar."));
		return;
	}
	const uint64_t delay = MIN(uint64_t(60000), uint64_t(5000) << MIN(download_attempts - 1, 4));
	retry_msec = OS::get_singleton()->get_ticks_msec() + delay;
	status["action_error"] = "";
	status["download_message"] = p_reason + vformat(String::utf8(" Se reanuda en %d s; lo descargado se conserva."), int(delay / 1000));
	_refresh_dialog();
}

void ROMEUpdateManager::_download_completed(int p_result, int p_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (!downloading) {
		return;
	}
	downloading = false;
	const Dictionary asset = update.get("asset", Dictionary());
	if (asset.is_empty()) {
		return;
	}
	if (p_result == HTTPRequest::RESULT_SUCCESS && p_code >= 400 && p_code < 500 && p_code != 408 && p_code != 416 && p_code != 429) {
		_set_action_error(vformat(String::utf8("El paquete no está disponible en el servidor de descargas (HTTP %d). Tu instalación actual sigue igual."), p_code));
		return;
	}
	if (p_code == HTTPClient::RESPONSE_REQUESTED_RANGE_NOT_SATISFIABLE) {
		DirAccess::remove_absolute(partial_path);
	}
	const bool answered = p_result == HTTPRequest::RESULT_SUCCESS && (p_code == HTTPClient::RESPONSE_OK || p_code == HTTPClient::RESPONSE_PARTIAL_CONTENT);
	const int64_t size = FileAccess::exists(partial_path) ? int64_t(FileAccess::get_size(partial_path)) : 0;
	if (!answered || size < int64_t(asset["size"])) {
		_retry_download(String::utf8("La conexión con el servidor de descargas se cortó."));
		return;
	}
	_finish_download();
}

void ROMEUpdateManager::_finish_download() {
	const Dictionary asset = update.get("asset", Dictionary());
	const bool correct_size = FileAccess::exists(partial_path) && FileAccess::get_size(partial_path) == uint64_t(int64_t(asset["size"]));
	if (!correct_size || FileAccess::get_sha256(partial_path).to_lower() != String(asset["sha256"])) {
		DirAccess::remove_absolute(partial_path);
		DirAccess::remove_absolute(partial_path + ".json");
		_set_action_error(String::utf8("El archivo descargado no coincide con el SHA-256 publicado. Se descartó; vuelve a descargarlo."));
		return;
	}
	if (FileAccess::exists(downloaded_path) && DirAccess::remove_absolute(downloaded_path) != OK) {
		_set_action_error(String::utf8("No se pudo sustituir una descarga anterior en la caché."));
		return;
	}
	if (DirAccess::rename_absolute(partial_path, downloaded_path) != OK) {
		_set_action_error(String::utf8("No se pudo preparar el archivo descargado para actualizar."));
		return;
	}
	DirAccess::remove_absolute(partial_path + ".json");
	verified = true;
	status["action_error"] = "";
	_refresh_dialog();
	if (EditorToaster::get_singleton() && _preference(INSTALL_ON_EXIT_SETTING, true)) {
		EditorToaster::get_singleton()->popup_str("ROME " + String(update["version"]) + String::utf8(" ya está descargada y verificada. Se instalará al cerrar el editor."));
	}
	emit_signal("status_changed");
}

void ROMEUpdateManager::_poll_download(uint64_t p_now) {
	const Dictionary asset = update.get("asset", Dictionary());
	const int64_t total = asset.get("size", 0);
	const int64_t bytes = download_offset + download_request->get_downloaded_bytes();
	progress->set_value(total > 0 ? double(bytes) / double(total) * 100.0 : 0.0);
	// The request has no total time limit; only a connection without data is retried.
	if (bytes != progress_bytes) {
		progress_bytes = bytes;
		progress_msec = p_now;
	} else if (p_now - progress_msec > DOWNLOAD_STALL_MSEC) {
		download_request->cancel_request();
		downloading = false;
		_retry_download(String::utf8("No llegaron datos durante un minuto."));
	}
}

void ROMEUpdateManager::cancel_download() {
	if (!downloading && retry_msec == 0) {
		return;
	}
	if (downloading) {
		download_request->cancel_request();
	}
	downloading = false;
	retry_msec = 0;
	verified = false;
	status["action_error"] = String::utf8("Descarga en pausa. Lo descargado se conserva y, al reanudarla, sigue desde ahí.");
	_refresh_dialog();
}

bool ROMEUpdateManager::_launch_helper(bool p_restart) {
	const Dictionary asset = update.get("asset", Dictionary());
	if (asset.is_empty() || !verified || !FileAccess::exists(downloaded_path)) {
		return false;
	}
	const String original = OS::get_singleton()->get_executable_path();
	if (!original.get_file().to_lower().begins_with("rome")) {
		_set_action_error(String::utf8("Abre ROME Studio.exe para aplicar la actualización al ejecutable distribuido."));
		return false;
	}
	// Hashed each time: another copy of ROME may have installed the update already.
	original_hash = FileAccess::get_sha256(original);
	if (!valid_sha256(original_hash)) {
		_set_action_error(String::utf8("No se pudo verificar el ejecutable actual para reemplazarlo de forma segura."));
		return false;
	}
	if (original_hash == String(asset["sha256"])) {
		_set_action_error(String::utf8("La versión nueva ya está instalada; se usará la próxima vez que abras ROME."));
		return false;
	}
	if (get_platform() == "linux-x86_64" && FileAccess::set_unix_permissions(downloaded_path, BitField<FileAccess::UnixPermissionFlags>(0755)) != OK) {
		_set_action_error(String::utf8("No se pudo habilitar la ejecución del archivo descargado."));
		return false;
	}
	const String staging = downloaded_path.get_base_dir().path_join("helper-" + itos(OS::get_singleton()->get_process_id()) + "-" + itos(OS::get_singleton()->get_ticks_usec()));
	if (DirAccess::dir_exists_absolute(staging) || DirAccess::make_dir_absolute(staging) != OK) {
		_set_action_error(String::utf8("No se pudo preparar una carpeta aislada para el instalador."));
		return false;
	}
	install_ticket = staging.path_join("apply.ticket");
	helper_status_path = staging.path_join("status.json");
	Ref<FileAccess> ticket = FileAccess::open(install_ticket, FileAccess::WRITE);
	if (ticket.is_null()) {
		_set_action_error(String::utf8("No se pudo autorizar el reinicio seguro de la actualización."));
		return false;
	}
	ticket->store_string(asset["sha256"]);
	ticket.unref();
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("--path");
	arguments.push_back(staging);
	arguments.push_back("--");
	arguments.push_back("--rome-update-helper");
	arguments.push_back("--target");
	arguments.push_back(original);
	arguments.push_back("--wait-pid");
	arguments.push_back(itos(OS::get_singleton()->get_process_id()));
	arguments.push_back("--sha256");
	arguments.push_back(asset["sha256"]);
	arguments.push_back("--original-sha256");
	arguments.push_back(original_hash);
	arguments.push_back("--cancel-file");
	arguments.push_back(install_ticket);
	arguments.push_back("--status-file");
	arguments.push_back(helper_status_path);
	if (p_restart) {
		arguments.push_back("--restart");
		if (EditorNode::get_singleton()) {
			arguments.push_back("--project");
			arguments.push_back(ProjectSettings::get_singleton()->get_resource_path());
		}
	}
	ProcessID pid = 0;
	if (OS::get_singleton()->create_process(downloaded_path, arguments, &pid) != OK) {
		_discard_ticket();
		_set_action_error(String::utf8("No se pudo iniciar el instalador de la actualización. Tu proyecto permanece abierto."));
		return false;
	}
	helper_pid = pid;
	helper_launch_msec = OS::get_singleton()->get_ticks_msec();
	next_poll_msec = helper_launch_msec + 250;
	return true;
}

void ROMEUpdateManager::_install_update() {
	if (!verified || helper_started || helper_preparing) {
		return;
	}
	if (!quit_callback.is_valid()) {
		_set_action_error(String::utf8("No está disponible el cierre seguro del editor en esta ventana."));
		return;
	}
	const Dictionary asset = update.get("asset", Dictionary());
	if (asset.is_empty() || !FileAccess::exists(downloaded_path) || FileAccess::get_sha256(downloaded_path).to_lower() != String(asset["sha256"])) {
		verified = false;
		_set_action_error(String::utf8("La descarga cambió después de verificarse. Descárgala de nuevo."));
		return;
	}
	status["action_error"] = "";
	if (!_launch_helper(true)) {
		return;
	}
	// The editor closes only once the helper reports that everything is prepared.
	helper_preparing = true;
	_refresh_dialog();
}

void ROMEUpdateManager::_install_on_exit() {
	EditorNode *editor = EditorNode::get_singleton();
	if (!verified || helper_started || helper_preparing || !editor || !editor->is_exiting() || !_preference(INSTALL_ON_EXIT_SETTING, true) || DisplayServer::get_singleton()->get_name() == "headless") {
		return;
	}
	if (String(update.get("version", "")) == failed_version()) {
		return;
	}
	// Without --restart: the next start of ROME is already the new version.
	_launch_helper(false);
}

void ROMEUpdateManager::_poll_helper() {
	const Dictionary state = read_json_file(helper_status_path);
	const String stage = state.get("stage", "");
	const bool alive = helper_pid != 0 && OS::get_singleton()->is_process_running(helper_pid);
	if (helper_preparing) {
		if (stage == "waiting") {
			helper_preparing = false;
			helper_started = true;
			_watch_quit_cancellation();
			_refresh_dialog();
			quit_callback.call_deferred();
			callable_mp(this, &ROMEUpdateManager::_watch_quit_cancellation).call_deferred();
			return;
		}
		if (alive && stage != "failed" && stage != "cancelled" && OS::get_singleton()->get_ticks_msec() - helper_launch_msec < HELPER_READY_MSEC) {
			return;
		}
		if (alive) {
			OS::get_singleton()->kill(helper_pid);
		}
		helper_preparing = false;
		_discard_ticket();
		const String message = state.get("message", "");
		_set_action_error(message.is_empty() ? String::utf8("El instalador no pudo prepararse. Tu proyecto sigue abierto; vuelve a intentarlo.") : message);
		return;
	}
	if (!alive) {
		// The helper gave up while the editor stayed open (cancelled, timeout or error).
		helper_started = false;
		_discard_ticket();
		const String message = state.get("message", "");
		_set_action_error(message.is_empty() ? String::utf8("La instalación terminó sin cambiar ROME. Puedes volver a intentarlo.") : message);
		emit_signal("status_changed");
	}
}

void ROMEUpdateManager::_discard_ticket() {
	if (!install_ticket.is_empty() && FileAccess::exists(install_ticket)) {
		DirAccess::remove_absolute(install_ticket);
	}
}

void ROMEUpdateManager::_watch_quit_cancellation() {
	if (!helper_started || !ui_base) {
		return;
	}
	const TypedArray<Node> dialogs = ui_base->find_children("*", "ConfirmationDialog", true, false);
	const Callable cancel = callable_mp(this, &ROMEUpdateManager::cancel_installation);
	for (int i = 0; i < dialogs.size(); i++) {
		Node *confirmation = Object::cast_to<Node>(dialogs[i]);
		if (confirmation && !confirmation->is_connected("canceled", cancel)) {
			confirmation->connect("canceled", cancel);
		}
	}
}

void ROMEUpdateManager::cancel_installation() {
	if (!helper_started && !helper_preparing) {
		return;
	}
	_discard_ticket();
	helper_started = false;
	helper_preparing = false;
	status["action_error"] = String::utf8("Actualización cancelada. Tu proyecto sigue abierto y la descarga verificada se conserva.");
	_refresh_dialog();
}

// The experience was updated before the editor existed; repeat it where the
// creator looks.
void ROMEUpdateManager::_report_experience_update() {
	bool failed = false;
	const String message = RomeProjectTemplate::take_update_message(&failed);
	if (message.is_empty()) {
		return;
	}
	if (failed) {
		WARN_PRINT(message);
	} else {
		print_line(message);
	}
	if (EditorToaster::get_singleton() && DisplayServer::get_singleton()->get_name() != "headless") {
		EditorToaster::get_singleton()->popup_str(message, failed ? EditorToaster::SEVERITY_WARNING : EditorToaster::SEVERITY_INFO);
	}
}

// Tells the user how the last installation ended. It runs in the new version
// after a restart, or in the old one when the helper had to reopen it.
void ROMEUpdateManager::_report_previous_installs() {
	if (DisplayServer::get_singleton()->get_name() == "headless") {
		return;
	}
	const String updates = cache_root().path_join("updates");
	const String own_target = OS::get_singleton()->get_executable_path().replace("\\", "/").simplify_path().to_lower();
	const double process_start = OS::get_singleton()->get_unix_time() - OS::get_singleton()->get_ticks_msec() / 1000.0;
	for (const String &version : folders_in(updates)) {
		const String folder = updates.path_join(version);
		for (const String &helper : folders_in(folder)) {
			const String staging = folder.path_join(helper);
			const String status_file = staging.path_join("status.json");
			if (!helper.begins_with("helper-") || (helper_status_path.get_base_dir() == staging)) {
				continue;
			}
			const Dictionary state = read_json_file(status_file);
			const String stage = state.get("stage", "");
			const int code = state.get("code", -1);
			const bool finished = stage == "done" || stage == "failed" || stage == "cancelled";
			const String ticket = staging.path_join("apply.ticket");
			if (!finished && ((FileAccess::exists(status_file) && !older_than(status_file, 3600.0)) || (FileAccess::exists(ticket) && !older_than(ticket, 120.0)))) {
				// Another ROME may still be closing while its helper waits.
				continue;
			}
			const String target = state.get("target", "");
			if (!target.is_empty() && target.to_lower() != own_target) {
				// Another copy of ROME was updated; it reports its own result.
				if (!FileAccess::exists(status_file) || older_than(status_file, 7 * 86400.0)) {
					remove_tree(staging);
				}
				continue;
			}
			if (stage == "done" || code == 8) {
				const int difference = compare_versions(version, ROME_VERSION);
				if (difference > 0 && process_start < double(state.get("time", 0.0)) + 5.0) {
					// This process started from the old file while the update was being
					// installed (quit to the hub, reload); the next start reports it.
					continue;
				}
				if (difference == 0) {
					set_failed_version(String());
					if (EditorToaster::get_singleton()) {
						EditorToaster::get_singleton()->popup_str(String::utf8("ROME se actualizó a la versión ") + version + ".");
					}
				} else if (difference > 0) {
					// The package was named for a newer version than it contains: stop
					// installing it automatically instead of repeating it on every exit.
					set_failed_version(version);
					status["action_error"] = String::utf8("Se instaló el paquete de ROME ") + version + String::utf8(", pero el editor sigue diciendo ") + ROME_VERSION + String::utf8(". Las actualizaciones automáticas a esa versión quedan en pausa; avisa del problema.");
					_refresh_dialog();
					dialog->popup_centered(Size2i(640, 600) * EDSCALE);
				}
			} else if (stage == "failed" && compare_versions(version, ROME_VERSION) > 0) {
				set_failed_version(version);
				status["action_error"] = String::utf8("No se pudo instalar ROME ") + version + ": " + String(state.get("message", "")) + String::utf8(" Puedes volver a intentarlo con «Actualizar y reiniciar».");
				_refresh_dialog();
				dialog->popup_centered(Size2i(640, 600) * EDSCALE);
			}
			remove_tree(staging);
		}
	}
}

// Keeps the cache to what this version still needs. Experiences carry their own
// copy of the runtime, so older runtimes and installed downloads can go.
void ROMEUpdateManager::_cleanup_cache() {
	if (DisplayServer::get_singleton()->get_name() == "headless") {
		return;
	}
	const String root = cache_root();
	const String pending = update.get("version", "");
	for (const String &version : folders_in(root.path_join("updates"))) {
		if (parse_version(version).valid && version != pending && compare_versions(version, ROME_VERSION) <= 0) {
			remove_tree(root.path_join("updates").path_join(version));
		}
	}
	for (const String &name : folders_in(root.path_join("runtime"))) {
		const int dash = name.rfind_char('-');
		if (dash > 0 && parse_version(name.left(dash)).valid && compare_versions(name.left(dash), ROME_VERSION) < 0) {
			remove_tree(root.path_join("runtime").path_join(name));
		}
	}
	const String backups = root.path_join("backups");
	String newest;
	uint64_t newest_time = 0;
	for (const String &file : files_in(backups)) {
		const uint64_t modified = FileAccess::get_modified_time(backups.path_join(file));
		if (file.ends_with(".bak") && modified >= newest_time) {
			newest = file;
			newest_time = modified;
		}
	}
	for (const String &file : files_in(backups)) {
		if (file.ends_with(".bak") && file != newest) {
			DirAccess::remove_absolute(backups.path_join(file));
		}
	}
	// Leftovers of an interrupted installation next to this executable.
	const String executable = OS::get_singleton()->get_executable_path();
	const String folder = executable.get_base_dir();
	const String name = executable.get_file();
	for (const String &file : files_in(folder)) {
		const bool leftover = (file.begins_with(name + ".rome-update-") || file.begins_with(name + ".rome-rollback-")) && file.ends_with(".tmp");
		if ((leftover || file.begins_with("." + name + ".rome-write-test-")) && older_than(folder.path_join(file), 3600.0)) {
			DirAccess::remove_absolute(folder.path_join(file));
		}
	}
}

void ROMEUpdateManager::_open_release_page() {
	const String url = update.get("release_url", "");
	if (url.begins_with("https://github.com/Pimpoli/ROME-Engine/releases/")) {
		OS::get_singleton()->shell_open(url);
	}
}

Dictionary ROMEUpdateManager::get_status() const {
	Dictionary result = status.duplicate(true);
	result["checking"] = checking;
	result["downloading"] = downloading;
	result["retry_pending"] = retry_msec != 0;
	result["download_offset"] = download_offset;
	result["download_attempts"] = download_attempts;
	result["verified"] = verified;
	result["helper_preparing"] = helper_preparing;
	result["helper_started"] = helper_started;
	result["download_path"] = downloaded_path;
	result["platform"] = get_platform();
	result["include_prereleases"] = _include_prereleases();
	result["install_on_exit"] = _preference(INSTALL_ON_EXIT_SETTING, true);
	return result;
}

void ROMEUpdateManager::_refresh_dialog() {
	rome_label->set_text(String::utf8("Versión actual: ") + ROME_VERSION + "\n" + String(status.get("rome_message", String::utf8("Comprueba si hay nuevas versiones de ROME."))));
	base_label->set_text(String::utf8("Base actual: Godot ") + GODOT_VERSION_FULL_CONFIG + "\n" + String(status.get("godot_message", String::utf8("Se consulta al abrir este diálogo; solo versiones estables del núcleo."))));
	const String error = status.get("action_error", "");
	const bool on_exit = _preference(INSTALL_ON_EXIT_SETTING, true) && EditorNode::get_singleton();
	String action;
	if (!error.is_empty()) {
		action = error;
	} else if (helper_preparing) {
		action = String::utf8("Preparando la instalación... El editor se cerrará en cuanto esté lista.");
	} else if (helper_started) {
		action = String::utf8("La actualización espera a que se cierre el editor. Guarda tu trabajo en los avisos de cierre.");
	} else if (retry_msec != 0) {
		action = status.get("download_message", "");
	} else if (downloading) {
		action = String::utf8("Descargando el paquete de ROME en segundo plano...") + (download_offset > 0 ? String::utf8(" Sigue desde lo que ya se había descargado.") : String());
	} else if (verified) {
		action = on_exit ? String::utf8("Descarga verificada mediante SHA-256. Se instalará al cerrar el editor; «Actualizar y reiniciar» la instala ahora.") : String::utf8("Descarga verificada mediante SHA-256. Actualizar y reiniciar cerrará el editor con los avisos de guardado habituales.");
	} else {
		action = String::utf8("La consulta y la descarga permiten seguir trabajando. La instalación empieza al pulsar Actualizar y reiniciar o al cerrar el editor.");
	}
	action_label->set_text(action);
	const String notes = update.get("notes", "");
	notes_label->set_visible(!notes.is_empty());
	if (!notes.is_empty()) {
		notes_label->set_text(String::utf8("Novedades de ") + String(update.get("name", "ROME")) + ":\n\n" + notes);
	}
	release_button->set_visible(!String(update.get("release_url", "")).is_empty());
	const bool available = !update.is_empty() && !Dictionary(update.get("asset", Dictionary())).is_empty() && compare_versions(update["version"], ROME_VERSION) > 0;
	const bool transferring = downloading || retry_msec != 0;
	const bool busy = checking || transferring || helper_started || helper_preparing;
	check_button->set_disabled(busy);
	download_button->set_visible(available && !verified);
	download_button->set_disabled(busy);
	install_button->set_visible(verified);
	install_button->set_disabled(helper_started || helper_preparing || !quit_callback.is_valid());
	cancel_button->set_visible(transferring);
	progress->set_visible(transferring);
	install_on_exit_toggle->set_visible(EditorNode::get_singleton() != nullptr);
	prerelease_toggle->set_pressed_no_signal(_include_prereleases());
}

void ROMEUpdateManager::show_update_dialog() {
	if (DisplayServer::get_singleton()->get_name() == "headless") {
		return;
	}
	_refresh_dialog();
	dialog->popup_centered(Size2i(640, 600) * EDSCALE);
	if (!checking && (String(status.get("rome_status", "idle")) == "idle" || String(status.get("godot_status", "idle")) == "idle")) {
		check_updates(true);
	}
}

void ROMEUpdateManager::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		cleanup_msec = OS::get_singleton()->get_ticks_msec() + 30000;
		callable_mp(this, &ROMEUpdateManager::_report_previous_installs).call_deferred();
		callable_mp(this, &ROMEUpdateManager::_report_experience_update).call_deferred();
		callable_mp(this, &ROMEUpdateManager::_startup_check).call_deferred();
	} else if (p_what == NOTIFICATION_PROCESS) {
		const uint64_t now = OS::get_singleton()->get_ticks_msec();
		if (downloading) {
			_poll_download(now);
		} else if (retry_msec != 0 && now >= retry_msec) {
			retry_msec = 0;
			_request_download();
		}
		if ((helper_preparing || helper_started) && now >= next_poll_msec) {
			next_poll_msec = now + 250;
			_poll_helper();
		}
		if (!cache_cleaned && cleanup_msec != 0 && now >= cleanup_msec) {
			// Late enough for the helper of a just-finished update to have exited.
			cache_cleaned = true;
			_cleanup_cache();
		}
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		_install_on_exit();
	}
}

void ROMEUpdateManager::_bind_methods() {
	ClassDB::bind_static_method("ROMEUpdateManager", D_METHOD("get_singleton"), &ROMEUpdateManager::get_singleton);
	ClassDB::bind_static_method("ROMEUpdateManager", D_METHOD("is_version_valid", "version"), &ROMEUpdateManager::is_version_valid);
	ClassDB::bind_static_method("ROMEUpdateManager", D_METHOD("compare_versions", "a", "b"), &ROMEUpdateManager::compare_versions);
	ClassDB::bind_static_method("ROMEUpdateManager", D_METHOD("select_rome_release", "releases", "platform", "include_prereleases"), &ROMEUpdateManager::select_rome_release, DEFVAL(true));
	ClassDB::bind_static_method("ROMEUpdateManager", D_METHOD("select_godot_release", "releases"), &ROMEUpdateManager::select_godot_release);
	ClassDB::bind_static_method("ROMEUpdateManager", D_METHOD("get_platform"), &ROMEUpdateManager::get_platform);
	ClassDB::bind_method(D_METHOD("check_updates", "force"), &ROMEUpdateManager::check_updates, DEFVAL(true));
	ClassDB::bind_method(D_METHOD("show_update_dialog"), &ROMEUpdateManager::show_update_dialog);
	ClassDB::bind_method(D_METHOD("cancel_download"), &ROMEUpdateManager::cancel_download);
	ClassDB::bind_method(D_METHOD("cancel_installation"), &ROMEUpdateManager::cancel_installation);
	ClassDB::bind_method(D_METHOD("get_status"), &ROMEUpdateManager::get_status);
	ADD_SIGNAL(MethodInfo("status_changed"));
}

ROMEUpdateManager::ROMEUpdateManager() {
	set_name("ROMEUpdateManager");
	status["current_rome_version"] = ROME_VERSION;
	status["current_godot_version"] = GODOT_VERSION_FULL_CONFIG;
	status["rome_status"] = "idle";
	status["godot_status"] = "idle";
	rome_request = memnew(HTTPRequest);
	godot_request = memnew(HTTPRequest);
	download_request = memnew(HTTPRequest);
	for (HTTPRequest *request : { rome_request, godot_request, download_request }) {
		add_child(request);
		request->set_use_threads(true);
		// Downloads only fail when they stall (see _poll_download), never for being slow.
		request->set_timeout(request == download_request ? 0.0 : 25.0);
		request->set_max_redirects(4);
		request->set_body_size_limit(request == download_request ? MAX_DOWNLOAD_BYTES : 16 * 1024 * 1024);
	}
	download_request->set_accept_gzip(false);
	// What arrived is kept so a cut transfer continues (see _request_download).
	download_request->set_keep_partial_download(true);
	rome_request->connect("request_completed", callable_mp(this, &ROMEUpdateManager::_request_completed).bind(false));
	godot_request->connect("request_completed", callable_mp(this, &ROMEUpdateManager::_request_completed).bind(true));
	download_request->connect("request_completed", callable_mp(this, &ROMEUpdateManager::_download_completed));
	dialog = memnew(AcceptDialog);
	dialog->set_name("ROMEUpdateDialog");
	dialog->set_title(String::utf8("Actualizaciones de ROME"));
	dialog->set_ok_button_text("Cerrar");
	add_child(dialog);
	VBoxContainer *layout = memnew(VBoxContainer);
	layout->set_custom_minimum_size(Size2(580, 0) * EDSCALE);
	layout->add_theme_constant_override("separation", 12 * EDSCALE);
	dialog->add_child(layout);
	auto section = [layout](const String &p_title) {
		Label *title = memnew(Label(p_title));
		title->add_theme_font_size_override("font_size", 16 * EDSCALE);
		layout->add_child(title);
		Label *body = memnew(Label);
		body->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		layout->add_child(body);
		return body;
	};
	rome_label = section("ROME Engine");
	notes_label = memnew(RichTextLabel);
	notes_label->set_name("ROMEReleaseNotes");
	notes_label->set_use_bbcode(false);
	notes_label->set_selection_enabled(true);
	notes_label->set_custom_minimum_size(Size2(0, 150) * EDSCALE);
	notes_label->set_visible(false);
	layout->add_child(notes_label);
	release_button = memnew(Button);
	release_button->set_name("ROMEReleasePage");
	release_button->set_text(String::utf8("Ver la versión en GitHub"));
	release_button->set_flat(true);
	release_button->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	release_button->set_visible(false);
	release_button->connect("pressed", callable_mp(this, &ROMEUpdateManager::_open_release_page));
	layout->add_child(release_button);
	layout->add_child(memnew(HSeparator));
	base_label = section(String::utf8("Base del motor"));
	layout->add_child(memnew(HSeparator));
	action_label = section(String::utf8("Descarga e instalación"));
	progress = memnew(ProgressBar);
	progress->set_custom_minimum_size(Size2(0, 18) * EDSCALE);
	layout->add_child(progress);
	HBoxContainer *buttons = memnew(HBoxContainer);
	layout->add_child(buttons);
	check_button = memnew(Button);
	check_button->set_name("ROMECheckUpdates");
	check_button->set_text("Comprobar");
	check_button->connect("pressed", callable_mp(this, &ROMEUpdateManager::check_updates).bind(true));
	buttons->add_child(check_button);
	download_button = memnew(Button);
	download_button->set_name("ROMEDownloadUpdate");
	download_button->set_text(String::utf8("Descargar actualización"));
	download_button->connect("pressed", callable_mp(this, &ROMEUpdateManager::_download_update));
	buttons->add_child(download_button);
	install_button = memnew(Button);
	install_button->set_name("ROMEInstallUpdate");
	install_button->set_text("Actualizar y reiniciar");
	install_button->connect("pressed", callable_mp(this, &ROMEUpdateManager::_install_update));
	buttons->add_child(install_button);
	cancel_button = memnew(Button);
	cancel_button->set_text("Cancelar descarga");
	cancel_button->connect("pressed", callable_mp(this, &ROMEUpdateManager::cancel_download));
	buttons->add_child(cancel_button);
	startup_toggle = memnew(CheckBox);
	startup_toggle->set_text("Comprobar actualizaciones al iniciar");
	startup_toggle->set_pressed(_preference(STARTUP_SETTING, true));
	startup_toggle->connect("toggled", callable_mp(this, &ROMEUpdateManager::_set_preference).bind(STARTUP_SETTING));
	layout->add_child(startup_toggle);
	auto_download_toggle = memnew(CheckBox);
	auto_download_toggle->set_text(String::utf8("Descargar nuevas versiones automáticamente"));
	auto_download_toggle->set_pressed(_preference(DOWNLOAD_SETTING, true));
	auto_download_toggle->connect("toggled", callable_mp(this, &ROMEUpdateManager::_set_preference).bind(DOWNLOAD_SETTING));
	layout->add_child(auto_download_toggle);
	install_on_exit_toggle = memnew(CheckBox);
	install_on_exit_toggle->set_text(String::utf8("Instalar la versión descargada al cerrar el editor"));
	install_on_exit_toggle->set_pressed(_preference(INSTALL_ON_EXIT_SETTING, true));
	install_on_exit_toggle->connect("toggled", callable_mp(this, &ROMEUpdateManager::_set_preference).bind(INSTALL_ON_EXIT_SETTING));
	layout->add_child(install_on_exit_toggle);
	prerelease_toggle = memnew(CheckBox);
	prerelease_toggle->set_text(String::utf8("Incluir versiones alfa y beta"));
	prerelease_toggle->set_pressed(_include_prereleases());
	prerelease_toggle->connect("toggled", callable_mp(this, &ROMEUpdateManager::_set_preference).bind(PRERELEASE_SETTING));
	layout->add_child(prerelease_toggle);
	Label *privacy = memnew(Label(String::utf8("Consultas anónimas a GitHub Releases; no se envían proyectos, scripts ni credenciales.")));
	privacy->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	privacy->add_theme_font_size_override("font_size", 11 * EDSCALE);
	layout->add_child(privacy);
	_refresh_dialog();
	set_process(true);
}

ROMEUpdateManager::~ROMEUpdateManager() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
