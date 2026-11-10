extends RefCounted

## Disposable fixture only: uses real filter rows, shortcuts and context menus.
static func run(root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var native := EditorInterface.get_base_control().get_parent()
	var filter := native.find_child("ROMEExplorerFilter", true, false) as LineEdit
	var editors := native.find_children("*", "SceneTreeEditor", true, false)
	if not _expect(filter != null and not editors.is_empty(), "Explorer exposes its search and hierarchy", report):
		return report
	var scene_editor: Node = editors[0]
	var trees := scene_editor.find_children("*", "Tree", true, false)
	if not _expect(not trees.is_empty(), "Explorer exposes the native object tree", report):
		return report
	var tree: Tree = trees[0]
	var original_filter := filter.text
	var selection := EditorInterface.get_selection()
	var original_selection := selection.get_selected_nodes()
	var prefix := "ExplorerQA" + str(Time.get_ticks_usec())
	var folder := _node("Folder", prefix, root.get_node("Workspace"), root)
	var part := _node("Part", prefix + "Board", folder, root)
	var other := _node("Part", prefix + "Other", folder, root)
	var wedge := _node("Part", prefix + "Wedge", folder, root)
	wedge.set_meta("__rbx_class", "WedgePart")
	wedge.set("Shape", 3)
	var emitter := _node("ParticleEmitter", prefix + "Emitter", folder, root)
	var accessory := _node("RBXModel", prefix + "Accessory", folder, root)
	accessory.set_meta("__rbx_class", "Accessory")
	var model := _node("RBXModel", prefix + "Model", folder, root)
	await _frames(5)
	# Use a real folded branch and verify search does not change its saved state.
	var branch := _row(tree.get_root(), prefix)
	if branch != null:
		branch.collapsed = true
	await _frames(2)
	var folded_before := folder.is_displayed_folded()
	await _search(filter, prefix + " is:Part")
	_expect(_matches(tree, part.name) and _matches(tree, other.name) and not _matches(tree, wedge.name) and not _matches(tree, emitter.name), "is:Part matches public class identity without ParticleEmitter or WedgePart false positives", report)
	branch = _row(tree.get_root(), prefix)
	_expect(branch != null and branch.visible and not branch.is_selectable(0) and not branch.collapsed and folder.is_displayed_folded() == folded_before, "Search reveals folded ancestors without selecting them or changing saved folds", report)
	await _search(filter, prefix + " is:basepart")
	_expect(_matches(tree, part.name) and _matches(tree, wedge.name) and not _matches(tree, accessory.name), "is:BasePart follows public inheritance and ignores case", report)
	await _search(filter, prefix + " is:Model")
	_expect(_matches(tree, model.name) and not _matches(tree, accessory.name), "Accessory identity is preserved instead of matching its physical Model implementation", report)
	await _search(filter, prefix + " is:Accoutrement")
	_expect(_matches(tree, accessory.name) and not _matches(tree, model.name), "Public Accessory ancestry can be searched", report)
	await _search(filter, prefix + " is:")
	_expect(not _matches(tree, part.name) and not _matches(tree, model.name), "An empty class query does not match all objects", report)
	await _search(filter, prefix + " is:UnknownRomeClass")
	_expect(not _matches(tree, part.name), "Unknown public classes return no matches", report)
	await _search(filter, prefix + " is:Part Board")
	_expect(_matches(tree, part.name) and not _matches(tree, other.name), "Class queries combine with name terms", report)
	await _search(filter, "")
	branch = _row(tree.get_root(), prefix)
	_expect(branch != null and branch.collapsed == folded_before and folder.is_displayed_folded() == folded_before, "Clearing search restores the original branch fold", report)
	# Keyboard selection is scoped to matching results; Ctrl+A never inserts.
	await _search(filter, prefix + " is:BasePart")
	selection.clear()
	tree.grab_focus()
	_key(KEY_A, true)
	await _frames(3)
	var selected := selection.get_selected_nodes()
	_expect(selected.has(part) and selected.has(other) and selected.has(wedge) and not selected.has(folder) and not selected.has(root), "Ctrl+A selects matching search results and skips contextual ancestors", report)
	var insert_dialog := native.find_child("ROMEInsertObject", true, false)
	_expect(insert_dialog == null or not insert_dialog.visible, "Ctrl+A does not open the insertion dialog in ROME", report)
	await _search(filter, "")
	_key(KEY_X, true, true)
	await _frames(3)
	_expect(filter.has_focus(), "Ctrl+Shift+X jumps to Explorer search", report)
	filter.release_focus()
	EditorInterface.get_base_control().get_viewport().gui_release_focus()
	EditorInterface.set_main_screen_editor("3D")
	_select(selection, [folder])
	_key(KEY_I, true)
	await _frames(3)
	var dialogs := native.find_children("*", "Window", true, false)
	var opened_insert := false
	for dialog in dialogs:
		if dialog.title == "Insertar objeto" and dialog.visible:
			opened_insert = true
			dialog.hide()
	_expect(opened_insert, "Ctrl+I opens the real object insertion window", report)
	# Context checks are kept separate so the same menu is exercised by mouse UI.
	await _context_checks(native, scene_editor, root, folder, part, other, report)
	await _picker_checks(native, report)
	await _search(filter, original_filter)
	selection.clear()
	folder.get_parent().remove_child(folder)
	folder.queue_free()
	for node in original_selection:
		if is_instance_valid(node) and node.is_inside_tree():
			selection.add_node(node)
	report["completed"] = true
	return report

static func _context_checks(native: Node, scene_editor: Node, root: Node, folder: Node, part: Node, other: Node, report: Dictionary) -> void:
	var selection := EditorInterface.get_selection()
	_select(selection, [folder])
	await _frames(3)
	scene_editor.emit_signal("rmb_pressed", Vector2(200, 200))
	var context := native.find_child("ROMEExplorerContext", true, false) as PopupMenu
	var hierarchy := native.find_child("ROMEExplorerHierarchy", true, false) as PopupMenu
	if not _expect(context != null and hierarchy != null, "Explorer context menu includes hierarchy commands", report):
		return
	context.hide()
	hierarchy.id_pressed.emit(45)
	await _frames(3)
	var nodes := selection.get_selected_nodes()
	_expect(nodes.has(part) and nodes.has(other) and nodes.all(func(node: Node) -> bool: return node.get_parent() == folder and node.owner == root), "Select Children selects only direct editable children", report)
	_select(selection, [part, other])
	hierarchy.id_pressed.emit(46)
	await _frames(3)
	_expect(selection.get_selected_nodes() == [folder], "Select Parents deduplicates the common parent", report)
	var first := part as Node3D
	var transform_before := first.global_transform
	var undo := EditorInterface.get_editor_undo_redo()
	var history := undo.get_history_undo_redo(undo.get_object_history_id(root))
	_select(selection, [part, other])
	context.id_pressed.emit(42)
	await _frames(3)
	var model := part.get_parent()
	_expect(model.is_class("RBXModel") and other.get_parent() == model and model.get_parent() == folder and first.global_transform.is_equal_approx(transform_before), "Context grouping creates a Model and preserves world transforms", report)
	_select(selection, [model])
	context.id_pressed.emit(44)
	await _frames(3)
	_expect(part.get_parent() == folder and other.get_parent() == folder and first.global_transform.is_equal_approx(transform_before), "Context Ungroup restores objects to their container", report)
	history.undo()
	await _frames(3)
	_expect(part.get_parent() == model and other.get_parent() == model, "Context Ungroup supports Undo", report)
	history.undo()
	await _frames(3)
	_expect(part.get_parent() == folder and other.get_parent() == folder, "Context Model grouping supports Undo", report)
	_select(selection, [part, other])
	context.id_pressed.emit(43)
	await _frames(3)
	_expect(part.get_parent().is_class("Folder") and part.get_parent() != folder and other.get_parent() == part.get_parent(), "Context grouping can organize the selection as a Folder", report)
	history.undo()
	await _frames(3)
	var workspace := root.get_node("Workspace")
	_select(selection, [workspace])
	await _frames(3)
	scene_editor.emit_signal("rmb_pressed", Vector2(200, 200))
	_expect(context.is_item_disabled(context.get_item_index(42)) and context.is_item_disabled(context.get_item_index(43)), "Fixed services disable grouping in the context menu", report)
	context.hide()
	context.id_pressed.emit(42)
	_expect(workspace.get_parent() == root and selection.get_selected_nodes() == [workspace], "Fixed-service grouping is rejected again at dispatch", report)
	hierarchy.id_pressed.emit(46)
	await _frames(3)
	_expect(not selection.get_selected_nodes().has(root), "Select Parents never selects the hidden DataModel root", report)

# Node pickers reuse the Explorer's tree control, but they have no selection behind them.
static func _picker_checks(native: Node, report: Dictionary) -> void:
	var dialogs := native.find_children("*", "ReparentDialog", true, false)
	if not _expect(not dialogs.is_empty(), "The reparent dialog shares the Explorer tree control", report):
		return
	var dialog := dialogs[0] as Window
	dialog.popup_centered(Vector2i(360, 480))
	await _frames(8)
	var tree: Tree = dialog.find_children("*", "Tree", true, false)[0]
	var row := _row(tree.get_root(), "Workspace")
	if not _expect(row != null, "The reparent dialog lists the services", report):
		dialog.hide()
		return
	var motion := InputEventMouseMotion.new()
	motion.position = tree.get_item_area_rect(row, 0).get_center()
	tree.gui_input.emit(motion)
	await _frames(2)
	_expect(row.get_button_by_id(0, 10) < 0, "Hovering a row in a node picker does not offer the Explorer insert button", report)
	tree.button_clicked.emit(row, 0, 10, MOUSE_BUTTON_LEFT)
	await _frames(2)
	var insert_dialog := native.find_child("ROMEInsertObject", true, false)
	_expect(insert_dialog == null or not insert_dialog.visible, "An insert click inside a node picker is ignored", report)
	dialog.hide()
	await _frames(2)

static func _node(type: String, title: String, parent: Node, root: Node) -> Node:
	var node: Node = ClassDB.instantiate(type)
	node.name = title
	parent.add_child(node)
	node.owner = root
	return node

static func _select(selection: EditorSelection, nodes: Array) -> void:
	selection.clear()
	for node in nodes:
		selection.add_node(node)

static func _key(code: Key, ctrl := false, shift := false) -> void:
	var key := InputEventKey.new()
	key.keycode = code
	key.physical_keycode = code
	key.ctrl_pressed = ctrl
	key.shift_pressed = shift
	key.pressed = true
	Input.parse_input_event(key)
	key = key.duplicate()
	key.pressed = false
	Input.parse_input_event(key)

static func _row(item: TreeItem, title: String) -> TreeItem:
	if item == null:
		return null
	if item.get_text(0) == title:
		return item
	for child in item.get_children():
		var match_row := _row(child, title)
		if match_row != null:
			return match_row
	return null

static func _matches(tree: Tree, title: String) -> bool:
	var item := _row(tree.get_root(), title)
	return item != null and item.visible and item.is_selectable(0)

static func _search(filter: LineEdit, query: String) -> void:
	filter.text = query
	filter.text_changed.emit(query)
	await _frames(3)

static func _frames(count: int) -> void:
	for frame in count:
		await EditorInterface.get_base_control().get_tree().process_frame

static func _expect(value: bool, text: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(text)
	else:
		report.errors.append(text)
	return value
