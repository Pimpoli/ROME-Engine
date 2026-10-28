#include "rome_project_template.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/version.h"
#include "modules/modules_enabled.gen.h"
#ifdef MODULE_ZIP_ENABLED
#include "modules/zip/zip_reader.h"
#endif

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
#ifdef MODULE_ZIP_ENABLED
	// Desktop releases carry their own runtime as a ZIP appended to the binary.
	// It is installed into a per-version cache on first launch, without a wizard.
	Ref<ZIPReader> archive;
	archive.instantiate();
	if (archive->open(executable) == OK && archive->file_exists("rome_runtime/manifest.json", true)) {
		const PackedByteArray manifest = archive->read_file("rome_runtime/manifest.json", true);
		const String identity = String::utf8(reinterpret_cast<const char *>(manifest.ptr()), manifest.size()).sha256_text().left(12);
		const String cache = OS::get_singleton()->get_cache_path().path_join("ROMEEngine/runtime").path_join(String(ROME_VERSION) + "-" + identity);
		const String complete = cache.path_join("runtime.complete");
		if (FileAccess::exists(complete)) {
			runtime_root = cache;
			return runtime_root;
		}
		bool valid = DirAccess::make_dir_recursive_absolute(cache) == OK;
		for (const String &entry : archive->get_files()) {
			if (!valid || !entry.begins_with("rome_runtime/")) {
				continue;
			}
			const String relative = entry.trim_prefix("rome_runtime/");
			if (relative.is_absolute_path() || relative.contains(":") || relative.split("/").has("..")) {
				valid = false;
				break;
			}
			const String destination = cache.path_join(relative);
			if (entry.ends_with("/")) {
				valid = DirAccess::make_dir_recursive_absolute(destination) == OK;
				continue;
			}
			if (DirAccess::make_dir_recursive_absolute(destination.get_base_dir()) != OK) {
				valid = false;
				break;
			}
			const PackedByteArray data = archive->read_file(entry, true);
			const String temporary = destination + "." + itos(OS::get_singleton()->get_process_id()) + ".tmp";
			Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE);
			if (file.is_null()) {
				valid = false;
				break;
			}
			file->store_buffer(data);
			file.unref();
			if (DirAccess::rename_absolute(temporary, destination) != OK) {
				valid = false;
				break;
			}
		}
		if (valid && FileAccess::exists(cache.path_join("project/GodotLuau/godot_luau.gdextension"))) {
			Ref<FileAccess> marker = FileAccess::open(complete, FileAccess::WRITE);
			if (marker.is_valid()) {
				marker->store_string(identity);
				runtime_root = cache;
				return runtime_root;
			}
		}
	}
#endif
	return sidecar;
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
	return OK;
}

void RomeProjectTemplate::configure(ProjectSettings::CustomMap &r_settings) {
	r_settings["application/run/main_scene"] = "res://Scenes/Place.tscn";
	r_settings["application/config/description"] = "Creado con ROME Studio y Luau";
	r_settings["editor_plugins/enabled"] = PackedStringArray({ "res://addons/GodotLuauUpdater/plugin.cfg", "res://addons/ROMEStudio/plugin.cfg" });
	r_settings["physics/3d/physics_engine"] = "Jolt Physics";
	r_settings["rendering/rendering_device/driver.windows"] = "vulkan";
	r_settings["rome/studio/enabled"] = true;
}
