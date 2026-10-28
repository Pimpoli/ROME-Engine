#include "rome_project_template.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/zip_io.h"
#include "core/os/os.h"
#include "core/version.h"

String RomeProjectTemplate::update_message;
bool RomeProjectTemplate::update_failed = false;

namespace {

bool is_safe_relative(const String &p_relative) {
	return !p_relative.is_empty() && !p_relative.is_absolute_path() && !p_relative.contains(":") && !p_relative.contains("\\") && !p_relative.split("/").has("..");
}

// Updates only touch the runtime's own folders; scenes, assets and the
// creator's Luau folders belong to the experience. So do the game's logo and
// the window icon GodotLuau draws from it, which only start as ROME's.
bool is_managed(const String &p_relative) {
	if (!is_safe_relative(p_relative) || p_relative.begins_with("GodotLuau/LocalScripts/") || p_relative.begins_with("GodotLuau/ModuleScripts/") || p_relative.begins_with("GodotLuau/ServerScripts/")) {
		return false;
	}
	if (p_relative == "GodotLuau/icons/MenuLogo.svg" || p_relative == "GodotLuau/icons/GodotLuau.ico") {
		return false;
	}
	return p_relative.begins_with("GodotLuau/") || p_relative.begins_with("addons/ROMEStudio/") || p_relative.begins_with("addons/GodotLuauUpdater/");
}

// The creator is meant to edit these; an update keeps their version.
bool is_user_editable(const String &p_relative) {
	return p_relative.begins_with("GodotLuau/DefaultScripts/") || p_relative.begins_with("GodotLuau/DedicatedServer/");
}

Dictionary managed_files(const Dictionary &p_manifest) {
	Dictionary files;
	const Dictionary listed = p_manifest.get("files", Dictionary());
	for (const KeyValue<Variant, Variant> &E : listed) {
		if (is_managed(E.key)) {
			files[E.key] = E.value;
		}
	}
	return files;
}

bool read_current(unzFile p_archive, PackedByteArray &r_data) {
	unz_file_info64 info;
	String path;
	if (godot_unzip_get_current_file_info(p_archive, info, path) != UNZ_OK || info.uncompressed_size > INT_MAX || unzOpenCurrentFile(p_archive) != UNZ_OK) {
		return false;
	}
	r_data.resize(info.uncompressed_size);
	int64_t done = 0;
	while (done < r_data.size()) {
		const int bytes = unzReadCurrentFile(p_archive, r_data.ptrw() + done, r_data.size() - done);
		if (bytes <= 0) {
			break;
		}
		done += bytes;
	}
	// Closing also checks the CRC of what was read.
	return unzCloseCurrentFile(p_archive) == UNZ_OK && done == r_data.size();
}

String extract_runtime(unzFile p_archive, const String &p_cache) {
	if (DirAccess::make_dir_recursive_absolute(p_cache) != OK) {
		return String::utf8("no se pudo crear la carpeta");
	}
	if (unzGoToFirstFile(p_archive) != UNZ_OK) {
		return String::utf8("el paquete está dañado");
	}
	do {
		unz_file_info64 info;
		String entry;
		if (godot_unzip_get_current_file_info(p_archive, info, entry) != UNZ_OK) {
			return String::utf8("el paquete está dañado");
		}
		const String relative = entry.trim_prefix("rome_runtime/");
		if (relative == entry || relative.is_empty()) {
			continue;
		}
		if (!is_safe_relative(relative.trim_suffix("/"))) {
			return String::utf8("el paquete trae una ruta no válida (") + entry + ")";
		}
		const String destination = p_cache.path_join(relative);
		if (entry.ends_with("/")) {
			if (DirAccess::make_dir_recursive_absolute(destination) != OK) {
				return String::utf8("no se pudo crear ") + relative;
			}
			continue;
		}
		PackedByteArray data;
		if (!read_current(p_archive, data)) {
			return String::utf8("no se pudo leer ") + relative + String::utf8(" del ejecutable");
		}
		if (DirAccess::make_dir_recursive_absolute(destination.get_base_dir()) != OK) {
			return String::utf8("no se pudo crear ") + relative.get_base_dir();
		}
		const String temporary = destination + "." + itos(OS::get_singleton()->get_process_id()) + ".tmp";
		Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE);
		const bool stored = file.is_valid() && file->store_buffer(data);
		file.unref();
		if (!stored || DirAccess::rename_absolute(temporary, destination) != OK) {
			DirAccess::remove_absolute(temporary);
			return String::utf8("no se pudo escribir ") + relative;
		}
	} while (unzGoToNextFile(p_archive) == UNZ_OK);
	return String();
}

