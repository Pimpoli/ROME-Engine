extends RefCounted
## Run only inside a disposable ROME editor fixture. It inserts, groups and edits
## objects in the open world and moves the 3D camera; nothing is saved.

const Catalog = preload("res://addons/ROMEStudio/studio_catalog.gd")
const Placement = preload("res://addons/ROMEStudio/studio_placement.gd")
const TARGET := Vector3(700, 40, 700)
const SIZES := [Vector3(4, 1, 2), Vector3(2, 2, 2), Vector3(2, 2, 2), Vector3(4, 1, 2), Vector3(2, 2, 2)]

static func run(root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var native := EditorInterface.get_base_control().get_parent()
	var undo := EditorInterface.get_editor_undo_redo()
	var history := undo.get_history_undo_redo(undo.get_object_history_id(root))
	var selection := EditorInterface.get_selection()
	var workspace := root.get_node("Workspace")
	EditorInterface.set_main_screen_editor("3D")
	await _frames(3)

	# Placement and the ribbon's Part menu.
	_part(workspace, root, "QAPlatform", TARGET - Vector3(0, 1, 0), Vector3(60, 2, 60))
	if not _expect(await _look_at(TARGET, 40.0, 0.7, 0.3), "Editor camera aims at the placement fixture", report):
		return report
	var last: Node3D
	for shape in SIZES.size():
		selection.clear()
		native.emit_signal("rome_editor_action", "insert_part", shape)
		var inserted := selection.get_selected_nodes()
		if not _expect(inserted.size() == 1 and inserted[0].is_class("Part") and inserted[0].get_parent() == workspace, "insert_part %d inserts a Part in Workspace" % shape, report):
			return report
		last = inserted[0]
		var size: Vector3 = last.get("Size")
		_expect(int(last.get("Shape")) == shape and size.is_equal_approx(SIZES[shape]), "insert_part %d uses its shape and Studio size" % shape, report)
		_expect(last.global_position.distance_to(TARGET + Vector3(0, size.y * 0.5, 0)) < 0.05, "Shape %d rests on the surface at the center of the view" % shape, report)
		if shape < SIZES.size() - 1:
			await _undo(history)
			_expect(last.get_parent() == null, "insert_part %d undo removes the Part" % shape, report)
	var placed := last.global_position
	await _undo(history)
	history.redo()
	_expect(last.get_parent() == workspace and last.global_position.is_equal_approx(placed), "insert_part redo restores the Part in place", report)
	await _undo(history)
	selection.clear()
	native.emit_signal("rome_insert_object", "SpawnLocation")
	var spawn := _selected(selection)
	_expect(spawn != null and spawn.is_class("SpawnLocation") and bool(spawn.get("Anchored")) and spawn.global_position.distance_to(TARGET + Vector3(0, 0.5, 0)) < 0.05, "SpawnLocation is anchored and rests on the surface", report)
	await _undo(history)
	selection.clear()
	native.emit_signal("rome_insert_object", "RBXModel")
	var model := _selected(selection)
	_expect(model != null and model.is_class("RBXModel") and model.name == "Model" and model.global_position.distance_to(TARGET) < 0.05, "A new Model is placed at the surface under the view center", report)
	await _undo(history)
	if _expect(await _look_at(Vector3(900, 10, 900), 30.0, 0.0, PI / 2), "Editor camera aims at empty space", report):
		var ray := Placement.view_ray()
		selection.clear()
		native.emit_signal("rome_editor_action", "insert_part", 0)
		var floating := _selected(selection) as Node3D
		var ahead: Vector3 = ray.origin + ray.direction * Placement.FALLBACK_DISTANCE
		_expect(floating != null and floating.global_position.distance_to(Vector3(ahead.x, 0.5, ahead.z)) < 0.05, "Without a surface the Part lands on y = 0 about 20 studs ahead", report)
		await _undo(history)

	# Folder grouping, locking, anchoring and materials.
	var first := _part(workspace, root, "QAFirst", Vector3(40, 3, 40), Vector3(4, 1, 2))
	var second := _part(workspace, root, "QASecond", Vector3(44, 5, 40), Vector3(2, 2, 2))
	var first_index := first.get_index()
	var second_index := second.get_index()
	var first_global := first.global_transform
	_select(selection, [first, second])
	native.emit_signal("rome_editor_action", "group_folder", null)
	var folder := first.get_parent()
	_expect(folder != null and folder.is_class("Folder") and second.get_parent() == folder and folder.get_parent() == workspace and str(folder.name).begins_with("Folder"), "group_folder puts the selection in a new Folder", report)
	_expect(first.global_transform.is_equal_approx(first_global), "group_folder keeps global transforms", report)
	await _undo(history)
	_expect(first.get_parent() == workspace and second.get_parent() == workspace and first.get_index() == first_index and second.get_index() == second_index and folder.get_parent() == null, "group_folder undo restores parents and order", report)
	history.redo()
	_expect(first.get_parent() == folder and second.get_parent() == folder and folder.get_parent() == workspace, "group_folder redo groups again", report)
	await _undo(history)
	_select(selection, [first, second])
	native.emit_signal("rome_editor_action", "lock_set", true)
	_expect(bool(first.get("Locked")) and bool(second.get("Locked")), "lock_set true locks the selection", report)
	native.emit_signal("rome_editor_action", "lock_set", false)
	_expect(not bool(first.get("Locked")) and not bool(second.get("Locked")), "lock_set false unlocks the selection", report)
	await _undo(history)
	_expect(bool(first.get("Locked")) and bool(second.get("Locked")), "lock_set undo restores Locked", report)
	native.emit_signal("rome_editor_action", "anchor_set", false)
	_expect(not bool(first.get("Anchored")) and not bool(second.get("Anchored")), "anchor_set false unanchors the selection", report)
	await _undo(history)
	_expect(bool(first.get("Anchored")) and bool(second.get("Anchored")), "anchor_set undo restores Anchored", report)
	_select(selection, [first])
	native.emit_signal("rome_editor_action", "insert_material", 1)
	_expect(int(first.get("Material")) == 3, "insert_material applies the ribbon material (Wood)", report)
	await _undo(history)
	_expect(int(first.get("Material")) == 0, "insert_material undo restores the material", report)

	# Ungroup keeps global transforms, names and order through Undo/Redo.
	_part(workspace, root, "QAConflict", Vector3(-40, 3, 40), Vector3(1, 1, 1))
	var container: Node3D = ClassDB.instantiate("RBXModel")
	container.name = "QAUngroupModel"
	container.transform = Transform3D(Basis(Vector3.UP, deg_to_rad(30)), Vector3(10, 5, -20))
	workspace.add_child(container)
	container.owner = root
	var child_a := _part(container, root, "QAConflict", Vector3(2, 0, 0), Vector3(1, 1, 1))
	var child_b := _part(container, root, "QAChildB", Vector3(-2, 1, 0), Vector3(1, 2, 1))
	child_b.rotation = Vector3(0, 0, deg_to_rad(15))
	var inner: Node = ClassDB.instantiate("Folder")
	inner.name = "QAInnerFolder"
	container.add_child(inner)
	inner.owner = root
	var container_index := container.get_index()
	var global_a := child_a.global_transform
	var global_b := child_b.global_transform
	var local_b := child_b.transform
	_select(selection, [container])
	native.emit_signal("rome_editor_action", "ungroup", null)
	var ungrouped := child_a.get_parent() == workspace and child_b.get_parent() == workspace and inner.get_parent() == workspace and container.get_parent() == null
	_expect(ungrouped, "ungroup moves the children to the parent and removes the Model", report)
	_expect(child_a.global_transform.is_equal_approx(global_a) and child_b.global_transform.is_equal_approx(global_b), "ungroup keeps global transforms", report)
	_expect(str(child_a.name) == "QAConflict2" and child_a.get_index() == container_index and inner.get_index() == container_index + 2, "ungroup uses unique names and the Model's place", report)
	await _undo(history)
	_expect(container.get_parent() == workspace and container.get_index() == container_index and child_a.get_parent() == container and inner.get_parent() == container, "ungroup undo restores the Model and its children", report)
	_expect(str(child_a.name) == "QAConflict" and child_b.transform.is_equal_approx(local_b) and child_a.global_transform.is_equal_approx(global_a), "ungroup undo restores names and transforms", report)
	history.redo()
	_expect(child_b.get_parent() == workspace and child_b.global_transform.is_equal_approx(global_b) and str(child_a.name) == "QAConflict2" and container.get_parent() == null, "ungroup redo matches the first ungroup", report)
	await _undo(history)
	_select(selection, [container])
	native.emit_signal("rome_editor_action", "lock_set", true)
	_expect(bool(child_a.get("Locked")) and bool(child_b.get("Locked")), "lock_set on a Model locks its Parts", report)
	await _undo(history)

	# Insert object window.
	var invalid := PackedStringArray()
	for title: String in Catalog.entries():
		if not ClassDB.can_instantiate(Catalog.entries()[title].type):
			invalid.append(title)
	_expect(invalid.is_empty() and Catalog.entries().size() >= 140, "Every catalog object maps to a GodotLuau class: " + str(invalid), report)
	selection.clear()
	native.emit_signal("rome_editor_action", "insert", null)
	await _frames(2)
	var dialog := EditorInterface.get_base_control().find_child("ROMEInsertObject", true, false)
	if not _expect(dialog != null and dialog.visible, "The toolbox opens Insertar objeto", report):
		return report
	var categories := PackedStringArray()
	for item in dialog.list.get_root().get_children():
		categories.append(item.get_text(0))
	var required := ["Básico", "Scripts", "Interfaz", "Luces", "Efectos", "Restricciones", "Valores", "Sonido", "Interacción", "Personajes"]
	var missing := required.filter(func(title: String) -> bool: return not categories.has(title))
	_expect(missing.is_empty(), "Insertar objeto groups objects by category: " + str(missing), report)
	_expect(dialog.select_object("RemoteEvent"), "Search finds RemoteEvent", report)
	dialog.choose()
	var remote := _selected(selection)
	_expect(remote != null and remote.is_class("RemoteEventNode") and remote.get_parent() == root.get_node("ReplicatedStorage") and not dialog.visible, "Choosing RemoteEvent inserts it into ReplicatedStorage", report)
	var recent: Array = EditorInterface.get_editor_settings().get_project_metadata("rome", "recent_objects", [])
	_expect(not recent.is_empty() and recent[0] == "RemoteEvent", "Recently used objects are remembered in the project", report)
	await _undo(history)
	_expect(remote != null and remote.get_parent() == null, "Insertar objeto supports Undo", report)
	dialog.open()
	var top: TreeItem = dialog.list.get_root().get_first_child()
	_expect(top != null and top.get_text(0) == "Usados recientemente" and top.get_first_child() != null and top.get_first_child().get_text(0) == "RemoteEvent", "Usados recientemente comes first", report)
	dialog.hide()
	selection.clear()
	native.emit_signal("rome_insert_object", "TextLabel")
	var label := _selected(selection)
	_expect(label != null and label.get_parent() != null and label.get_parent().is_class("ScreenGui") and label.get_parent().get_parent() == root.get_node("StarterGui"), "A GUI object without a GUI parent goes into a ScreenGui", report)
	await _undo(history)
	selection.clear()
	native.emit_signal("rome_insert_object", "Accessory")
	var accessory := _selected(selection)
	_expect(accessory != null and accessory.is_class("RBXModel") and accessory.get_meta("__rbx_class", "") == "Accessory", "The ribbon Accessory inserts a Roblox Accessory", report)
	await _undo(history)

	# Properties panel.
	_show_inspector()
	_select(selection, [first])
	EditorInterface.edit_node(first)
	await _frames(3)
	var panel := _panel()
	if not _expect(panel != null, "Part properties use the Studio panel", report):
		return report
	var titles: PackedStringArray = panel.section_titles()
	var expected := ["Datos", "Apariencia", "Colisión", "Comportamiento", "Pieza", "Transformar", "Atributos"]
	_expect(expected.all(func(title: String) -> bool: return titles.has(title)) and titles[0] == "Datos" and titles[-1] == "Atributos", "Properties are grouped in Studio sections: " + str(titles), report)
	_expect(panel.visible_rows("Datos") == PackedStringArray(["Name", "ClassName", "Parent"]), "Datos shows Name, ClassName and Parent", report)
	panel.commit("Transparency", 0.5)
	_expect(is_equal_approx(float(first.get("Transparency")), 0.5), "Editing a property changes the Part", report)
	await _undo(history)
	_expect(is_zero_approx(float(first.get("Transparency"))), "Property edits support Undo", report)
	history.redo()
	_expect(is_equal_approx(float(first.get("Transparency")), 0.5), "Property edits support Redo", report)
	await _undo(history)
	# The Inspector rebuilds the panel after edits; always ask for the current one.
	panel = _panel()
	_expect(panel != null and panel.add_attribute("Velocidad", "number").is_empty() and first.get_meta("Velocidad", null) is float, "Agregar atributo stores a GodotLuau number attribute", report)
	await _undo(history)
	_expect(not first.has_meta("Velocidad"), "Agregar atributo supports Undo", report)
	history.redo()
	_expect(first.has_meta("Velocidad"), "Agregar atributo supports Redo", report)
	await _frames(3)
	panel = _panel()
	_expect(panel != null and not panel.add_attribute("Mala palabra", "bool").is_empty() and not panel.add_attribute("_oculto", "bool").is_empty() and not panel.add_attribute("Velocidad", "string").is_empty(), "Invalid, reserved and repeated attribute names are rejected", report)
	_expect(panel != null and panel.visible_rows("Atributos").has("Velocidad"), "Attributes appear at the end of the panel", report)
	if panel != null:
		panel.commit("metadata/Velocidad", 12.5)
		_expect(is_equal_approx(float(first.get_meta("Velocidad", 0.0)), 12.5), "Attribute values are edited in place", report)
		await _undo(history)
		_expect(is_zero_approx(float(first.get_meta("Velocidad", -1.0))), "Attribute edits support Undo", report)
	var filter_box := _filter_box()
	if _expect(filter_box != null, "The property filter is available", report):
		filter_box.text = "Anch"
		filter_box.text_changed.emit("Anch")
		await _frames(3)
		panel = _panel()
		_expect(panel != null and panel.section_titles() == PackedStringArray(["Comportamiento"]) and panel.visible_rows("Comportamiento") == PackedStringArray(["Anchored"]), "The property filter keeps only matching rows", report)
		filter_box.text = ""
		filter_box.text_changed.emit("")
		await _frames(2)
	selection.clear()
	native.emit_signal("rome_insert_object", "ServerScript")
	await _frames(3)
	panel = _panel()
	var code_editor := false
	for property: EditorProperty in EditorInterface.get_inspector().find_children("*", "EditorProperty", true, false):
		code_editor = code_editor or property.get_edited_property() == &"codigo_luau"
	_expect(panel != null and panel.section_titles().has("Comportamiento") and panel.visible_rows("Comportamiento").has("Enabled") and code_editor, "Scripts show Studio properties and keep their code resource", report)
	await _undo(history)
	selection.clear()
	native.emit_signal("rome_insert_object", "PointLight")
	await _frames(3)
	panel = _panel()
	_expect(panel != null and panel.visible_rows("Apariencia").has("Brightness") and panel.visible_rows("Apariencia").has("Color"), "Lights show their properties under Apariencia", report)
	await _undo(history)

	# Leave the camera on the template's starting view.
	var editors := EditorInterface.get_base_control().find_children("*", "Node3DEditor", true, false)
	if not editors.is_empty():
		editors[0].call("rome_set_view_state", {"viewports": [{"position": Vector3(0, 1, 0), "distance": 32.7, "x_rotation": 0.409, "y_rotation": -0.6435}]})
	return report

static func _part(parent: Node, root: Node, title: String, position: Vector3, size: Vector3) -> Node3D:
	var part: Node3D = ClassDB.instantiate("Part")
	part.name = title
	part.set("Size", size)
	part.set("Anchored", true)
	part.position = position
	parent.add_child(part)
	part.owner = root
	return part

static func _look_at(target: Vector3, distance: float, x_rotation: float, y_rotation: float) -> bool:
	var editors := EditorInterface.get_base_control().find_children("*", "Node3DEditor", true, false)
	if editors.is_empty() or not editors[0].has_method("rome_set_view_state"):
		return false
	editors[0].call("rome_set_view_state", {"viewports": [{"position": target, "distance": distance, "x_rotation": x_rotation, "y_rotation": y_rotation, "orthogonal": false}]})
	var tree := EditorInterface.get_base_control().get_tree()
	for attempt in 240:
		await tree.process_frame
		var ray := Placement.view_ray()
		if ray.is_empty():
			continue
		var to_target: Vector3 = target - ray.origin
		var direction: Vector3 = ray.direction
		if absf(to_target.length() - distance) < 0.05 and (to_target - direction * to_target.dot(direction)).length() < 0.01:
			return true
	return false

static func _select(selection: EditorSelection, nodes: Array) -> void:
	selection.clear()
	for node in nodes:
		selection.add_node(node)

static func _selected(selection: EditorSelection) -> Node:
	var nodes := selection.get_selected_nodes()
	return nodes[0] if nodes.size() == 1 else null

static func _panel() -> Node:
	return EditorInterface.get_inspector().find_child("ROMEStudioProperties", true, false)

static func _filter_box() -> LineEdit:
	var dock := EditorInterface.get_inspector().get_parent().get_parent()
	for row in dock.get_children():
		if row is HBoxContainer:
			for child in row.get_children():
				if child is LineEdit:
					return child
	return null

static func _show_inspector() -> void:
	var node: Node = EditorInterface.get_inspector()
	while node != null and not node is EditorDock:
		node = node.get_parent()
	if node != null:
		node.make_visible()

# Lets the editor drop the undone selection before the next action frees it.
static func _undo(history: UndoRedo) -> void:
	history.undo()
	await _frames(2)

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(label)
	else:
		report.errors.append(label)
	return value

static func _frames(count: int) -> void:
	var tree := EditorInterface.get_base_control().get_tree()
	for frame in count:
		await tree.process_frame
