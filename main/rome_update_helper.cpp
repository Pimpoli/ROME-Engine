#include "rome_update_helper.h"

#ifdef TOOLS_ENABLED

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/string/print_string.h"
#include "core/templates/hash_map.h"

#include <cstdio>
#include <cstring>

#ifdef WINDOWS_ENABLED
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#endif

namespace {

enum UpdateExitCode {
	UPDATE_OK = 0,
	UPDATE_ARGUMENTS = 2,
	UPDATE_VALIDATION = 3,
	UPDATE_WAIT_TIMEOUT = 4,
	UPDATE_STAGING = 5,
	UPDATE_TARGET_CHANGED = 6,
	UPDATE_REPLACEMENT = 7,
	UPDATE_RESTART = 8,
	UPDATE_CANCELLED = 9,
};

// Antivirus scanners can keep the executable open for a while after ROME closes.
constexpr int REPLACE_RETRY_SECONDS = 30;

// Progress for the editor that launched the helper, and for the next start of
// ROME when the editor is already closed. Small JSON, replaced as a whole.
struct UpdateStatus {
	String path;
	Dictionary data;

	void write(const String &p_stage, int p_code, const String &p_message) {
		if (path.is_empty()) {
			return;
		}
		data["stage"] = p_stage;
		data["code"] = p_code;
		data["message"] = p_message;
		data["time"] = OS::get_singleton()->get_unix_time();
		const String temporary = path + ".tmp";
		Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE);
		if (file.is_null()) {
			return;
		}
		file->store_string(JSON::stringify(data));
		file.unref();
		DirAccess::rename_absolute(temporary, path);
	}
};

bool update_failed(int &r_code, UpdateStatus &r_status, int p_code, const String &p_message) {
	r_code = p_code;
	print_error("ROME updater: " + p_message);
	r_status.write("failed", p_code, p_message);
	return true;
}

struct UpdateCancelTicket {
	String path;
	String original_sha;

	bool cancelled() const {
		return !path.is_empty() && (!FileAccess::exists(path) || FileAccess::get_sha256(path) != original_sha);
	}

	~UpdateCancelTicket() {
		// Never remove a different ticket that replaced this helper's own token.
		if (!path.is_empty() && FileAccess::exists(path) && FileAccess::get_sha256(path) == original_sha) {
			DirAccess::remove_absolute(path);
		}
	}
};

bool update_cancelled(int &r_code, UpdateStatus &r_status) {
	r_code = UPDATE_CANCELLED;
	const String message = String::utf8("Actualización cancelada. ROME sigue en la versión que tenías.");
	print_line("ROME updater: " + message);
	r_status.write("cancelled", UPDATE_CANCELLED, message);
	return true;
}

bool valid_hash(const String &p_hash) {
	if (p_hash.length() != 64) {
		return false;
	}
	for (int i = 0; i < p_hash.length(); i++) {
		const char32_t c = p_hash[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return false;
		}
	}
	return true;
}

bool unsigned_integer(const String &p_value) {
	if (p_value.is_empty() || p_value.length() > 10) {
		return false;
	}
	for (int i = 0; i < p_value.length(); i++) {
		if (p_value[i] < '0' || p_value[i] > '9') {
			return false;
		}
	}
	return true;
}

