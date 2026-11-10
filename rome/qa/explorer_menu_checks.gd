extends RefCounted

static func run(root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var native := EditorInterface.get_base_control().get_parent()
	var editor: Node = native.find_children("*", "SceneTreeEditor", true, false)[0]
	var selection := EditorInterface.get_selection()
	var old := selection.get_selected_nodes()
	var workspace := root.get_node("Workspace")
	var folder := _node("Folder", "QAClipboard" + str(Time.get_ticks_usec()), workspace, root)
	var parent_a := _node("Folder", "ParentA", folder, root)
	var parent_b := _node("Folder", "ParentB", folder, root)
	var source := _node("Part", "ClipboardPart", folder, root)
	source.position = Vector3(14, 8, -4)
	await _select([source])
	var menu := await _menu(native, editor)
	var advanced := native.find_child("ROMEExplorerAdvanced", true, false) as PopupMenu
	if not _expect(menu != null and advanced != null, "Explorer provides a compact context menu and an advanced submenu", report):
		return report
	_expect(menu.get_item_count() < advanced.get_item_count() and _id(menu, "Copiar") >= 0 and _id(menu, "Renombrar") >= 0 and _id(menu, "Pegar dentro") >= 0, "The compact menu exposes common Studio actions without the advanced action list", report)
	_expect(_disabled(menu, "Pegar"), "Paste is disabled before objects are copied", report)
	var copy_id := _id(menu, "Copiar")
	var duplicate_id := _id(menu, "Duplicar")
	var paste_id := _id(menu, "Pegar")
	var into_id := _id(menu, "Pegar dentro")
	menu.id_pressed.emit(copy_id)
	await _select([parent_a])
	menu = await _menu(native, editor)
	_expect(not _disabled(menu, "Pegar") and not _disabled(menu, "Pegar dentro"), "Copy enables both paste destinations", report)
	menu.id_pressed.emit(paste_id)
	await _frames(4)
	var pasted := selection.get_selected_nodes()
	var history := EditorInterface.get_editor_undo_redo().get_history_undo_redo(EditorInterface.get_editor_undo_redo().get_object_history_id(root))
	_expect(pasted.size() == 1 and pasted[0].get_parent() == workspace and pasted[0].owner == root, "Paste targets top-level Workspace even when another parent is selected", report)
	history.undo()
	await _frames(3)
	await _select([parent_a])
	var tree: Tree = editor.find_children("*", "Tree", true, false)[0]
	tree.grab_focus()
	_key(false)
	await _frames(4)
	pasted = selection.get_selected_nodes()
	_expect(pasted.size() == 1 and pasted[0].get_parent() == workspace, "Ctrl+V uses the top-level Workspace destination", report)
	history.undo()
	await _frames(3)
	await _select([parent_a, parent_b])
	tree.grab_focus()
	_key(true)
	await _frames(4)
	_expect(parent_a.get_child_count() == 1 and parent_b.get_child_count() == 1, "Ctrl+Shift+V pastes into every selected parent", report)
	history.undo()
	await _frames(3)
	await _select([parent_a, parent_b])
	menu = await _menu(native, editor)
	menu.id_pressed.emit(into_id)
	await _frames(4)
	pasted = selection.get_selected_nodes()
	_expect(pasted.size() == 2 and parent_a.get_child_count() == 1 and parent_b.get_child_count() == 1 and pasted.all(func(node: Node) -> bool: return node.owner == root), "Paste Into inserts the clipboard into every selected parent", report)
	history.undo()
	await _frames(3)
	_expect(parent_a.get_child_count() == 0 and parent_b.get_child_count() == 0, "One Undo removes all copies from multi-parent Paste Into", report)
	history.redo()
	await _frames(3)
	_expect(parent_a.get_child_count() == 1 and parent_b.get_child_count() == 1, "Multi-parent Paste Into supports Redo", report)
	history.undo()
	await _frames(3)
	await _select([source])
	menu = await _menu(native, editor)
	menu.id_pressed.emit(duplicate_id)
	await _frames(4)
	pasted = selection.get_selected_nodes()
	_expect(pasted.size() == 1 and pasted[0] != source and pasted[0].get_parent() == folder and pasted[0].position.is_equal_approx(source.position), "Duplicate keeps the source branch and local transform", report)
	history.undo()
	await _frames(3)
	await _select([workspace])
	menu = await _menu(native, editor)
	_expect(_disabled(menu, "Copiar") and _disabled(menu, "Cortar") and _disabled(menu, "Renombrar") and _disabled(menu, "Duplicar"), "Fixed services disable invalid clipboard and rename operations", report)
	menu.id_pressed.emit(duplicate_id)
	menu.id_pressed.emit(copy_id)
	_expect(workspace.get_parent() == root and selection.get_selected_nodes() == [workspace], "Fixed-service protection is revalidated when an action is dispatched", report)
	selection.clear()
	folder.get_parent().remove_child(folder)
	folder.queue_free()
	for node in old:
		if is_instance_valid(node) and node.is_inside_tree():
			selection.add_node(node)
	await _frames(3)
	report["completed"] = true
	return report

static func _menu(native: Node, editor: Node) -> PopupMenu:
	editor.emit_signal("rmb_pressed", Vector2(250, 200))
	await _frames(2)
	var menu := native.find_child("ROMEExplorerContext", true, false) as PopupMenu
	if menu != null:
		menu.hide()
	return menu

static func _id(menu: PopupMenu, label: String) -> int:
	for index in menu.item_count:
		if menu.get_item_text(index) == label:
			return menu.get_item_id(index)
	return -1

static func _disabled(menu: PopupMenu, label: String) -> bool:
	var id := _id(menu, label)
	return id < 0 or menu.is_item_disabled(menu.get_item_index(id))

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
	await _frames(4)

static func _frames(count: int) -> void:
	for frame in count:
		await EditorInterface.get_base_control().get_tree().process_frame

static func _key(shift: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = KEY_V
	event.physical_keycode = KEY_V
	event.ctrl_pressed = true
	event.shift_pressed = shift
	event.pressed = true
	Input.parse_input_event(event)
	event = event.duplicate()
	event.pressed = false
	Input.parse_input_event(event)

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	(report.checks if value else report.errors).append(label)
	return value
