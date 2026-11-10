extends RefCounted

static func run(root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var selection := EditorInterface.get_selection()
	var previous := selection.get_selected_nodes()
	var folder := Node.new()
	folder.name = "QAMulti" + str(Time.get_ticks_usec())
	root.get_node("Workspace").add_child(folder)
	folder.owner = root
	var first := _node("Part", "First", folder, root)
	var second := _node("Part", "Second", folder, root)
	first.position = Vector3(1, 2, 3)
	second.position = Vector3(4, 5, 6)
	first.set("Color", Color.RED)
	second.set("Color", Color.BLUE)
	first.set("Anchored", true)
	second.set("Anchored", false)
	var history := EditorInterface.get_editor_undo_redo().get_history_undo_redo(EditorInterface.get_editor_undo_redo().get_object_history_id(root))
	await _select([first, second])
	var panel := _panel()
	if not _expect(panel != null and panel.get("multi") == true, "Selecting two Parts uses the Studio property grid", report):
		return report
	_expect(str(panel.get("object_title").text).begins_with("2 objetos"), "Multi-property header identifies the selected object count", report)
	_expect(panel.rows.Color.get_text(1) == "—" and panel.rows.position.get_text(1) == "—" and panel.rows.Anchored.is_indeterminate(1), "Mixed colors, positions and booleans are displayed as mixed values", report)
	_expect(not panel.add_button.visible and not panel.sections.has("Atributos"), "Multi-selection does not offer single-object attribute mutations", report)
	# Open the mixed Name and confirm it without typing, as Enter or a click outside would.
	panel.table.set_selected(panel.rows.name, 1)
	panel.table.edit_selected(true)
	await _frames(2)
	var confirmed := false
	for editor: LineEdit in panel.table.find_children("*", "LineEdit", true, false):
		if editor.is_visible_in_tree():
			confirmed = true
			editor.text_submitted.emit(editor.text)
	await _frames(3)
	_expect(confirmed and first.name == "First" and second.name == "Second", "Confirming a mixed Name without typing keeps each object's name", report)
	panel.commit("Transparency", 0.5)
	await _frames(3)
	_expect(is_equal_approx(first.get("Transparency"), 0.5) and is_equal_approx(second.get("Transparency"), 0.5), "One property edit changes both selected Parts", report)
	history.undo()
	await _frames(3)
	_expect(is_zero_approx(first.get("Transparency")) and is_zero_approx(second.get("Transparency")), "One Undo restores each selected object's previous value", report)
	history.redo()
	await _frames(3)
	_expect(is_equal_approx(first.get("Transparency"), 0.5) and is_equal_approx(second.get("Transparency"), 0.5), "Multi-property changes support Redo", report)
	history.undo()
	await _frames(3)
	panel = _panel()
	panel._commit_components(panel.rows.position.get_child(0).get_metadata(0), 12.5)
	await _frames(3)
	_expect(first.position.is_equal_approx(Vector3(12.5, 2, 3)) and second.position.is_equal_approx(Vector3(12.5, 5, 6)), "Editing a shared X coordinate preserves each Part's different Y and Z", report)
	history.undo()
	await _frames(3)
	_expect(first.position.is_equal_approx(Vector3(1, 2, 3)) and second.position.is_equal_approx(Vector3(4, 5, 6)), "Component Undo restores both original vectors", report)
	first.rotation = Vector3(0.1, 0.2, 0.3)
	second.rotation = Vector3(0.4, 0.5, 0.6)
	panel = _panel()
	panel._commit_components(panel.rows.rotation.get_child(1).get_metadata(0), 90.0)
	await _frames(3)
	_expect(is_equal_approx(first.rotation.y, PI / 2) and is_equal_approx(second.rotation.y, PI / 2) and is_equal_approx(first.rotation.x, 0.1) and is_equal_approx(second.rotation.z, 0.6), "Multi-orientation edits use degrees and preserve untouched axes", report)
	history.undo()
	await _frames(3)
	var light := _node("PointLight", "Light", folder, root)
	await _select([first, light])
	panel = _panel()
	_expect(panel != null and panel.rows.has("Color") and panel.rows.has("position") and not panel.rows.has("Anchored") and not panel.rows.has("Brightness"), "Different classes display only their compatible common properties", report)
	var screen := _node("ScreenGui", "MultiScreen", root.get_node("StarterGui"), root)
	var frame_a := _node("Frame", "FrameA", screen, root)
	var frame_b := _node("Frame", "FrameB", screen, root)
	var size_a := Vector4(0.2, 10, 0.3, 20)
	var size_b := Vector4(0.8, 30, 0.9, 40)
	frame_a.set("Size", size_a)
	frame_b.set("Size", size_b)
	await _select([frame_a, frame_b])
	panel = _panel()
	if _expect(panel != null and panel.rows.has("Size"), "Selected GUI instances retain the Studio UDim2 editor", report):
		var pair: TreeItem = panel.rows.Size.get_child(0)
		_expect(not pair.is_editable(1) and pair.get_child(1).is_editable(1), "A mixed UDim2 pair is read-only while individual Scale and Offset remain editable", report)
		panel._commit_components(pair.get_child(1).get_metadata(0), 64.0)
		await _frames(3)
		_expect(frame_a.get("Size").is_equal_approx(Vector4(0.2, 64, 0.3, 20)) and frame_b.get("Size").is_equal_approx(Vector4(0.8, 64, 0.9, 40)), "Editing a UDim2 offset preserves each object's scales and other offset", report)
		history.undo()
		await _frames(3)
		_expect(frame_a.get("Size").is_equal_approx(size_a) and frame_b.get("Size").is_equal_approx(size_b), "Multi-UDim2 edit supports Undo", report)
	var canvas_a := Node2D.new()
	var canvas_b := Node2D.new()
	folder.add_child(canvas_a)
	folder.add_child(canvas_b)
	canvas_a.owner = root
	canvas_b.owner = root
	await _select([canvas_a, canvas_b])
	_expect(_panel() == null, "Native 2D multi-selection keeps its complete native Inspector", report)
	selection.clear()
	screen.get_parent().remove_child(screen)
	screen.queue_free()
	folder.get_parent().remove_child(folder)
	folder.queue_free()
	for node in previous:
		if is_instance_valid(node) and node.is_inside_tree():
			selection.add_node(node)
	await _frames(3)
	report["completed"] = true
	return report

static func _node(type: String, title: String, parent: Node, root: Node) -> Node:
	var node: Node = ClassDB.instantiate(type)
	node.name = title
	parent.add_child(node)
	node.owner = root
	return node

static func _select(nodes: Array) -> void:
	var selection := EditorInterface.get_selection()
	selection.clear()
	for node in nodes:
		selection.add_node(node)
	await _frames(5)

static func _panel() -> Node:
	return EditorInterface.get_inspector().find_child("ROMEStudioProperties", true, false)

static func _frames(count: int) -> void:
	for frame in count:
		await EditorInterface.get_base_control().get_tree().process_frame

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	(report.checks if value else report.errors).append(label)
	return value
