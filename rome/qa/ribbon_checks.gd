extends RefCounted

static func run(native: Node, root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var tree := native.get_tree()
	var part_tool := native.find_child("ToolPieza", true, false) as Button
	var arrow := part_tool.get_node_or_null("Arrow") as Button if part_tool != null else null
	var menus := part_tool.find_children("*", "PopupMenu", false, false) if part_tool != null else []
	_expect(arrow != null and not menus.is_empty() and menus[0].item_count == 5, "Part tool offers its five shapes from a dropdown", report)
	var script_tool := native.find_child("ToolScript", true, false) as Button
	_expect(script_tool != null and script_tool.get_node_or_null("Arrow") != null, "Script tool offers Script, LocalScript and ModuleScript", report)
	var test_menu := native.find_child("ROMETestMenu", true, false) as MenuButton
	_expect(test_menu != null and test_menu.get_popup().item_count == 4, "Probar menu lists play, current scene and stop", report)

	var selection := EditorInterface.get_selection()
	selection.clear()
	await tree.process_frame
	await tree.process_frame
	var color_tool := native.find_child("ToolColor", true, false) as Button
	_expect(color_tool != null and color_tool.disabled, "Selection tools are unavailable without a selection", report)
	var workspace := root.get_node("Workspace")
	var part := workspace.get_child(0)
	for child in workspace.get_children():
		if child.is_class("Part"):
			part = child
			break
	selection.add_node(part)
	await tree.process_frame
	await tree.process_frame
	_expect(color_tool != null and not color_tool.disabled, "Selection tools become available with a selection", report)

	var editors := EditorInterface.get_base_control().find_children("*", "Node3DEditor", true, false)
	var toggle := native.find_child("ROMESnapToggle0", true, false) as CheckBox
	var step := native.find_child("ROMESnapValue0", true, false) as SpinBox
	if _expect(not editors.is_empty() and toggle != null and step != null, "Snap controls and the 3D editor are available", report):
		var original: bool = toggle.button_pressed
		toggle.button_pressed = true
		var state: Dictionary = editors[0].call("rome_get_view_state")
		_expect(bool(state.snap_enabled) and is_equal_approx(float(state.translate_snap), step.value), "Move snap applies its distance when checked", report)
		toggle.button_pressed = false
		state = editors[0].call("rome_get_view_state")
		var rotate_toggle := native.find_child("ROMESnapToggle1", true, false) as CheckBox
		_expect(is_zero_approx(float(state.translate_snap)) and bool(state.snap_enabled) == rotate_toggle.button_pressed, "Unchecking move snap frees movement", report)
		var mirrors := native.find_children("ROMESnapToggle0", "CheckBox", true, false)
		var synced := true
		for mirror: CheckBox in mirrors:
			synced = synced and mirror.button_pressed == toggle.button_pressed
		_expect(mirrors.size() >= 2 and synced, "Every ribbon page mirrors the same snap state", report)
		toggle.button_pressed = original

	native.get_viewport().gui_release_focus()
	EditorInterface.set_main_screen_editor("3D")
	await tree.process_frame
	var key := InputEventKey.new()
	key.keycode = KEY_2
	key.physical_keycode = KEY_2
	key.ctrl_pressed = true
	key.pressed = true
	Input.parse_input_event(key)
	await tree.process_frame
	await tree.process_frame
	var move_tool := native.find_child("ToolMover", true, false) as Button
	_expect(move_tool != null and move_tool.button_pressed, "Ctrl+2 selects the Move tool like Studio", report)
	key.keycode = KEY_1
	key.physical_keycode = KEY_1
	Input.parse_input_event(key)
	await tree.process_frame
	await tree.process_frame
	var select_tool := native.find_child("ToolSeleccionar", true, false) as Button
	_expect(select_tool != null and select_tool.button_pressed, "Ctrl+1 returns to the Select tool", report)
	selection.clear()
	return report

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(label)
	else:
		report.errors.append(label)
	return value
