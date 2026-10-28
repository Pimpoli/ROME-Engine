@tool
extends EditorPlugin

var _editor_node: Node
var _pending_root: Node
var _picker: ConfirmationDialog
var _picker_search: LineEdit
var _picker_list: ItemList
var _studio_inspector: EditorInspectorPlugin
var _view_initialized := false
var _resources: EditorDock
var _services := preload("res://addons/ROMEStudio/studio_services.gd").new()
var _script_files := preload("res://addons/ROMEStudio/studio_script_files.gd").new()

const OBJECTS := {
	"Part": "Part", "Model": "RBXModel", "Folder": "Folder", "SpawnLocation": "SpawnLocation",
	"Script": "ServerScript", "LocalScript": "LocalScript", "ModuleScript": "ModuleScript",
	"ScreenGui": "ScreenGui", "Frame": "Frame", "ScrollingFrame": "ScrollingFrame",
	"TextLabel": "TextLabel", "TextButton": "TextButton", "TextBox": "TextBox",
	"ImageLabel": "ImageLabel", "ImageButton": "ImageButton", "Humanoid": "Humanoid",
	"Character": "Character", "PointLight": "PointLight", "SpotLight": "SpotLight",
	"SurfaceLight": "SurfaceLight", "Sound": "Sound", "RemoteEvent": "RemoteEvent",
	"RemoteFunction": "RemoteFunction", "BindableEvent": "BindableEvent", "BindableFunction": "BindableFunction",
	"StringValue": "StringValue", "NumberValue": "NumberValue", "BoolValue": "BoolValue",
	"ObjectValue": "ObjectValue", "Vector3Value": "Vector3Value", "ParticleEmitter": "ParticleEmitter",
	"Beam": "Beam", "Trail": "Trail", "Highlight": "Highlight"
}

class StudioVectorProperty extends EditorProperty:
	var controls: Array[SpinBox] = []
	var updating := false
	var degrees := false
	func _init(as_degrees: bool = false) -> void:
		degrees = as_degrees
		var row := HBoxContainer.new()
		add_child(row)
		for axis in range(3):
			var number := SpinBox.new()
			number.min_value = -100000
			number.max_value = 100000
			number.step = 0.1
			number.allow_greater = true
			number.allow_lesser = true
			number.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			number.value_changed.connect(_changed)
			row.add_child(number)
			controls.append(number)
	func _changed(_value: float) -> void:
		if updating:
			return
		var value := Vector3(controls[0].value, controls[1].value, controls[2].value)
		if degrees:
			value *= PI / 180.0
		emit_changed(get_edited_property(), value)
	func _update_property() -> void:
		var value: Vector3 = get_edited_object().get(get_edited_property())
		if degrees:
			value *= 180.0 / PI
		updating = true
		for axis in range(3):
			controls[axis].value = value[axis]
		updating = false

class StudioInspector extends EditorInspectorPlugin:
	const PropertyGrid = preload("res://addons/ROMEStudio/studio_properties.gd")
	func _can_handle(object: Object) -> bool:
		return object is Node and (object.is_class("RobloxPart") or object.has_method("get_codigo_luau"))
	func _parse_property(object: Object, _type: Variant.Type, name: String, _hint: PropertyHint, _hint_string: String, _usage: int, _wide: bool) -> bool:
		if object.is_class("RobloxPart"):
			if name == "Size":
				add_property_editor_for_multiple_properties("", PackedStringArray(["name", "Color", "Material", "Size", "position", "rotation", "Anchored", "Locked", "CanCollide", "CanTouch", "CanQuery", "Massless", "Transparency", "Reflectance", "CastShadow"]), PropertyGrid.new(object))
			return true
		if name in ["position", "rotation", "scale"]:
			var property := StudioVectorProperty.new(name == "rotation")
			property.label = "Orientation" if name == "rotation" else name.capitalize()
			add_property_editor(name, property)
			return true
		if name == "codigo_luau":
			return false
		return name.is_empty() or name[0] != name[0].to_upper()

