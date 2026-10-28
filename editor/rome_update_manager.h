#pragma once

#include "core/os/process_id.h"
#include "scene/main/node.h"

class AcceptDialog;
class Button;
class CheckBox;
class Control;
class HTTPRequest;
class Label;
class ProgressBar;
class RichTextLabel;

// Owns ROME release checks. The inherited Godot ABI is informational here;
// official Godot executables are never installed over this application.
class ROMEUpdateManager : public Node {
	GDCLASS(ROMEUpdateManager, Node);

	inline static ROMEUpdateManager *singleton = nullptr;
	HTTPRequest *rome_request = nullptr;
	HTTPRequest *godot_request = nullptr;
	HTTPRequest *download_request = nullptr;
	AcceptDialog *dialog = nullptr;
	Label *rome_label = nullptr;
	Label *base_label = nullptr;
	Label *action_label = nullptr;
	RichTextLabel *notes_label = nullptr;
	Button *release_button = nullptr;
	Button *check_button = nullptr;
	Button *download_button = nullptr;
	Button *install_button = nullptr;
	Button *cancel_button = nullptr;
	CheckBox *startup_toggle = nullptr;
	CheckBox *auto_download_toggle = nullptr;
	CheckBox *install_on_exit_toggle = nullptr;
	CheckBox *prerelease_toggle = nullptr;
	ProgressBar *progress = nullptr;
	Control *ui_base = nullptr;
	Dictionary status;
	Dictionary update;
	Callable quit_callback;
	String partial_path;
	String downloaded_path;
	String notified_version;
	String install_ticket;
	String helper_status_path;
	String original_hash;
	ProcessID helper_pid = 0;
	uint64_t helper_launch_msec = 0;
	uint64_t next_poll_msec = 0;
	uint64_t cleanup_msec = 0;
	uint64_t progress_msec = 0;
	int64_t progress_bytes = -1;
	bool checking = false;
	bool rome_done = false;
	bool godot_done = false;
	bool downloading = false;
	bool verified = false;
	bool automatic_check = false;
	bool helper_preparing = false;
	bool helper_started = false;
	bool cache_cleaned = false;

	void _startup_check();
	void _request_completed(int p_result, int p_code, const PackedStringArray &p_headers, const PackedByteArray &p_body, bool p_godot);
	void _apply_response(const String &p_body, bool p_godot);
	void _download_completed(int p_result, int p_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _download_update();
	void _install_update();
	bool _launch_helper(bool p_restart);
	void _install_on_exit();
	void _poll_helper();
	void _poll_download(uint64_t p_now);
	void _discard_ticket();
	void _report_previous_installs();
	void _cleanup_cache();
	void _open_release_page();
	void _finish_check();
	void _refresh_dialog();
	void _set_preference(bool p_enabled, const String &p_key);
	void _set_action_error(const String &p_message);
	void _watch_quit_cancellation();
	bool _preference(const String &p_key, bool p_default) const;
	bool _include_prereleases() const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	static ROMEUpdateManager *install(Control *p_ui_base);
	static ROMEUpdateManager *get_singleton() { return singleton; }
	static bool is_version_valid(const String &p_version);
	static int compare_versions(const String &p_a, const String &p_b);
	static Dictionary select_rome_release(const Array &p_releases, const String &p_platform, bool p_include_prereleases = true);
	static Dictionary select_godot_release(const Array &p_releases);
	static String get_platform();
	void set_quit_callback(const Callable &p_callback) { quit_callback = p_callback; }
	void check_updates(bool p_force = true);
	void show_update_dialog();
	void cancel_download();
	void cancel_installation();
	Dictionary get_status() const;
	ROMEUpdateManager();
	~ROMEUpdateManager();
};
