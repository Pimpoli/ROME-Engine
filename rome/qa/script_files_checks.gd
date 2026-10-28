@tool
extends RefCounted

static func run(root: Node) -> Array[String]:
	var errors: Array[String] = []
	var helper = preload("res://addons/ROMEStudio/studio_script_files.gd").new()
	helper.ensure_folders()
	var title := "RoutingQA_" + str(Time.get_ticks_usec())
	var parents := {"LocalScript": root.get_node("StarterPlayer/StarterPlayerScripts"), "ServerScript": root.get_node("ServerScriptService"), "ModuleScript": root.get_node("ReplicatedStorage")}
	var created: Array[Node] = []
	for type: String in parents:
		_expect(DirAccess.dir_exists_absolute("res://Scripts/" + type), "Default " + type + " directory exists", errors)
		var node: Node = ClassDB.instantiate(type)
		node.name = title + type
		parents[type].add_child(node)
		node.owner = root
		created.append(node)
		var resource: Script = node.call("get_codigo_luau")
		if type == "ModuleScript":
			resource.source_code = "" # Preserve an intentionally emptied unsaved file.
		var old_path := resource.resource_path
		var original_resource := resource
		var source := resource.source_code
		var identity := str(node.call("get_script_id"))
		_expect(helper.adopt_node(node), "New " + type + " adopts a game-owned path", errors)
		resource = node.call("get_codigo_luau")
		_expect(resource == original_resource, type + " open-document resource identity is preserved", errors)
		_expect(resource.resource_path.begins_with("res://Scripts/" + type + "/") and FileAccess.file_exists(resource.resource_path), type + " is stored in its correct directory", errors)
		_expect(FileAccess.file_exists(old_path), type + " source file is preserved", errors)
		_expect(resource.source_code == source and FileAccess.get_file_as_string(resource.resource_path) == source, type + " code is preserved exactly", errors)
		_expect(str(node.call("get_script_id")) == identity, type + " immutable identity is preserved", errors)
		var assigned_path := resource.resource_path
		_expect(not helper.adopt_node(node) and resource.resource_path == assigned_path, type + " adoption is idempotent", errors)
		node.name = str(node.name) + "Renamed"
		_expect(resource.resource_name == str(node.name) and resource.get_meta("rome_script_name", "") == str(node.name) and resource.get_meta("rome_script_type", "") == type, type + " logical name and tab metadata update", errors)
		_expect(resource.resource_path == assigned_path, type + " rename preserves references", errors)
		_expect(helper.unique_path(type, assigned_path.get_file().get_basename()) != assigned_path, type + " collisions never overwrite an existing resource", errors)
	helper.disconnect_watchers()
	for node in created:
		node.get_parent().remove_child(node)
		node.queue_free()
	return errors

static func _expect(condition: bool, title: String, errors: Array[String]) -> void:
	if not condition:
		errors.append(title)