func _enter_tree() -> void:
	_editor_node = EditorInterface.get_base_control().get_parent()
	_resources = preload("res://addons/ROMEStudio/studio_resources.gd").new()
	_resources.set("plugin", self)
	add_dock(_resources)
	var editor_settings := EditorInterface.get_editor_settings()
	if not editor_settings.get_project_metadata("rome", "resources_initialized", false):
		_resources.call_deferred("close")
		editor_settings.set_project_metadata("rome", "resources_initialized", true)
	if _editor_node.has_signal("rome_insert_object"):
		_editor_node.connect("rome_insert_object", _insert_object)
	if _editor_node.has_signal("rome_editor_action"):
		_editor_node.connect("rome_editor_action", _editor_action)
	scene_changed.connect(_scene_changed)
	_studio_inspector = StudioInspector.new()
	add_inspector_plugin(_studio_inspector)
	call_deferred("_scene_changed", EditorInterface.get_edited_scene_root())
	get_tree().node_added.connect(_spawn_added)

func _exit_tree() -> void:
	_script_files.disconnect_watchers()
	_services.attach(null)
	if is_instance_valid(_resources):
		remove_dock(_resources)
		_resources.queue_free()
	if get_tree().node_added.is_connected(_spawn_added):
		get_tree().node_added.disconnect(_spawn_added)
	if _studio_inspector != null:
		remove_inspector_plugin(_studio_inspector)
	if is_instance_valid(_picker):
		_picker.queue_free()
	if is_instance_valid(_editor_node) and _editor_node.is_connected("rome_insert_object", _insert_object):
		_editor_node.disconnect("rome_insert_object", _insert_object)
	if is_instance_valid(_editor_node) and _editor_node.is_connected("rome_editor_action", _editor_action):
		_editor_node.disconnect("rome_editor_action", _editor_action)

func _scene_changed(root: Node) -> void:
	call_deferred("_organize_script_files", weakref(root) if root != null else null)
	if is_instance_valid(_resources):
		_resources.call_deferred("refresh")
	_services.attach(root)
	if root != null and root.is_class("RobloxGame3D"):
		call_deferred("_ensure_spawn_symbols", root)
	if root != null and root.is_class("RobloxGame3D") and not _view_initialized:
		_view_initialized = true
		call_deferred("_prepare_studio_view")
	if root == null or not root.has_meta("rome_template") or root.has_meta("rome_template_built"):
		return
	_pending_root = root
	call_deferred("_prepare_template")

func _prepare_studio_view() -> void:
	if DisplayServer.get_name() == "headless":
		return
	var settings := EditorInterface.get_editor_settings()
	if int(settings.get_project_metadata("rome", "initial_camera_revision", 0)) >= 4:
		return
	await get_tree().create_timer(0.8).timeout
	var root := EditorInterface.get_edited_scene_root()
	if root == null or not root.is_class("RobloxGame3D"):
		return
	var target := Vector3(0, 1, 0)
	var camera_position := Vector3(18, 14, 24)
	if root.get_meta("rome_template", "") == "empty":
		target = Vector3(0, 8, 0)
		camera_position = Vector3(0, 8, 20)
	elif root.get_meta("rome_template", "") == "obby":
		target = Vector3(0, 7, -18)
		camera_position = Vector3(24, 23, 30)
	var offset := camera_position - target
	var view_state := {"grid": false, "gizmos": false, "transform_gizmo": true, "use_environment": false,
		"position": target, "distance": offset.length(), "x_rotation": atan2(offset.y, Vector2(offset.x, offset.z).length()),
		"y_rotation": -atan2(offset.x, offset.z), "orthogonal": false, "view_type": 0}
	var editors := EditorInterface.get_base_control().find_children("*", "Node3DEditor", true, false)
	if not editors.is_empty() and editors[0].has_method("rome_set_view_state"):
		editors[0].call("rome_set_view_state", {"show_grid": false, "show_origin": false, "viewports": [view_state]})
	settings.set_project_metadata("rome", "initial_camera_revision", 4)
	if root != null and root.has_node("Workspace"):
		root.get_node("Workspace").set_display_folded(true)
		# The scene tree caches rows; update their fold state for this first view.
		for scene_tree in EditorInterface.get_base_control().find_children("*", "SceneTreeEditor", true, false):
			for tree: Tree in scene_tree.find_children("*", "Tree", true, false):
				var item := tree.get_root()
				if item != null and str(item.get_text(0)) == str(root.name):
					item = item.get_first_child()
					while item != null:
						item.set_collapsed(true)
						item = item.get_next()