#ifdef WINDOWS_ENABLED
// OneDrive and other cloud folders are reparse points too; only real links count.
bool is_real_link(const String &p_path) {
	WIN32_FIND_DATAW data;
	const HANDLE search = FindFirstFileW((LPCWSTR)(p_path.replace("/", "\\").utf16().get_data()), &data);
	if (search == INVALID_HANDLE_VALUE) {
		return false;
	}
	FindClose(search);
	return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && (data.dwReserved0 == IO_REPARSE_TAG_SYMLINK || data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
}
#else
bool is_real_link(const String &p_path) {
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	return filesystem->is_link(p_path);
}
#endif

// Folders reached through junctions or cloud placeholders are accepted; the file
// itself must not be a link, so the replacement lands where the editor runs from.
bool safe_absolute_path(const String &p_input, String &r_path) {
	const String path = p_input.replace("\\", "/");
	if (!path.is_absolute_path() || path.contains("://") || path.ends_with("/")) {
		return false;
	}
	for (int i = 0; i < path.length(); i++) {
		if (path[i] < 32 || path[i] == 127) {
			return false;
		}
	}
	for (const String &component : path.split("/", false)) {
		if (component == "." || component == "..") {
			return false;
		}
	}
	r_path = path.simplify_path();
	return !is_real_link(r_path);
}

String program_format(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null() || file->get_length() < 1024 * 1024) {
		return String();
	}
	const Vector<uint8_t> header = file->get_buffer(64);
	if (header.size() != 64) {
		return String();
	}
#ifdef WINDOWS_ENABLED
	if (header[0] != 'M' || header[1] != 'Z') {
		return String();
	}
	file->seek(0x3c);
	const uint32_t pe_offset = file->get_32();
	if (pe_offset < 64 || pe_offset > file->get_length() - 24) {
		return String();
	}
	file->seek(pe_offset);
	if (file->get_32() != 0x00004550) {
		return String();
	}
	return "pe:" + itos(file->get_16());
#elif defined(MACOS_ENABLED)
	file->seek(0);
	const uint32_t magic = file->get_32();
	if (magic == 0xfeedface || magic == 0xfeedfacf || magic == 0xcefaedfe || magic == 0xcffaedfe || magic == 0xbebafeca || magic == 0xcafebabe || magic == 0xbfbafeca || magic == 0xcafebabf) {
		return "macho";
	}
	return String();
#elif defined(UNIX_ENABLED)
	if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' || header[3] != 'F' || (header[4] != 1 && header[4] != 2) || (header[5] != 1 && header[5] != 2)) {
		return String();
	}
	const int machine = header[5] == 1 ? header[18] | (header[19] << 8) : (header[18] << 8) | header[19];
	return "elf:" + itos(header[4]) + ":" + itos(machine);
#else
	return String();
#endif
}

bool has_rome_brand(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		return false;
	}
	static const char brand[] = "ROME Engine";
	constexpr int BRAND_LENGTH = sizeof(brand) - 1;
	uint8_t buffer[65536 + BRAND_LENGTH];
	int retained = 0;
	while (true) {
		const int read = file->get_buffer(buffer + retained, 65536);
		const int length = retained + read;
		for (int i = 0; i <= length - BRAND_LENGTH; i++) {
			if (buffer[i] == 'R' && std::memcmp(buffer + i, brand, BRAND_LENGTH) == 0) {
				return true;
			}
		}
		if (read == 0) {
			return false;
		}
		retained = MIN(length, BRAND_LENGTH - 1);
		std::memmove(buffer, buffer + length - retained, retained);
	}
}

// OS::is_process_running() only knows processes started by this one. The editor
// is the helper's parent, so it is watched through the platform instead.
class ParentProcess {
	ProcessID pid = 0;
#ifdef WINDOWS_ENABLED
	HANDLE handle = nullptr;
#endif

public:
	explicit ParentProcess(ProcessID p_pid) :
			pid(p_pid) {
#ifdef WINDOWS_ENABLED
		// Opened at once: the handle keeps meaning this editor even if its ID is reused.
		if (pid != 0) {
			handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
		}
#endif
	}

	ParentProcess(const ParentProcess &) = delete;
	ParentProcess &operator=(const ParentProcess &) = delete;

	bool running() const {
		if (pid == 0) {
			return false;
		}
#ifdef WINDOWS_ENABLED
		if (handle != nullptr) {
			return WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
		}
		const HANDLE probe = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
		if (probe == nullptr) {
			return GetLastError() == ERROR_ACCESS_DENIED;
		}
		DWORD exit_code = 0;
		const bool alive = GetExitCodeProcess(probe, &exit_code) && exit_code == STILL_ACTIVE;
		CloseHandle(probe);
		return alive;
#else
		return kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
	}

