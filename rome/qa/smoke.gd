@tool
extends EditorPlugin

var checks: Array[String] = []
var _native: Node
var _custom_tab := -1

func _enter_tree() -> void:
	call_deferred("_run")

func _check(value: bool, label: String) -> bool:
	if not value:
		if _custom_tab >= 0 and is_instance_valid(_native):
			_native.call("rome_remove_custom_tab", _custom_tab)
			_custom_tab = -1
		push_error("ROME integration: " + label)
		get_tree().quit(1)
		return false
	checks.append(label)
	return true

func _run() -> void:
	await get_tree().create_timer(0.2).timeout
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().create_timer(0.1).timeout
	if EditorInterface.get_edited_scene_root() == null:
		EditorInterface.open_scene_from_path(str(ProjectSettings.get_setting("application/run/main_scene")))
	for attempt in 100:
		await get_tree().create_timer(0.05).timeout
		if EditorInterface.get_edited_scene_root() != null:
			break
	var root := EditorInterface.get_edited_scene_root()
	if not _check(root != null and root.is_class("RobloxGame3D"), "Game3D opens"):
		return
	for service in ["Workspace", "Players", "Lighting", "ServerScriptService", "ReplicatedStorage", "StarterGui"]:
		if not _check(root.has_node(NodePath(service)), "Service " + service):
			return
	var native := EditorInterface.get_base_control().get_parent()
	var output := native.find_child("Output", true, false) as EditorDock
	var bottom_panels := native.find_children("*", "EditorBottomPanel", true, false)
	if not _check(output != null and output.default_slot == EditorDock.DOCK_SLOT_BOTTOM and not bottom_panels.is_empty() and bottom_panels[0].is_ancestor_of(output), "Console is docked below the world"):
		return
	var console_banner := false
	var version := str(ROMEUpdateManager.get_singleton().get_status().get("current_rome_version", ""))
	for text: RichTextLabel in output.find_children("*", "RichTextLabel", true, false):
		if text.get_parsed_text().contains("ROME Engine " + version + " X Godot Luau | Rocket Meteor"):
			console_banner = true
	if not _check(console_banner, "Console starts with ROME and Godot Luau branding"):
		return
	await get_tree().process_frame
	var marker := root.get_node_or_null("Workspace/SpawnLocation/SpawnSymbol/Artwork") as MeshInstance3D
	if not _check(marker != null and marker.mesh is PlaneMesh, "Spawn symbol retains a separate decal mesh"):
		return
	_native = native
	var selection := EditorInterface.get_selection()
	selection.clear()
	native.emit_signal("rome_insert_object", "Part")
	var selected := selection.get_selected_nodes()
	if not _check(selected.size() == 1 and selected[0].is_class("RobloxPart"), "Ribbon inserts Part"):
		return
	var part := selected[0]
	if not _check(part.get_parent() == root.get_node("Workspace"), "Part belongs to Workspace"):
		return
	var anchored: bool = part.get("Anchored")
	native.emit_signal("rome_editor_action", "anchor", null)
	if not _check(bool(part.get("Anchored")) != anchored, "Anchor action changes property"):
		return
	get_undo_redo().get_history_undo_redo(get_undo_redo().get_object_history_id(part)).undo()
	if not _check(bool(part.get("Anchored")) == anchored, "Anchor undo restores property"):
		return
	native.emit_signal("rome_editor_action", "color", Color("ed914b"))
	if not _check(Color(part.get("Color")).is_equal_approx(Color("ed914b")), "Color action changes property"):
		return
	native.emit_signal("rome_editor_action", "group", null)
	if not _check(part.get_parent().is_class("RBXModel"), "Group action creates Model"):
		return
	get_undo_redo().get_history_undo_redo(get_undo_redo().get_object_history_id(part)).undo()
	if not _check(part.get_parent() == root.get_node("Workspace"), "Group undo restores parent"):
		return
	selection.clear()
	native.emit_signal("rome_insert_object", "ServerScript")
	selected = selection.get_selected_nodes()
	if not _check(selected.size() == 1 and selected[0].get_parent() == root.get_node("ServerScriptService"), "Script inserts into server service"):
		return
	if not _check(selected[0].has_method("get_codigo_luau"), "Script uses Luau resource"):
		return
	var initial_tabs: Array = native.call("rome_get_custom_tabs")
	var test_name := "Ribbon QA " + str(Time.get_ticks_msec())
	var created: int = native.call("rome_create_custom_tab", test_name)
	_custom_tab = created - 6
	if not _check(created >= 6, "Plus creates a custom tab after the six defaults"):
		return
	var tabs: Array = native.call("rome_get_custom_tabs")
	if not _check(tabs[-1].name == test_name and tabs[-1].tools.is_empty(), "New custom tab starts empty"):
		return
	if not _check(int(native.call("rome_create_custom_tab", test_name)) == -1, "Duplicate tab names are rejected"):
		return
	native.call("rome_set_custom_tools", _custom_tab, PackedStringArray(["Mover", "Recursos", "Script", "Mover", "Invalid QA Tool"]))
	tabs = native.call("rome_get_custom_tabs")
	if not _check(tabs[-1].tools == PackedStringArray(["Mover", "Recursos", "Script"]), "Custom tab stores valid ordered shortcuts without duplicates"):
		return
	var saved: Array = EditorInterface.get_editor_settings().get_setting("rome/studio/custom_tabs")
	if not _check(saved[-1].name == test_name and saved[-1].tools.size() == 3, "Toolbar preferences persist in editor settings"):
		return
	native.call("rome_remove_custom_tab", _custom_tab)
	_custom_tab = -1
	if not _check(var_to_str(native.call("rome_get_custom_tabs")) == var_to_str(initial_tabs), "Custom tab deletion preserves existing preferences"):
		return
	var original_storage := root.get_node("ReplicatedStorage")
	root.remove_child(original_storage)
	await get_tree().process_frame
	await get_tree().process_frame
	if not _check(root.has_node("ReplicatedStorage"), "Removed service container is recreated"):
		return
	root.add_child(original_storage, true)
	original_storage.owner = root
	await get_tree().process_frame
	await get_tree().process_frame
	if not _check(root.get_node("ReplicatedStorage") == original_storage, "Restoring an original service removes its temporary replacement"):
		return
	original_storage.name = "RenamedStorage"
	await get_tree().process_frame
	await get_tree().process_frame
	if not _check(root.get_node_or_null("ReplicatedStorage") == original_storage, "Service rename restores its canonical name"):
		return
	root.remove_child(original_storage)
	await get_tree().process_frame
	await get_tree().process_frame
	var replacement := root.get_node("ReplicatedStorage")
	var stored_content := Node.new()
	stored_content.name = "ContentAddedAfterRemoval"
	replacement.add_child(stored_content)
	stored_content.owner = root
	root.add_child(original_storage, true)
	original_storage.owner = root
	await get_tree().process_frame
	await get_tree().process_frame
	if not _check(stored_content.get_parent() == original_storage and stored_content.owner == root, "Undo restoration preserves content authored in the replacement service"):
		return
	var starter := root.get_node("StarterPlayer")
	var original_scripts := starter.get_node("StarterPlayerScripts")
	starter.remove_child(original_scripts)
	await get_tree().process_frame
	await get_tree().process_frame
	if not _check(starter.has_node("StarterPlayerScripts") and starter.get_node("StarterPlayerScripts").get_child_count() == 0, "Removing a script container recreates it empty"):
		return
	starter.add_child(original_scripts, true)
	original_scripts.owner = root
	await get_tree().process_frame
	await get_tree().process_frame
	var custom_failures: Array[String] = preload("res://addons/ROMESmoke/custom_tabs_checks.gd").run(native)
	if not _check(custom_failures.is_empty(), "Custom tab rename, duplicate, reorder, dividers and persistence: " + str(custom_failures)):
		return
	var resources := native.find_child("ROMEAssets", true, false)
	if not _check(resources != null, "Resource dock is registered"):
		return
	var model := Node3D.new()
	model.name = "ReusableModel"
	var model_scene := PackedScene.new()
	model_scene.pack(model)
	model.free()
	ResourceSaver.save(model_scene, "res://Scenes/QAReusable.tscn")
	resources.call("refresh")
	var asset_list: Tree = resources.find_child("ROMEAssetList", true, false)
	var asset_row: TreeItem = resources.call("_find_item", asset_list.get_root(), "res://Scenes/QAReusable.tscn")
	if not _check(asset_row != null, "Resources lists user-authored reusable scenes"):
		return
	asset_row.select(0)
	selection.clear()
	resources.call("_insert_selected")
	selected = selection.get_selected_nodes()
	if not _check(selected.size() == 1 and selected[0].get_parent() == root.get_node("Workspace") and selected[0].scene_file_path == "res://Scenes/QAReusable.tscn", "Resources inserts a native scene instance in Workspace"):
		return
	var instance := selected[0]
	get_undo_redo().get_history_undo_redo(get_undo_redo().get_object_history_id(instance)).undo()
	if not _check(instance.get_parent() == null, "Resource scene insertion supports Undo"):
		return
	native.call("rome_new_component_scene")
	await get_tree().process_frame
	var component := EditorInterface.get_edited_scene_root()
	if not _check(component is Node3D and not component.is_class("RobloxGame3D") and component.get_child_count() == 0, "Reusable scene starts as a plain Model without game services"):
		return
	EditorInterface.open_scene_from_path(str(ProjectSettings.get_setting("application/run/main_scene")))
	await get_tree().process_frame
	if not _check(EditorInterface.get_edited_scene_root() == root, "Returning to the main scene retains its data model"):
		return
	var routing_errors: Array[String] = preload("res://addons/ROMESmoke/script_files_checks.gd").run(root)
	if not _check(routing_errors.is_empty(), "Scripts use typed folders, stable resources and logical names: " + str(routing_errors)):
		return
	for directory in ["res://Scenes", "res://Scripts/LocalScript", "res://Scripts/ServerScript", "res://Scripts/ModuleScript"]:
		if not _check(DirAccess.dir_exists_absolute(directory), "Default resource folder " + directory):
			return
	if not _check(int(resources.call("create_folder", "QAOrganized", "res://Scenes")) in [OK, ERR_ALREADY_EXISTS], "Resources creates a real folder"):
		return
	if not _check(int(resources.call("create_folder", "../Invalid", "res://Scenes")) == ERR_INVALID_PARAMETER, "Resource folder validation rejects paths"):
		return
	var doc_report: Dictionary = await preload("res://addons/ROMESmoke/document_tabs_checks.gd").run(root)
	if not _check(doc_report.errors.is_empty(), "Unified document tabs: " + str(doc_report.errors)):
		return
	checks.append_array(doc_report.checks)
	var ribbon: Dictionary = await preload("res://addons/ROMESmoke/ribbon_checks.gd").run(native, root)
	if not _check(ribbon.errors.is_empty(), "Studio ribbon dropdowns, snapping and shortcuts: " + str(ribbon.errors)):
		return
	checks.append_array(ribbon.checks)
	var updates: Dictionary = preload("res://addons/ROMESmoke/update_checks.gd").run()
	if not _check(updates.errors.is_empty(), "Update release, version and integrity rules: " + str(updates.errors)):
		return
	checks.append_array(updates.checks)
	var stamp = JSON.parse_string(FileAccess.get_file_as_string("res://.rome/runtime.json"))
	var manifest := OS.get_executable_path().get_base_dir().path_join("rome_runtime/manifest.json")
	var identity: String = FileAccess.get_file_as_string(manifest).sha256_text().left(12) if FileAccess.file_exists(manifest) else ""
	if not _check(stamp is Dictionary and str(stamp.get("identity", "")).length() == 12 and (identity.is_empty() or stamp.identity == identity), "Opening the experience records its runtime in .rome/runtime.json"):
		return
	var panels: Dictionary = await preload("res://addons/ROMESmoke/studio_panels_checks.gd").run(root)
	if not _check(panels.has("errors") and panels.errors.is_empty(), "Studio panels, insertion and properties: " + str(panels.get("errors", "la prueba se detuvo"))):
		return
	checks.append_array(panels.checks)
	# A nested main loop inside a timer callback (here the mesh previews' progress dialog) fired
	# that timer again and freed it twice while another one was pending (0xC0000374 on close).
	get_tree().create_timer(30.0)
	var timer := get_tree().create_timer(0.05)
	var fired := [0]
	timer.timeout.connect(func() -> void: fired[0] += 1)
	await timer.timeout
	var meshes: Array[Mesh] = [BoxMesh.new()]
	EditorInterface.make_mesh_previews(meshes, 32)
	await get_tree().process_frame
	if not _check(fired[0] == 1, "A nested main loop inside a timer callback does not fire it twice"):
		return
	var report := FileAccess.open("res://integration-result.json", FileAccess.WRITE)
	report.store_string(JSON.stringify({"passed": true, "checks": checks}, "  "))
	report.close()
	print("ROME integration passed: ", checks.size(), " checks")
	get_tree().quit()