func _prepare_template() -> void:
	var root := _pending_root
	if not is_instance_valid(root):
		return
	var workspace := root.get_node_or_null("Workspace")
	if workspace == null:
		return
	var kind := str(root.get_meta("rome_template", "baseplate"))
	var floor_node := workspace.get_node_or_null("Baseplate")
	var sun := workspace.get_node_or_null("SunLight") as DirectionalLight3D
	if sun != null:
		sun.rotation_degrees = Vector3(-55, -25, 0)
		sun.shadow_enabled = true
	if kind != "empty" and workspace.get_node_or_null("SpawnLocation") == null and ClassDB.can_instantiate("SpawnLocation"):
		var spawn := ClassDB.instantiate("SpawnLocation") as Node3D
		spawn.name = "SpawnLocation"
		workspace.add_child(spawn)
		spawn.owner = root
		spawn.set("Size", Vector3(12, 1, 12))
		spawn.set("Anchored", true)
		spawn.position = Vector3(0, 0.6, 0)
	_ensure_spawn_symbols(root)
	if floor_node != null:
		if kind == "empty":
			workspace.remove_child(floor_node)
			floor_node.queue_free()
		elif kind == "terrain" or kind == "starting":
			floor_node.set("Color", Color("66894b"))
			floor_node.set("Texture", null)
		elif kind == "classic":
			floor_node.set("Color", Color("777777"))
			floor_node.set("TopSurface", 1)
	if kind == "obby":
		preload("res://addons/ROMEStudio/studio_obby.gd").new().build(root, workspace)
		_ensure_spawn_symbols(root)
	if kind == "starting":
		for i in range(4):
			var piece: Node3D = ClassDB.instantiate("Part")
			piece.name = "Building%d" % (i + 1)
			workspace.add_child(piece)
			piece.owner = root
			piece.set("Anchored", true)
			piece.set("Size", Vector3(14, 10 + i * 4, 14))
			piece.set("Color", Color("9b8060"))
			piece.position = Vector3((i % 2) * 35 - 18, (10 + i * 4) * 0.5, (i / 2) * 35 - 18)
	root.set_meta("rome_template_built", true)
	for service in root.get_children():
		service.set_display_folded(true)
	EditorInterface.mark_scene_as_unsaved()
	if DisplayServer.get_name() == "headless":
		var packed := PackedScene.new()
		packed.pack(root)
		ResourceSaver.save(packed, root.scene_file_path)
	else:
		EditorInterface.save_scene()

func _organize_script_files(reference: WeakRef) -> void:
	var root := reference.get_ref() as Node if reference != null else null
	if root != EditorInterface.get_edited_scene_root():
		return
	if _script_files.organize_scene(root):
		EditorInterface.mark_scene_as_unsaved()
		_resources.call_deferred("_request_rescan")
	if is_instance_valid(_resources):
		_resources.call_deferred("refresh")

func _adopt_game_script(reference: WeakRef, scene_reference: WeakRef) -> void:
	var node := reference.get_ref() as Node
	var root := scene_reference.get_ref() as Node
	if not is_instance_valid(node) or not is_instance_valid(root) or not root.is_ancestor_of(node):
		return
	if _script_files.adopt_node(node):
		EditorInterface.mark_scene_as_unsaved()
		if is_instance_valid(_resources):
			_resources.call_deferred("refresh")
			_resources.call_deferred("_request_rescan")

func _spawn_added(node: Node) -> void:
	if node.has_method("get_codigo_luau"):
		var scene := EditorInterface.get_edited_scene_root()
		if scene != null and scene.is_ancestor_of(node):
			call_deferred("_adopt_game_script", weakref(node), weakref(scene))
	if node.is_class("SpawnLocation"):
		var root := EditorInterface.get_edited_scene_root()
		if root != null and root.is_ancestor_of(node):
			call_deferred("_ensure_spawn_symbols", root)