Dictionary read_json(const String &p_path) {
	if (!FileAccess::exists(p_path)) {
		return Dictionary();
	}
	Error err = OK;
	const String text = FileAccess::get_file_as_string(p_path, &err);
	Ref<JSON> json;
	json.instantiate();
	if (err != OK || json->parse(text) != OK || json->get_data().get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return json->get_data();
}

Error write_text(const String &p_path, const String &p_text) {
	Error err = DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
	if (err != OK) {
		return err;
	}
	const String temporary = p_path + ".tmp";
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE, &err);
	if (file.is_null()) {
		return err;
	}
	const bool stored = file->store_string(p_text);
	file.unref();
	return stored ? DirAccess::rename_absolute(temporary, p_path) : ERR_FILE_CANT_WRITE;
}

Error copy_file(const String &p_from, const String &p_to) {
	Error err = DirAccess::make_dir_recursive_absolute(p_to.get_base_dir());
	if (err != OK) {
		return err;
	}
	const String temporary = p_to + ".rome-tmp";
	err = DirAccess::copy_absolute(p_from, temporary);
	if (err == OK) {
		err = DirAccess::rename_absolute(temporary, p_to);
	}
	if (err != OK) {
		DirAccess::remove_absolute(temporary);
	}
	return err;
}

void remove_tree(const String &p_path) {
	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_valid() && dir->erase_contents_recursive() == OK) {
		DirAccess::remove_absolute(p_path);
	}
}

// `.rome` records which runtime files the experience has, so the next ROME can
// tell its own files apart from the creator's changes.
Error write_stamp(const String &p_path, const String &p_identity, const Dictionary &p_files) {
	const String folder = p_path.path_join(".rome");
	Dictionary stamp;
	stamp["identity"] = p_identity;
	stamp["rome_version"] = ROME_VERSION;
	stamp["files"] = p_files;
	Error err = write_text(folder.path_join("runtime.json"), JSON::stringify(stamp, "\t"));
	if (err == OK && !FileAccess::exists(folder.path_join(".gdignore"))) {
		err = write_text(folder.path_join(".gdignore"), "");
	}
	if (err == OK && !FileAccess::exists(folder.path_join(".gitignore"))) {
		err = write_text(folder.path_join(".gitignore"), "*\n!.gitignore\n!runtime.json\n");
	}
	return err;
}

String list_files(const Vector<String> &p_files) {
	String text;
	for (int i = 0; i < MIN(p_files.size(), 3); i++) {
		text += (i == 0 ? "" : ", ") + p_files[i].get_file();
	}
	if (p_files.size() > 3) {
		text += vformat(String::utf8(" y %d más"), p_files.size() - 3);
	}
	return text;
}

} // namespace

String RomeProjectTemplate::get_root() {
	static String runtime_root;
	if (!runtime_root.is_empty()) {
		return runtime_root;
	}
	const String executable = OS::get_singleton()->get_executable_path();
	const String sidecar = executable.get_base_dir().path_join("rome_runtime");
	if (FileAccess::exists(sidecar.path_join("manifest.json"))) {
		runtime_root = sidecar;
		return runtime_root;
	}
	// Desktop releases carry their own runtime as a ZIP appended to the binary.
	// It is installed into a per-version cache on first launch, without a wizard.
	// minizip is used directly: experiences are updated before modules load.
	Ref<FileAccess> io_file;
	zlib_filefunc_def io = zipio_create_io(&io_file);
	unzFile archive = unzOpen2(executable.utf8().get_data(), &io);
	PackedByteArray manifest;
	if (archive != nullptr && godot_unzip_locate_file(archive, "rome_runtime/manifest.json") == UNZ_OK && read_current(archive, manifest)) {
		const String identity = String::utf8(reinterpret_cast<const char *>(manifest.ptr()), manifest.size()).sha256_text().left(12);
		const String cache = OS::get_singleton()->get_cache_path().path_join("ROMEEngine/runtime").path_join(String(ROME_VERSION) + "-" + identity);
		const String complete = cache.path_join("runtime.complete");
		String failure;
		if (!FileAccess::exists(complete)) {
			failure = extract_runtime(archive, cache);
			if (failure.is_empty() && !FileAccess::exists(cache.path_join("project/GodotLuau/godot_luau.gdextension"))) {
				failure = String::utf8("al paquete le falta GodotLuau");
			}
			if (failure.is_empty() && write_text(complete, identity) != OK) {
				failure = String::utf8("no se pudo terminar la instalación");
			}
		}
		if (failure.is_empty()) {
			runtime_root = cache;
		} else {
			ERR_PRINT(vformat(String::utf8("ROME no pudo preparar sus componentes en \"%s\": %s. Comprueba que hay espacio libre y permiso de escritura en esa carpeta y vuelve a abrir ROME."), cache, failure));
		}
	}
	if (archive != nullptr) {
		unzClose(archive);
	}
	if (runtime_root.is_empty()) {
		runtime_root = sidecar;
	}
	return runtime_root;
}

