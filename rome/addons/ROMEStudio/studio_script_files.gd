@tool
extends RefCounted

## Keep game-owned script files outside the language extension. Existing files
## are copied and rebound, never moved or overwritten. The runtime's immutable
## script_id remains unchanged, preserving its delete/Undo lifecycle.
const TYPES := ["LocalScript", "ServerScript", "ModuleScript"]
var _watched: Array[WeakRef] = []

static func ensure_folders() -> void:
	for type: String in TYPES:
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://Scripts/" + type))

static func folder_for(type: String) -> String:
	return "res://Scripts/" + type if type in TYPES else ""

static func _filename(title: String, fallback: String) -> String:
	var stem := title.strip_edges().validate_filename().left(100)
	while stem.ends_with(".") or stem.ends_with(" "):
		stem = stem.left(-1)
	if stem.is_empty() or stem in [".", ".."]:
		stem = fallback
	var device := stem.get_slice(".", 0).to_upper()
	if device in ["CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"]:
		stem = "_" + stem
	return stem

static func unique_path(type: String, title: String) -> String:
	var folder := folder_for(type)
	if folder.is_empty():
		return ""
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(folder))
	var stem := _filename(title, type)
	var path := folder.path_join(stem + ".lua")
	var suffix := 2
	while FileAccess.file_exists(path) or ResourceLoader.has_cached(path):
		path = folder.path_join(stem + "_" + str(suffix) + ".lua")
		suffix += 1
	return path

static func is_legacy_generated_path(path: String, type: String) -> bool:
	return path.begins_with("res://GodotLuau/" + type + "s/") or path.begins_with("res://" + type + "s/") or path.begins_with("res://Scripts/ClassicObby/")

func organize_scene(root: Node) -> bool:
	ensure_folders()
	disconnect_watchers()
	if root == null:
		return false
	var changed := adopt_node(root, true)
	for node in root.find_children("*", "Node", true, false):
		changed = adopt_node(node, true) or changed
	return changed

func adopt_node(node: Node, include_legacy: bool = false) -> bool:
	if not is_instance_valid(node) or node.get_class() not in TYPES or not node.has_method("get_codigo_luau"):
		return false
	_watch(node)
	var resource := node.call("get_codigo_luau") as Script
	if resource == null:
		return false # Call after add_child so the runtime has selected its template.
	resource.resource_name = str(node.name)
	var type := node.get_class()
	resource.set_meta("rome_script_name", str(node.name))
	resource.set_meta("rome_script_type", type)
	var path := resource.resource_path
	if path.begins_with(folder_for(type) + "/"):
		return false
	if not path.is_empty() and not is_legacy_generated_path(path, type):
		return false # Preserve manually selected resources and their dependencies.
	if not include_legacy and not path.is_empty() and not path.begins_with("res://GodotLuau/" + type + "s/"):
		return false
	var source := resource.source_code
	# Empty in-memory source is valid, including an unsaved "select all/delete".
	# Never replace it with older disk contents while organizing the document.
	var destination := unique_path(type, str(node.name))
	if destination.is_empty():
		return false
	var file := FileAccess.open(destination, FileAccess.WRITE)
	if file == null:
		push_error("ROME: no se pudo crear el script " + destination)
		return false
	file.store_string(source)
	file.close()
	# Keep the same Script instance: open editor documents and other scene
	# references must continue pointing at their code after organization.
	resource.take_over_path(destination)
	resource.emit_changed()
	return true

func _watch(node: Node) -> void:
	for reference in _watched:
		if reference.get_ref() == node:
			return
	var reference := weakref(node)
	node.renamed.connect(_node_renamed.bind(reference))
	_watched.append(reference)

func _node_renamed(reference: WeakRef) -> void:
	var node := reference.get_ref() as Node
	if node == null:
		return
	var resource := node.call("get_codigo_luau") as Resource
	if resource != null:
		# Renaming a logical node changes its document label. Keep the physical
		# resource path stable so scenes and require references remain intact.
		resource.resource_name = str(node.name)
		resource.set_meta("rome_script_name", str(node.name))
		resource.set_meta("rome_script_type", node.get_class())
		resource.emit_changed()

func disconnect_watchers() -> void:
	for reference in _watched:
		var node := reference.get_ref() as Node
		if node == null:
			continue
		# The original WeakRef identity is retained, so this bound Callable matches.
		var callback := _node_renamed.bind(reference)
		if node.renamed.is_connected(callback):
			node.renamed.disconnect(callback)
	_watched.clear()