func _ensure_spawn_symbols(root: Node) -> void:
	var artwork_path := "res://addons/ROMEStudio/assets/spawn_marker.png"
	if not ResourceLoader.exists(artwork_path):
		artwork_path = "res://GodotLuau/assets/textures/spawn_marker.png"
	if not is_instance_valid(root) or not root.is_class("RobloxGame3D") or not ResourceLoader.exists(artwork_path):
		return
	var changed := false
	for spawn in root.find_children("*", "SpawnLocation", true, false):
		var existing := spawn.get_node_or_null("SpawnSymbol")
		if existing != null:
			if existing.owner != root:
				existing.owner = root
				for artwork in existing.get_children():
					artwork.owner = root
				changed = true
			continue
		# A separate container prevents the Part renderer from adopting this mesh.
		var layer := Node3D.new()
		layer.name = "SpawnSymbol"
		spawn.add_child(layer)
		layer.owner = root
		var emblem := MeshInstance3D.new()
		emblem.name = "Artwork"
		var plane := PlaneMesh.new()
		plane.size = Vector2(10, 10)
		emblem.mesh = plane
		var material := StandardMaterial3D.new()
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
		material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
		material.cull_mode = BaseMaterial3D.CULL_DISABLED
		material.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
		material.albedo_texture = load(artwork_path)
		emblem.material_override = material
		emblem.position.y = 0.515
		emblem.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		layer.add_child(emblem)
		emblem.owner = root
		changed = true
	if changed:
		EditorInterface.mark_scene_as_unsaved()


func _has_property(object: Object, property_name: String) -> bool:
	for info in object.get_property_list():
		if info.name == property_name:
			return true
	return false

func _editor_action(action: String, payload: Variant) -> void:
	if action == "resources":
		_resources.make_visible()
		return
	if action in ["terrain", "avatar_settings"]:
		var root := EditorInterface.get_edited_scene_root()
		if root != null:
			var node := root.get_node_or_null("Workspace/Terrain") if action == "terrain" else root.get_node_or_null("StarterPlayer")
			if node != null:
				EditorInterface.get_selection().clear()
				EditorInterface.get_selection().add_node(node)
				EditorInterface.edit_node(node)
		return
	if action == "insert":
		_show_object_picker()
		return
	if action == "import":
		for plugin in _editor_node.get_children():
			if plugin.has_method("_pick_rbxl"):
				plugin.call("_pick_rbxl")
				return
		return
	var selected := EditorInterface.get_selection().get_selected_nodes()
	if selected.is_empty():
		return
	if action == "group":
		_group_selection(selected)
		return
	var property_name := {"anchor": "Anchored", "lock": "Locked", "color": "Color", "material": "Material"}.get(action, "") as String
	if property_name.is_empty():
		return
	var undo := get_undo_redo()
	undo.create_action("Set " + property_name, UndoRedo.MERGE_ENDS if action in ["color", "material"] else UndoRedo.MERGE_DISABLE)
	var target: Variant = payload
	if action in ["anchor", "lock"]:
		for object in selected:
			if _has_property(object, property_name):
				target = not bool(object.get(property_name))
				break
	if action == "material":
		target = [0, 3, 10, 7, 16, 2, 14][int(payload)]
	for object in selected:
		if _has_property(object, property_name):
			undo.add_do_property(object, property_name, target)
			undo.add_undo_property(object, property_name, object.get(property_name))
	undo.commit_action()
	for object in selected:
		object.notify_property_list_changed()
	EditorInterface.mark_scene_as_unsaved()

func _show_object_picker() -> void:
	if _picker == null:
		_picker = ConfirmationDialog.new()
		_picker.title = "Insertar objeto"
		_picker.ok_button_text = "Insertar"
		EditorInterface.get_base_control().add_child(_picker)
		var layout := VBoxContainer.new()
		_picker.add_child(layout)
		_picker_search = LineEdit.new()
		_picker_search.placeholder_text = "Buscar objeto"
		_picker_search.text_changed.connect(_filter_objects)
		layout.add_child(_picker_search)
		_picker_list = ItemList.new()
		_picker_list.size_flags_vertical = Control.SIZE_EXPAND_FILL
		_picker_list.item_activated.connect(func(_index: int) -> void: _confirm_object())
		layout.add_child(_picker_list)
		_picker.confirmed.connect(_confirm_object)
	_picker_search.text = ""
	_filter_objects("")
	_picker.popup_centered(Vector2i(420, 520))
	_picker_search.grab_focus()