String RomeProjectTemplate::get_identity() {
	const String manifest = get_root().path_join("manifest.json");
	return FileAccess::exists(manifest) ? FileAccess::get_file_as_string(manifest).sha256_text().left(12) : String();
}

bool RomeProjectTemplate::is_available() {
	return FileAccess::exists(get_root().path_join("project/GodotLuau/godot_luau.gdextension"));
}

Error RomeProjectTemplate::install(const String &p_path, const String &p_template) {
	if (!is_available()) {
		return ERR_FILE_NOT_FOUND;
	}
	Ref<DirAccess> source = DirAccess::open(get_root().path_join("project"));
	ERR_FAIL_COND_V(source.is_null(), ERR_CANT_OPEN);
	Error err = source->copy_dir(".", p_path, -1, true);
	ERR_FAIL_COND_V(err != OK, err);
	err = DirAccess::make_dir_recursive_absolute(p_path.path_join("Scenes"));
	ERR_FAIL_COND_V(err != OK, err);
	Ref<FileAccess> scene = FileAccess::open(p_path.path_join("Scenes/Place.tscn"), FileAccess::WRITE, &err);
	ERR_FAIL_COND_V(scene.is_null(), err);
	const Vector<String> allowed = { "baseplate", "empty", "obby" };
	const String template_id = allowed.has(p_template) ? p_template : String("baseplate");
	scene->store_string("[gd_scene format=3]\n\n[node name=\"Game\" type=\"Game3D\"]\nmetadata/rome_template = \"" + template_id + "\"\n");
	return write_stamp(p_path, get_identity(), managed_files(read_json(get_root().path_join("manifest.json"))));
}

void RomeProjectTemplate::configure(ProjectSettings::CustomMap &r_settings) {
	r_settings["application/run/main_scene"] = "res://Scenes/Place.tscn";
	r_settings["application/config/description"] = "Creado con ROME Studio y Luau";
	r_settings["editor_plugins/enabled"] = PackedStringArray({ "res://addons/GodotLuauUpdater/plugin.cfg", "res://addons/ROMEStudio/plugin.cfg" });
	r_settings["physics/3d/physics_engine"] = "Jolt Physics";
	r_settings["rendering/rendering_device/driver.windows"] = "vulkan";
	r_settings["rome/studio/enabled"] = true;
}

