extends RefCounted
## Disposable fixture: native mouse transforms and the registered resource dock.

const Panels = preload("res://addons/ROMESmoke/studio_panels_checks.gd")

static func run(root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var native := EditorInterface.get_base_control().get_parent()
	var resources := native.find_child("ROMEAssets", true, false) as EditorDock
	if _expect(resources != null, "Comfort fixture finds Resources", report):
		_expect(resources.default_slot == EditorDock.DOCK_SLOT_LEFT_BL, "Resources defaults to the lower left dock", report)
		_expect(resources.is_visible_in_tree() and resources.get_global_rect().get_center().x < native.get_viewport().get_visible_rect().size.x * 0.3, "Previous default Resources layout migrates visibly to the left", report)
		resources.call("refresh")
		var files := resources.find_child("ROMEAssetList", true, false) as Tree
		var scenes: TreeItem = resources.call("_find_item", files.get_root(), "res://Scenes")
		var local: TreeItem = resources.call("_find_item", files.get_root(), "res://Scripts/LocalScript")
		var server: TreeItem = resources.call("_find_item", files.get_root(), "res://Scripts/ServerScript")
		var module: TreeItem = resources.call("_find_item", files.get_root(), "res://Scripts/ModuleScript")
		if _expect(scenes != null and local != null and server != null and module != null, "Resources keeps the organized scene and typed script folders", report):
			_expect(local.get_icon_modulate(0) != server.get_icon_modulate(0) and server.get_icon_modulate(0) != module.get_icon_modulate(0) and module.get_icon_modulate(0) != local.get_icon_modulate(0), "Local, Server and Module script folders have distinct semantic colors", report)
			var scene: TreeItem = resources.call("_find_item", files.get_root(), root.scene_file_path)
			_expect(scene != null and scene.get_icon_modulate(0) == scenes.get_icon_modulate(0), "Scene resources use the scene folder color", report)
		var count_text := str(resources.get("hint").text)
		_expect(count_text.ends_with(" elementos") or count_text.begins_with("No hay") if resources.get("_view") == "grid" else count_text.begins_with(str(resources.get("_count")) + " recursos"), "Resources displays the visible resource count", report)
	await _transform_checks(root, report)
	report["completed"] = true
	return report

static func _transform_checks(root: Node, report: Dictionary) -> void:
	EditorInterface.set_main_screen_editor("3D")
	var native := EditorInterface.get_base_control().get_parent()
	var viewport := EditorInterface.get_editor_viewport_3d(0)
	var camera := viewport.get_camera_3d()
	var surface: Control
	for child in viewport.get_parent().get_parent().get_children():
		if child is Control and child.focus_mode == Control.FOCUS_ALL and not child.get_signal_connection_list("gui_input").is_empty():
			surface = child
	if not _expect(surface != null and camera != null, "Continuous drag has a real 3D surface", report):
		return
	var settings := EditorInterface.get_editor_settings()
	var key := "editors/3d/navigation/continuous_transform_grab"
	var original: bool = settings.get_setting(key)
	settings.set_setting(key, true)
	var target := Vector3(900, 80, 900)
	var part := Panels._part(root.get_node("Workspace"), root, "QAContinuousDrag", target, Vector3(4, 4, 4))
	var selection := EditorInterface.get_selection()
	selection.clear()
	selection.add_node(part)
	if not _expect(await Panels._look_at(target, 40.0, 0.7, 0.3), "Continuous drag camera settles on its isolated Part", report):
		return
	var snap := native.find_child("ROMESnapToggle0", true, false) as CheckBox
	var original_snap := snap.button_pressed
	snap.button_pressed = false
	native.get_viewport().gui_release_focus()
	var move := native.find_child("ToolMover", true, false) as Button
	move.pressed.emit()
	await Panels._frames(4)
	var start := camera.unproject_position(target)
	_button(surface, start, true)
	_motion(surface, start + Vector2(30, 15), Vector2(30, 15))
	var first := part.global_position
	_motion(surface, start + Vector2(60, 30), Vector2(30, 15))
	var second := part.global_position
	# Exercise both edges and the relocation event in one dispatch batch so real
	# zero-button OS events cannot interrupt the synthetic held-button fixture.
	var bounds := surface.get_global_rect().grow(-3 * EditorInterface.get_editor_scale())
	var edge := bounds.end + Vector2(1, 1)
	_motion(surface, edge - surface.global_position, Vector2(30, 15))
	var third := part.global_position
	_motion(surface, bounds.position + Vector2(31, 16) - surface.global_position, -bounds.size + Vector2(30, 15))
	var fourth := part.global_position
	_button(surface, start, false)
	await Panels._frames(3)
	var delta := second - first
	_expect(delta.length() > 0.1, "Dragging the selected Part changes its world position", report)
	_expect((third - second).distance_to(delta) < 0.003, "Crossing the right and bottom edges preserves continuous movement: " + str([delta, third - second]), report)
	_expect((fourth - third).distance_to(delta) < 0.003, "The cursor relocation event causes no object jump: " + str([delta, fourth - third]), report)
	var undo := EditorInterface.get_editor_undo_redo()
	var history := undo.get_history_undo_redo(undo.get_object_history_id(root))
	history.undo()
	await Panels._frames(3)
	_expect(part.global_position.is_equal_approx(target), "A wrapped drag commits one reversible transform", report)
	settings.set_setting(key, original)
	snap.button_pressed = original_snap
	selection.clear()
	part.get_parent().remove_child(part)
	part.queue_free()
	native.find_child("ToolSeleccionar", true, false).pressed.emit()
	await Panels._frames(3)

static func _button(surface: Control, point: Vector2, pressed: bool) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = MOUSE_BUTTON_LEFT
	event.button_mask = MOUSE_BUTTON_MASK_LEFT if pressed else 0
	event.position = point
	event.global_position = surface.global_position + point
	event.pressed = pressed
	surface.gui_input.emit(event)

static func _motion(surface: Control, point: Vector2, relative: Vector2) -> void:
	var event := InputEventMouseMotion.new()
	event.position = point
	event.global_position = surface.global_position + point
	event.relative = relative
	event.button_mask = MOUSE_BUTTON_MASK_LEFT
	surface.gui_input.emit(event)

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(label)
	else:
		report.errors.append(label)
	return value