	~ParentProcess() {
#ifdef WINDOWS_ENABLED
		if (handle != nullptr) {
			CloseHandle(handle);
		}
#endif
	}
};

// ReplaceFileW keeps the old executable until the new one is in place (it works
// even while an old process still runs from it). Both paths share a directory.
Error atomic_replace(const String &p_staged, const String &p_target) {
#ifdef WINDOWS_ENABLED
	const Char16String staged = p_staged.replace("/", "\\").utf16();
	const Char16String target = p_target.replace("/", "\\").utf16();
	if (FileAccess::exists(p_target)) {
		if (ReplaceFileW((LPCWSTR)target.get_data(), (LPCWSTR)staged.get_data(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
			return OK;
		}
		// Only when the old file is already gone is the plain move below safe.
		if (GetLastError() != ERROR_UNABLE_TO_MOVE_REPLACEMENT || FileAccess::exists(p_target)) {
			return FAILED;
		}
	}
	return MoveFileExW((LPCWSTR)staged.get_data(), (LPCWSTR)target.get_data(), MOVEFILE_WRITE_THROUGH) ? OK : FAILED;
#else
	return std::rename(p_staged.utf8().get_data(), p_target.utf8().get_data()) == 0 ? OK : FAILED;
#endif
}

Error replace_when_free(const String &p_staged, const String &p_target) {
	OS *os = OS::get_singleton();
	const uint64_t deadline = os->get_ticks_msec() + uint64_t(REPLACE_RETRY_SECONDS) * 1000;
	uint32_t pause = 250000;
	while (atomic_replace(p_staged, p_target) != OK) {
		if (!FileAccess::exists(p_target) || os->get_ticks_msec() >= deadline) {
			return FAILED;
		}
		os->delay_usec(pause);
		pause = MIN(pause * 2, uint32_t(2000000));
	}
	return OK;
}

Error execution_permissions(const String &p_file, BitField<FileAccess::UnixPermissionFlags> p_permissions) {
#ifdef UNIX_ENABLED
	p_permissions.set_flag(FileAccess::UNIX_EXECUTE_OWNER);
	p_permissions.clear_flag(FileAccess::UNIX_SET_USER_ID);
	p_permissions.clear_flag(FileAccess::UNIX_SET_GROUP_ID);
	return FileAccess::set_unix_permissions(p_file, p_permissions);
#else
	return OK;
#endif
}

void remove_staged_file(const String &p_path) {
	// Only paths generated by this helper are eligible for cleanup.
	if (FileAccess::exists(p_path)) {
		DirAccess::remove_absolute(p_path);
	}
}

// The verified copy of the previous executable lives in the user cache, not in
// the user's folder. The next start of ROME keeps only the newest copy.
String backup_path(const String &p_target, const String &p_original_sha) {
	const String folder = OS::get_singleton()->get_cache_path().path_join("ROMEEngine/backups");
	if (DirAccess::make_dir_recursive_absolute(folder) == OK) {
		return folder.path_join(p_target.get_file() + "." + p_original_sha.left(12) + ".bak");
	}
	return p_target + ".previous-" + p_original_sha.left(12) + ".bak";
}

bool restore_original(const String &p_backup, const String &p_target, const String &p_original_sha, BitField<FileAccess::UnixPermissionFlags> p_permissions) {
	if (FileAccess::exists(p_target) && FileAccess::get_sha256(p_target) == p_original_sha) {
		return true;
	}
	const String rollback = p_target + ".rome-rollback-" + itos(OS::get_singleton()->get_process_id()) + ".tmp";
	if (FileAccess::exists(rollback) || DirAccess::dir_exists_absolute(rollback)) {
		return false;
	}
	if (DirAccess::copy_absolute(p_backup, rollback) != OK || FileAccess::get_sha256(rollback) != p_original_sha || execution_permissions(rollback, p_permissions) != OK) {
		remove_staged_file(rollback);
		return false;
	}
	const bool restored = replace_when_free(rollback, p_target) == OK && FileAccess::get_sha256(p_target) == p_original_sha;
	remove_staged_file(rollback);
	return restored;
}

// Fails before the editor closes when ROME lives in a folder the user cannot change.
bool can_write_beside(const String &p_target) {
	const String probe = p_target.get_base_dir().path_join("." + p_target.get_file() + ".rome-write-test-" + itos(OS::get_singleton()->get_process_id()));
	Ref<FileAccess> file = FileAccess::open(probe, FileAccess::WRITE);
	if (file.is_null()) {
		return false;
	}
	file.unref();
	return DirAccess::remove_absolute(probe) == OK;
}

Error restart_editor(const String &p_target, const String &p_project) {
	List<String> arguments;
	if (p_project.is_empty()) {
		arguments.push_back("--project-manager");
	} else {
		arguments.push_back("--editor");
		arguments.push_back("--path");
		arguments.push_back(p_project);
	}
	// The child inherits the working directory; leave the staging folder so the
	// next start of ROME can remove it.
	OS::get_singleton()->set_cwd(p_target.get_base_dir());
	return OS::get_singleton()->create_process(p_target, arguments, nullptr, false);
}

} // namespace

bool rome_run_update_helper_if_requested(int &r_exit_code) {
	OS *os = OS::get_singleton();
	const List<String> args = os->get_cmdline_user_args();
	if (args.is_empty() || args.front()->get() != "--rome-update-helper") {
		return false;
	}
	r_exit_code = UPDATE_ARGUMENTS;
	HashMap<String, String> options;
	UpdateCancelTicket cancel_ticket;
	UpdateStatus status;
	bool restart = false;
	for (const List<String>::Element *entry = args.front()->next(); entry; entry = entry->next()) {
		const String flag = entry->get();
		if (flag == "--restart") {
			if (restart) {
				return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Duplicate restart argument.");
			}
			restart = true;
			continue;
		}
		if (flag != "--target" && flag != "--wait-pid" && flag != "--sha256" && flag != "--original-sha256" && flag != "--project" && flag != "--timeout-seconds" && flag != "--cancel-file" && flag != "--status-file") {
			return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Unknown helper argument: " + flag);
		}
		entry = entry->next();
		if (!entry || options.has(flag)) {
			return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Missing value or duplicate helper argument: " + flag);
		}
		options.insert(flag, entry->get());
	}
	if (options.has("--status-file")) {
		String status_path;
		if (!safe_absolute_path(options["--status-file"], status_path) || status_path.get_extension().to_lower() == "exe" || DirAccess::dir_exists_absolute(status_path)) {
			return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Status file must be a regular file at an absolute path.");
		}
		status.path = status_path;
		status.write("validating", -1, String::utf8("Comprobando la actualización..."));
	}
	if (options.has("--cancel-file")) {
		String ticket_path;
		if (!safe_absolute_path(options["--cancel-file"], ticket_path) || ticket_path.get_extension().to_lower() == "exe" || DirAccess::dir_exists_absolute(ticket_path) || (FileAccess::exists(ticket_path) && FileAccess::get_size(ticket_path) > 4096)) {
			return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Cancel ticket must be a small regular file at an absolute path.");
		}
		cancel_ticket.path = ticket_path;
		cancel_ticket.original_sha = FileAccess::exists(ticket_path) ? FileAccess::get_sha256(ticket_path) : String();
		if (cancel_ticket.cancelled()) {
			return update_cancelled(r_exit_code, status);
		}
	}
	for (const String &required : { String("--target"), String("--wait-pid"), String("--sha256"), String("--original-sha256") }) {
		if (!options.has(required)) {
			return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Missing required helper argument: " + required);
		}
	}
	const String downloaded_sha = options["--sha256"].to_lower();
	const String original_sha = options["--original-sha256"].to_lower();
	if (!valid_hash(downloaded_sha) || !valid_hash(original_sha) || !unsigned_integer(options["--wait-pid"])) {
		return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Expected two SHA-256 hashes and a nonnegative process ID.");
	}
	const ProcessID parent_pid = options["--wait-pid"].to_int();
	if (parent_pid > UINT32_MAX || parent_pid == os->get_process_id()) {
		return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Invalid parent process ID.");
	}
	ParentProcess parent(parent_pid);
	int timeout_seconds = 1800;
	if (options.has("--timeout-seconds")) {
		if (!unsigned_integer(options["--timeout-seconds"])) {
			return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Invalid wait timeout.");
		}
		const int64_t parsed_timeout = options["--timeout-seconds"].to_int();
		if (parsed_timeout < 1 || parsed_timeout > 1800) {
			return update_failed(r_exit_code, status, UPDATE_ARGUMENTS, "Wait timeout must be between 1 and 1800 seconds.");
		}
		timeout_seconds = parsed_timeout;
	}
	String target;
	String source;
	if (!safe_absolute_path(options["--target"], target) || !safe_absolute_path(os->get_executable_path(), source)) {
		return update_failed(r_exit_code, status, UPDATE_VALIDATION, String::utf8("Las rutas de la actualización no son válidas (deben ser absolutas y sin enlaces simbólicos)."));
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem->is_equivalent(source, target) || source.to_lower() == target.to_lower() || !target.get_file().to_lower().begins_with("rome") || !FileAccess::exists(target)) {
		return update_failed(r_exit_code, status, UPDATE_VALIDATION, String::utf8("El destino debe ser el ejecutable de ROME instalado y existir."));
	}
#ifdef WINDOWS_ENABLED
	if (target.get_extension().to_lower() != "exe") {
		return update_failed(r_exit_code, status, UPDATE_VALIDATION, String::utf8("En Windows el destino debe ser un archivo .exe."));
	}
#endif
	const String source_format = program_format(source);
	if (source_format.is_empty() || program_format(target) != source_format || !has_rome_brand(source) || !has_rome_brand(target)) {
		return update_failed(r_exit_code, status, UPDATE_VALIDATION, String::utf8("El paquete descargado y el instalado deben ser ejecutables de ROME para el mismo sistema."));
	}
	if (FileAccess::get_sha256(source) != downloaded_sha) {
		return update_failed(r_exit_code, status, UPDATE_VALIDATION, String::utf8("La descarga no coincide con el SHA-256 publicado. Descárgala de nuevo."));
	}
	if (FileAccess::get_sha256(target) != original_sha) {
		return update_failed(r_exit_code, status, UPDATE_VALIDATION, String::utf8("El ejecutable instalado cambió desde que se abrió ROME. Reinicia ROME y vuelve a intentarlo."));
	}
	String project;
	if (options.has("--project") && (!safe_absolute_path(options["--project"], project) || !FileAccess::exists(project.path_join("project.godot")))) {
		return update_failed(r_exit_code, status, UPDATE_VALIDATION, String::utf8("La experiencia que había que reabrir ya no existe."));
	}
	if (!can_write_beside(target)) {
		return update_failed(r_exit_code, status, UPDATE_STAGING, String::utf8("No hay permiso para escribir en la carpeta de ROME (") + target.get_base_dir() + String::utf8("). Mueve ROME Studio a una carpeta tuya, como Documentos o el Escritorio, y vuelve a intentarlo."));
	}
	status.data["target"] = target;
	// The copy and the backup are prepared while the editor is still open, so a
	// full disk is reported there and the closed editor is replaced in a moment.
	const String unique = itos(os->get_process_id()) + "-" + itos(os->get_ticks_usec());
	const String staged = target + ".rome-update-" + unique + ".tmp";
	if (FileAccess::exists(staged) || DirAccess::dir_exists_absolute(staged)) {
		return update_failed(r_exit_code, status, UPDATE_STAGING, String::utf8("No se pudo preparar el archivo temporal de la actualización."));
	}
	const BitField<FileAccess::UnixPermissionFlags> permissions = FileAccess::get_unix_permissions(target);
	if (DirAccess::copy_absolute(source, staged) != OK || FileAccess::get_sha256(staged) != downloaded_sha || execution_permissions(staged, permissions) != OK) {
		remove_staged_file(staged);
		return update_failed(r_exit_code, status, UPDATE_STAGING, String::utf8("No se pudo copiar la versión nueva junto a ROME Studio (¿falta espacio en el disco?). No se cambió nada."));
	}
	const String backup = backup_path(target, original_sha);
	if (!FileAccess::exists(backup) || FileAccess::get_sha256(backup) != original_sha) {
		if (DirAccess::copy_absolute(target, backup) != OK || FileAccess::get_sha256(backup) != original_sha) {
			remove_staged_file(staged);
			return update_failed(r_exit_code, status, UPDATE_STAGING, String::utf8("No se pudo guardar una copia de la versión actual, así que no se cambió nada."));
		}
	}
	status.data["backup"] = backup;
	if (parent_pid != 0) {
		status.write("waiting", -1, String::utf8("Esperando a que se cierre ROME para instalar la actualización."));
		print_line("ROME updater: waiting for the editor to close normally.");
		const uint64_t deadline = os->get_ticks_msec() + uint64_t(timeout_seconds) * 1000;
		while (parent.running()) {
			if (cancel_ticket.cancelled()) {
				remove_staged_file(staged);
				return update_cancelled(r_exit_code, status);
			}
			if (os->get_ticks_msec() >= deadline) {
				remove_staged_file(staged);
				return update_failed(r_exit_code, status, UPDATE_WAIT_TIMEOUT, String::utf8("ROME no se cerró a tiempo. No se cambió nada; puedes volver a intentarlo."));
			}
			os->delay_usec(250000);
		}
	}
	if (cancel_ticket.cancelled()) {
		remove_staged_file(staged);
		return update_cancelled(r_exit_code, status);
	}
	// From here the editor is closed: a failure reopens the installed version so
	// the user is never left without ROME, and the message waits for that start.
	status.write("installing", -1, String::utf8("Instalando la actualización..."));
	const auto fail_installed = [&](int p_code, const String &p_message) {
		update_failed(r_exit_code, status, p_code, p_message);
		if (restart && FileAccess::exists(target)) {
			restart_editor(target, project);
		}
		return true;
	};
	String validated_target;
	if (!safe_absolute_path(target, validated_target) || FileAccess::get_sha256(target) != original_sha) {
		remove_staged_file(staged);
		return fail_installed(UPDATE_TARGET_CHANGED, String::utf8("El ejecutable instalado cambió mientras se esperaba. No se instaló nada."));
	}
	if (replace_when_free(staged, target) != OK && FileAccess::get_sha256(target) != downloaded_sha) {
		const bool restored = restore_original(backup, target, original_sha, permissions);
		remove_staged_file(staged);
		return fail_installed(UPDATE_REPLACEMENT, restored ? String::utf8("Otro programa (quizá el antivirus) tenía ROME Studio abierto y no se pudo reemplazar. Tu versión sigue intacta; vuelve a intentarlo.") : String::utf8("No se pudo reemplazar ROME Studio. Recupera la versión anterior desde esta copia: ") + backup);
	}
	remove_staged_file(staged);
	if (FileAccess::get_sha256(target) != downloaded_sha) {
		const bool restored = restore_original(backup, target, original_sha, permissions);
		return fail_installed(UPDATE_REPLACEMENT, restored ? String::utf8("La versión instalada no pasó la comprobación final; se recuperó la anterior.") : String::utf8("La versión instalada no pasó la comprobación final. Recupera la anterior desde esta copia: ") + backup);
	}
	print_line("ROME updater: update installed. Previous executable kept at " + backup);
	status.write("done", UPDATE_OK, String::utf8("ROME se actualizó correctamente."));
	if (restart && restart_editor(target, project) != OK) {
		return update_failed(r_exit_code, status, UPDATE_RESTART, String::utf8("La actualización se instaló, pero ROME no se pudo abrir solo. Ábrelo de nuevo."));
	}
	r_exit_code = UPDATE_OK;
	return true;
}

#endif // TOOLS_ENABLED
