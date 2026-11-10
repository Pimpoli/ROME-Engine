extends RefCounted

static func run(root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var resources := EditorInterface.get_base_control().find_child("ROMEAssets", true, false)
	if not _expect(resources != null and resources.has_method("set_view"), "Resources offers tree and thumbnail views", report):
		return report
	var previous_view: String = resources.get("_view")
	var previous_folder: String = resources.get("_folder")
	var previous_search: String = resources.search.text
	var previous_category: int = resources.category.selected
	var prefix := "GridQA" + str(Time.get_ticks_usec())
	var folder := "res://Scenes/" + prefix
	_expect(resources.create_folder(prefix, "res://Scenes") == OK, "The thumbnail fixture creates an organized asset folder", report)
	var texture := GradientTexture2D.new()
	texture.gradient = Gradient.new()
	texture.gradient.set_color(0, Color("ff9b32"))
	texture.gradient.set_color(1, Color("4035d4"))
	var texture_path := folder.path_join(prefix + "Texture.tres")
	_expect(ResourceSaver.save(texture, texture_path) == OK, "A real texture resource is saved for thumbnail generation", report)
	resources.search.text = ""
	resources.category.select(0)
	resources.refresh()
	resources.set_view("grid")
	resources.browse_folder(folder)
	await _frames(4)
	var grid := resources.get("grid") as ItemList
	_expect(grid != null and grid.is_visible_in_tree() and not resources.files.visible and resources.location_row.visible, "Thumbnail view replaces the tree and exposes folder navigation", report)
	var index := _index(grid, texture_path)
	if _expect(index >= 0, "The folder's texture appears as a named thumbnail tile", report):
		resources.call("_grid_selected", index)
		_expect(str(resources.call("_selected_path")) == texture_path and resources.insert.disabled, "A tile shares selection and open/insert rules with the resource tree", report)
		for attempt in 300:
			if bool(resources.get("_previewed").get(texture_path, false)):
				break
			await _frames(1)
		_expect(bool(resources.get("_previewed").get(texture_path, false)), "The native previewer supplies a genuine generated texture thumbnail", report)
	var settings := EditorInterface.get_editor_settings()
	_expect(settings.get_project_metadata("rome", "resource_view", "") == "grid" and settings.get_project_metadata("rome", "resource_folder", "") == folder, "View mode and folder are saved per project", report)
	var old_generation := int(resources.get("_preview_generation"))
	resources.up.pressed.emit()
	_expect(resources.get("_folder") == "res://Scenes", "Up navigation returns to the parent resource folder", report)
	if grid.item_count > 0:
		var icon := grid.get_item_icon(0)
		resources.call("_grid_preview", texture_path, texture, null, {"generation": old_generation, "index": 0})
		_expect(grid.get_item_icon(0) == icon, "A stale asynchronous thumbnail cannot overwrite another folder's tile", report)
	resources.search.text = prefix + "Texture"
	resources.refresh()
	_expect(grid.item_count == 1 and str(grid.get_item_metadata(0)) == texture_path, "Search finds resources across folders while using thumbnails", report)
	resources.search.text = ""
	resources.refresh()
	resources.browse_folder("res://Scenes")
	var scene_path := "res://Scenes/QAReusable.tscn"
	index = _index(grid, scene_path)
	if _expect(index >= 0, "Reusable scenes remain available as thumbnail tiles", report):
		resources.call("_grid_selected", index)
		EditorInterface.get_selection().clear()
		resources.call("_insert_selected")
		await _frames(4)
		var selected := EditorInterface.get_selection().get_selected_nodes()
		_expect(selected.size() == 1 and selected[0].scene_file_path == scene_path and selected[0].get_parent() == root.get_node("Workspace"), "The thumbnail's Insert action creates the real scene instance in Workspace", report)
		var undo := EditorInterface.get_editor_undo_redo()
		undo.get_history_undo_redo(undo.get_object_history_id(root)).undo()
		await _frames(3)
	# Move a real dependency through the native dock; a raw filesystem rename
	# would leave the resource reference pointing at the old path.
	var dependent := Sprite2D.new()
	dependent.texture = load(texture_path)
	var packed := PackedScene.new()
	packed.pack(dependent)
	dependent.free()
	var dependent_path := "res://Scenes/" + prefix + "Owner.tscn"
	ResourceSaver.save(packed, dependent_path)
	await _scan_done()
	EditorInterface.get_resource_filesystem().scan()
	await _scan_done()
	var destination := "res://Scenes/" + prefix + "Moved"
	_expect(await _relocate(resources, folder, destination) == OK, "Resources moves a folder through the native dependency pipeline", report)
	await _scan_done()
	var new_texture := destination.path_join(texture_path.get_file())
	_expect(FileAccess.file_exists(new_texture) and not DirAccess.dir_exists_absolute(folder), "Moving a folder keeps its resource contents", report)
	var owner_text := FileAccess.get_file_as_string(dependent_path)
	_expect(owner_text.contains(new_texture) and not owner_text.contains(texture_path), "Moving assets rewrites references in dependent scenes", report)
	var renamed := destination.path_join("Renamed.tres")
	_expect(await _relocate(resources, new_texture, renamed) == OK, "Resources renames a file without changing its type", report)
	await _scan_done()
	_expect(FileAccess.get_file_as_string(dependent_path).contains(renamed), "Renaming an asset preserves its scene references", report)
	_expect(resources.relocate_resource(destination, destination.path_join("Child")) == ERR_INVALID_PARAMETER, "A folder cannot move inside itself", report)
	_expect(resources.relocate_resource(renamed, dependent_path) == ERR_INVALID_PARAMETER, "Moving cannot silently change an asset extension", report)
	_expect(resources.relocate_resource(destination, "res://addons/Moved") == ERR_INVALID_PARAMETER, "Bundled addon directories are protected from asset relocation", report)
	resources.search.text = previous_search
	resources.category.select(previous_category)
	resources.browse_folder(previous_folder)
	resources.set_view(previous_view)
	resources.refresh()
	await _frames(3)
	report["completed"] = true
	return report

static func _index(grid: ItemList, path: String) -> int:
	for index in grid.item_count:
		if str(grid.get_item_metadata(index)) == path:
			return index
	return -1

static func _frames(count: int) -> void:
	for frame in count:
		await EditorInterface.get_base_control().get_tree().process_frame

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	(report.checks if value else report.errors).append(label)
	return value

static func _scan_done() -> void:
	await _frames(3)
	for attempt in 600:
		if EditorInterface.get_file_system_dock().call("rome_is_resource_operation_ready"):
			break
		await _frames(1)
	await _frames(3)

static func _relocate(resources: Node, source: String, destination: String) -> Error:
	for attempt in 100:
		var result: Error = resources.relocate_resource(source, destination)
		if result != ERR_BUSY:
			return result
		await EditorInterface.get_base_control().get_tree().create_timer(0.1).timeout
	return ERR_BUSY