// Brings the experience's copy of ROMEStudio and GodotLuau to the runtime of
// this ROME. It runs before the experience's GDExtension is loaded, because a
// loaded library cannot be replaced; scenes, scripts and assets are not touched.
Error RomeProjectTemplate::update_project(const String &p_path) {
	if (!is_available()) {
		return ERR_UNAVAILABLE;
	}
	const String root = get_root();
	const Dictionary shipped = managed_files(read_json(root.path_join("manifest.json")));
	const String identity = get_identity();
	if (shipped.is_empty() || identity.is_empty()) {
		return ERR_UNAVAILABLE;
	}
	const String folder = p_path.path_join(".rome");
	const Dictionary stamp = read_json(folder.path_join("runtime.json"));
	const Dictionary base = stamp.get("files", Dictionary());
	const String previous = stamp.get("identity", "");
	if (previous == identity) {
		bool complete = true;
		for (const KeyValue<Variant, Variant> &E : shipped) {
			if (!FileAccess::exists(p_path.path_join(E.key))) {
				complete = false;
				break;
			}
		}
		if (complete) {
			return OK;
		}
	}

	Vector<String> replace;
	Vector<String> stale;
	Vector<String> kept;
	for (const KeyValue<Variant, Variant> &E : shipped) {
		const String relative = E.key;
		const String target = p_path.path_join(relative);
		if (!FileAccess::exists(target)) {
			replace.push_back(relative);
			continue;
		}
		const String current = FileAccess::get_sha256(target);
		if (current == String(E.value)) {
			continue;
		}
		// Without a record of what ROME installed, a different editable file is
		// assumed to be the creator's.
		const String installed = base.get(relative, "");
		if (is_user_editable(relative) && current != installed) {
			if (installed != String(E.value)) {
				kept.push_back(relative);
			}
			continue;
		}
		replace.push_back(relative);
	}
	for (const KeyValue<Variant, Variant> &E : base) {
		const String relative = E.key;
		const String target = p_path.path_join(relative);
		if (!shipped.has(relative) && is_managed(relative) && FileAccess::exists(target) && FileAccess::get_sha256(target) == String(E.value)) {
			stale.push_back(relative);
		}
	}

	Vector<String> touched = replace;
	touched.append_array(stale);
	for (const String &relative : touched) {
		const String target = p_path.path_join(relative);
		if (FileAccess::exists(target) && FileAccess::open(target, FileAccess::READ_WRITE).is_null()) {
			update_message = vformat(String::utf8("ROME no pudo actualizar los componentes de esta experiencia porque %s está en uso (¿otra ventana de ROME o un juego abierto?). Se volverá a intentar la próxima vez que la abras."), relative);
			update_failed = true;
			WARN_PRINT(update_message);
			return ERR_FILE_CANT_WRITE;
		}
	}

	const String backup_name = "backup-" + (previous.is_empty() ? String("anterior") : previous);
	const String backup = folder.path_join(backup_name);
	if (!touched.is_empty() && DirAccess::dir_exists_absolute(backup)) {
		remove_tree(backup);
	}
	Vector<String> saved;
	Vector<String> created;
	Error err = OK;
	for (const String &relative : replace) {
		const String target = p_path.path_join(relative);
		if (FileAccess::exists(target)) {
			err = copy_file(target, backup.path_join(relative));
			if (err != OK) {
				break;
			}
			saved.push_back(relative);
		} else {
			created.push_back(relative);
		}
		err = copy_file(root.path_join("project").path_join(relative), target);
		if (err == OK && FileAccess::get_sha256(target) != String(shipped[relative])) {
			err = ERR_FILE_CORRUPT;
		}
		if (err != OK) {
			break;
		}
	}
	for (int i = 0; err == OK && i < stale.size(); i++) {
		const String target = p_path.path_join(stale[i]);
		err = copy_file(target, backup.path_join(stale[i]));
		if (err == OK) {
			saved.push_back(stale[i]);
			err = DirAccess::remove_absolute(target);
		}
	}
	if (err == OK) {
		err = write_stamp(p_path, identity, shipped);
	}
	if (err != OK) {
		bool restored = true;
		for (const String &relative : saved) {
			const String target = p_path.path_join(relative);
			const String copy = backup.path_join(relative);
			if (!FileAccess::exists(target) || FileAccess::get_sha256(target) != FileAccess::get_sha256(copy)) {
				restored = copy_file(copy, target) == OK && restored;
			}
		}
		for (const String &relative : created) {
			DirAccess::remove_absolute(p_path.path_join(relative));
		}
		if (restored) {
			remove_tree(backup);
		}
		if (err == ERR_FILE_CORRUPT && FileAccess::exists(root.path_join("runtime.complete"))) {
			// The next start extracts the runtime from the executable again.
			DirAccess::remove_absolute(root.path_join("runtime.complete"));
		}
		update_message = String::utf8("ROME no pudo actualizar los componentes de esta experiencia y la dejó como estaba") + (restored ? String(".") : vformat(String::utf8("; la copia de lo anterior está en .rome/%s."), backup_name)) + String::utf8(" Se volverá a intentar la próxima vez que la abras.");
		update_failed = true;
		WARN_PRINT(update_message + " (" + error_names[err] + ")");
		return err;
	}

	if (!saved.is_empty()) {
		Ref<DirAccess> dir = DirAccess::open(folder);
		for (const String &other : dir.is_valid() ? dir->get_directories() : PackedStringArray()) {
			if (other.begins_with("backup-") && other != backup_name) {
				remove_tree(folder.path_join(other));
			}
		}
	}
	String message;
	if (!touched.is_empty()) {
		const String count = touched.size() == 1 ? String::utf8("1 archivo") : vformat(String::utf8("%d archivos"), touched.size());
		message = vformat(String::utf8("ROME actualizó los componentes de esta experiencia a la versión %s (%s de ROMEStudio y GodotLuau)."), ROME_VERSION, count);
		if (!saved.is_empty()) {
			message += vformat(String::utf8(" Lo anterior quedó en .rome/%s."), backup_name);
		}
	}
	if (!kept.is_empty()) {
		message += (message.is_empty() ? "" : " ") + vformat(String::utf8("Se mantuvo tu versión de %s."), list_files(kept));
	}
	if (!message.is_empty()) {
		update_message = message;
		update_failed = false;
		print_line(message);
	}
	return OK;
}

String RomeProjectTemplate::take_update_message(bool *r_failed) {
	const String message = update_message;
	if (r_failed) {
		*r_failed = update_failed;
	}
	update_message = String();
	update_failed = false;
	return message;
}
