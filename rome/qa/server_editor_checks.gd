@tool
extends EditorPlugin
## Disposable owner-server fixture. Credentials come from an excluded hidden file.
var manager: Node

func _enter_tree() -> void:
	call_deferred("_run")

func _run() -> void:
	await get_tree().create_timer(0.5).timeout
	var bar := HBoxContainer.new()
	add_control_to_container(EditorPlugin.CONTAINER_TOOLBAR, bar)
	manager = preload("res://addons/GodotLuauUpdater/server_manager.gd").new()
	add_child(manager)
	manager.setup(self, bar)
	var configuration: Dictionary = JSON.parse_string(FileAccess.get_file_as_string("res://.server-qa-config.json"))
	manager.set("_profiles", [configuration])
	manager._reload_profiles()
	manager._save_profile()
	var saved_profiles: Array = EditorInterface.get_editor_settings().get_meta("_godot_luau_server_profiles", [])
	var health: Dictionary = await manager._request("/health", HTTPClient.METHOD_GET)
	var result: Dictionary = await manager.snapshot(true)
	result["health"] = health.get("protocol", 0) == 1
	result["profile_saved"] = saved_profiles.size() == 1 and saved_profiles[0].get("url") == configuration.url
	var archive := ZIPReader.new()
	if not result.has("error") and archive.open(result.path) == OK:
		result["private_files_excluded"] = not Array(archive.get_files()).any(func(path: String) -> bool: return path.begins_with(".") or path.contains("owner-token"))
		archive.close()
	if not result.has("error"):
		await manager.publish(false)
		var state: Dictionary = await manager._request("/v1/projects/" + manager._project_id() + "/status", HTTPClient.METHOD_GET)
		result["server_running"] = state.get("running", false)
		result["port"] = state.get("port", 0)
		result["project_id"] = manager._project_id()
		manager.mode.select(1)
		var args: PackedStringArray = manager.run_arguments(PackedStringArray())
		result["remote_args"] = args.has("--glconnect") and args.has("127.0.0.1:" + str(int(result.port)))
		manager.mode.select(0)
		result["local_args"] = manager.run_arguments(PackedStringArray()).is_empty()
		EditorInterface.play_main_scene()
		await get_tree().create_timer(0.8).timeout
		var was_playing := EditorInterface.is_playing_scene()
		await manager.snapshot(false)
		result["backup_keeps_playing"] = was_playing and EditorInterface.is_playing_scene()
		EditorInterface.stop_playing_scene()
	result["passed"] = not result.has("error") and result.get("profile_saved", false) and result.get("health", false) and result.get("server_running", false) and result.get("private_files_excluded", false) and result.get("remote_args", false) and result.get("local_args", false) and result.get("backup_keeps_playing", false)
	var file := FileAccess.open("res://server-qa-result.json", FileAccess.WRITE)
	file.store_string(JSON.stringify(result))
	file.close()
	print("SERVER_EDITOR_QA passed=", result.passed)
	manager.teardown()
	remove_control_from_container(EditorPlugin.CONTAINER_TOOLBAR, bar)
	bar.queue_free()
	get_tree().quit(0 if result.passed else 1)