func _filter_objects(query: String) -> void:
	_picker_list.clear()
	for title: String in OBJECTS:
		var class_name_value: String = OBJECTS[title]
		if not ClassDB.can_instantiate(class_name_value) or not title.to_lower().contains(query.to_lower()):
			continue
		var icon_path := "res://GodotLuau/icons/" + title + ".svg"
		var icon: Texture2D = load(icon_path) if ResourceLoader.exists(icon_path) else EditorInterface.get_editor_theme().get_icon("Node", "EditorIcons")
		var index := _picker_list.add_item(title, icon)
		_picker_list.set_item_metadata(index, class_name_value)
	if _picker_list.item_count > 0:
		_picker_list.select(0)

func _confirm_object() -> void:
	var selected := _picker_list.get_selected_items()
	if selected.is_empty():
		return
	_insert_object(str(_picker_list.get_item_metadata(selected[0])))
	_picker.hide()

func _group_selection(selection: Array[Node]) -> void:
	var root := EditorInterface.get_edited_scene_root()
	var group := ClassDB.instantiate("RBXModel") as Node3D
	group.name = "Model"
	var parent := selection[0].get_parent()
	if parent == null or selection.has(root):
		group.free()
		return
	var nodes: Array[Node] = []
	var parents: Array[Node] = []
	for object in selection:
		var nested := false
		for other in selection:
			if other != object and other.is_ancestor_of(object):
				nested = true
		if not nested:
			nodes.append(object)
			parents.append(object.get_parent())
	var undo := get_undo_redo()
	undo.create_action("Group objects", UndoRedo.MERGE_DISABLE, root)
	undo.add_do_method(self, "_do_group", group, parent, nodes, root)
	undo.add_undo_method(self, "_undo_group", group, nodes, parents, root)
	undo.add_do_reference(group)
	undo.commit_action()
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(group)

func _do_group(group: Node, parent: Node, nodes: Array[Node], root: Node) -> void:
	parent.add_child(group, true)
	group.owner = root
	for object in nodes:
		object.reparent(group, true)
		object.owner = root

func _undo_group(group: Node, nodes: Array[Node], parents: Array[Node], root: Node) -> void:
	for i in nodes.size():
		nodes[i].reparent(parents[i], true)
		nodes[i].owner = root
	group.get_parent().remove_child(group)

func _find_service(root: Node, service_name: String) -> Node:
	return root.get_node_or_null(NodePath(service_name))

func _insert_object(type_name: String) -> void:
	var root := EditorInterface.get_edited_scene_root()
	if root == null or not ClassDB.can_instantiate(type_name):
		return
	var parent: Node = root
	var selection := EditorInterface.get_selection().get_selected_nodes()
	if not selection.is_empty():
		parent = selection[0]
	var world_types := ["Part", "SpawnLocation", "RBXMeshPart", "Character", "PointLight", "SpotLight", "Folder", "RBXModel"]
	if type_name in world_types and (parent == root or not parent is Node3D):
		parent = _find_service(root, "Workspace")
	elif type_name == "ServerScript" and (parent == root or parent.name == "Workspace"):
		parent = _find_service(root, "ServerScriptService")
	elif type_name == "LocalScript" and parent == root:
		parent = root.get_node_or_null("StarterPlayer/StarterPlayerScripts")
	elif type_name == "ModuleScript" and parent == root:
		parent = _find_service(root, "ReplicatedStorage")
	elif type_name == "ScreenGui":
		parent = _find_service(root, "StarterGui")
	if parent == null:
		parent = root
	var object := ClassDB.instantiate(type_name) as Node
	if object == null:
		return
	object.name = "Script" if type_name == "ServerScript" else type_name
	if type_name == "Part":
		object.set("Size", Vector3(4, 1.2, 2))
		object.set("position", Vector3(0, 4, 0))
	var undo := get_undo_redo()
	undo.create_action("Insert " + type_name)
	undo.add_do_method(parent, "add_child", object, true)
	undo.add_do_method(object, "set_owner", root)
	undo.add_do_reference(object)
	undo.add_undo_method(parent, "remove_child", object)
	undo.commit_action()
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(object)
	EditorInterface.edit_node(object)
	EditorInterface.mark_scene_as_unsaved()
