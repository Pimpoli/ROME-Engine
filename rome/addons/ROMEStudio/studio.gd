@tool
extends EditorPlugin

const Catalog = preload("res://addons/ROMEStudio/studio_catalog.gd")
const Placement = preload("res://addons/ROMEStudio/studio_placement.gd")
const InsertDialog = preload("res://addons/ROMEStudio/studio_insert_dialog.gd")
const Services = preload("res://addons/ROMEStudio/studio_services.gd")
# Ribbon material menu order: Plastic, Wood, Metal, Concrete, Glass, Neon, Grass.
const MATERIALS := [0, 3, 10, 7, 16, 2, 14]
# Studio sizes for Block, Ball, Cylinder, Wedge, CornerWedge and Truss.
const PART_SIZES := [Vector3(4, 1, 2), Vector3(2, 2, 2), Vector3(2, 2, 2), Vector3(4, 1, 2), Vector3(2, 2, 2), Vector3(2, 2, 2)]
const STORAGE := ["ReplicatedStorage", "ServerStorage", "ReplicatedFirst", "StarterPack"]

var _editor_node: Node
var _pending_root: Node
var _picker: InsertDialog
var _studio_inspector: EditorInspectorPlugin
var _view_initialized := false
var _resources: EditorDock
var _services := Services.new()
var _script_files := preload("res://addons/ROMEStudio/studio_script_files.gd").new()

class StudioInspector extends EditorInspectorPlugin:
	const PropertyGrid = preload("res://addons/ROMEStudio/studio_properties.gd")
	var _shown: Dictionary = {}
	func _can_handle(object: Object) -> bool:
		var root := EditorInterface.get_edited_scene_root()
		if not ProjectSettings.get_setting("rome/studio/enabled", false) or not object is Node or root == null or not (object == root or root.is_ancestor_of(object)):
			return false
		if object.is_class("RobloxGame3D") or object.is_class("RobloxGame2D") or object.is_class("RobloxTerrain"):
			return false
		return ClassDB.class_get_api_type(object.get_class()) in [ClassDB.API_EXTENSION, ClassDB.API_EDITOR_EXTENSION]
	func _parse_begin(object: Object) -> void:
		var grid := PropertyGrid.new(object as Node, inspector_filter())
		_shown = grid.shown
		add_property_editor_for_multiple_properties("", PackedStringArray(_shown.keys()), grid)
	func _parse_property(_object: Object, _type: Variant.Type, name: String, _hint: PropertyHint, _hint_string: String, _usage: int, _wide: bool) -> bool:
		if name == "codigo_luau":
			return false
		if _shown.has(name) or name.begins_with("metadata/"):
			return true
		return name.is_empty() or name[0] != name[0].to_upper()
	## Text of the Inspector dock's property filter.
	static func inspector_filter() -> String:
		var inspector := EditorInterface.get_inspector()
		if inspector == null or inspector.get_parent() == null or inspector.get_parent().get_parent() == null:
			return ""
		for row in inspector.get_parent().get_parent().get_children():
			if row is HBoxContainer:
				for child in row.get_children():
					if child is LineEdit:
						return child.text
		return ""

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
	if action == "insert_part":
		var shape := int(payload) if payload is int or payload is float else 0
		_insert_entry(Catalog.entry_for("Part"), clampi(shape, 0, PART_SIZES.size() - 1))
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
	match action:
		"group":
			_group_selection(selected, "RBXModel", "Model")
		"group_folder":
			_group_selection(selected, "Folder", "Folder")
		"ungroup":
			_ungroup_selection(selected)
		"anchor", "lock":
			_set_on_selection(selected, "Anchored" if action == "anchor" else "Locked", null, true)
		"anchor_set", "lock_set":
			if payload is bool or payload is int:
				_set_on_selection(selected, "Anchored" if action == "anchor_set" else "Locked", bool(payload))
		"color":
			if payload is Color:
				_set_on_selection(selected, "Color", payload, false, true)
		"material", "insert_material":
			var index := int(payload) if payload is int or payload is float else -1
			if index >= 0 and index < MATERIALS.size():
				_set_on_selection(selected, "Material", MATERIALS[index], false, true)

## Applies to the selection and to the Parts inside selected Models, like Studio.
func _set_on_selection(selected: Array[Node], property_name: String, value: Variant, toggle := false, merge := false) -> void:
	var targets: Array[Node] = []
	var seen := {}
	for object in selected:
		var candidates: Array[Node] = [object]
		if not object.is_class("RobloxPart"):
			candidates.append_array(object.find_children("*", "RobloxPart", true, true))
		for node in candidates:
			if not seen.has(node) and property_name in node:
				seen[node] = true
				targets.append(node)
	if targets.is_empty():
		return
	if toggle:
		value = not bool(targets[0].get(property_name))
	var undo := get_undo_redo()
	undo.create_action("Cambiar " + property_name, UndoRedo.MERGE_ENDS if merge else UndoRedo.MERGE_DISABLE, EditorInterface.get_edited_scene_root())
	for object in targets:
		undo.add_do_property(object, property_name, value)
		undo.add_undo_property(object, property_name, object.get(property_name))
	for object in selected:
		undo.add_do_method(object, "notify_property_list_changed")
		undo.add_undo_method(object, "notify_property_list_changed")
	undo.commit_action()
	EditorInterface.mark_scene_as_unsaved()

func _show_object_picker() -> void:
	if _picker == null:
		_picker = InsertDialog.new()
		_picker.describe = _describe_destination
		_picker.object_chosen.connect(_insert_object)
		EditorInterface.get_base_control().add_child(_picker)
	_picker.open()

func _describe_destination(title: String) -> String:
	var root := EditorInterface.get_edited_scene_root()
	var entry := Catalog.entry_for(title)
	if root == null or entry.is_empty():
		return ""
	var selection := EditorInterface.get_selection().get_selected_nodes()
	var parent := _destination(entry, selection[0] if not selection.is_empty() else null, root)
	if parent == null and entry.kind == "gui":
		parent = _screen_gui(root)
		if parent == null:
			return "StarterGui (en un ScreenGui nuevo)"
	return str(parent.name) if parent != null else str(root.name)

func _insert_object(type_name: String) -> void:
	_insert_entry(Catalog.entry_for(type_name))

func _insert_entry(entry: Dictionary, shape := -1) -> Node:
	var root := EditorInterface.get_edited_scene_root()
	if root == null or entry.is_empty() or not ClassDB.can_instantiate(entry.type):
		return null
	var selection := EditorInterface.get_selection().get_selected_nodes()
	var parent := _destination(entry, selection[0] if not selection.is_empty() else null, root)
	var object := ClassDB.instantiate(entry.type) as Node
	if object == null:
		return null
	object.name = entry.name
	if not str(entry.rbx_class).is_empty():
		object.set_meta("__rbx_class", entry.rbx_class)
	if shape < 0:
		shape = entry.shape
	if object.is_class("SpawnLocation"):
		object.set("Size", Vector3(12, 1, 12))
		object.set("Anchored", true)
	elif object.is_class("Part"):
		object.set("Shape", maxi(shape, 0))
		object.set("Size", PART_SIZES[maxi(shape, 0)])
	var top := object
	if parent == null and entry.kind == "gui":
		parent = _screen_gui(root)
		if parent == null and root.has_node("StarterGui") and ClassDB.can_instantiate("ScreenGui"):
			top = ClassDB.instantiate("ScreenGui")
			top.name = "ScreenGui"
			top.add_child(object)
			parent = root.get_node("StarterGui")
	if parent == null:
		parent = root
	var workspace := root.get_node_or_null("Workspace")
	if top is Node3D and _placeable(top) and workspace != null and (parent == workspace or workspace.is_ancestor_of(parent)):
		Placement.place(top as Node3D, parent, root)
	var undo := get_undo_redo()
	undo.create_action("Insertar " + str(entry.name), UndoRedo.MERGE_DISABLE, root)
	undo.add_do_method(parent, "add_child", top, true)
	undo.add_do_method(top, "set_owner", root)
	if top != object:
		undo.add_do_method(object, "set_owner", root)
	undo.add_do_reference(top)
	undo.add_undo_method(parent, "remove_child", top)
	undo.commit_action()
	_select_nodes([object])
	EditorInterface.edit_node(object)
	EditorInterface.mark_scene_as_unsaved()
	InsertDialog.remember(str(entry.name))
	return object

func _placeable(node: Node) -> bool:
	return node.is_class("RobloxPart") or node.is_class("RobloxPlayer") or node.is_class("RobloxTool") or (node.is_class("RBXModel") and not node.is_class("WorldModel"))

func _screen_gui(root: Node) -> Node:
	var starter := root.get_node_or_null("StarterGui")
	if starter != null:
		for child in starter.get_children():
			if child.is_class("ScreenGui"):
				return child
	return null

## Studio's destination rules; null for a GUI object means "inside a ScreenGui".
func _destination(entry: Dictionary, selected: Node, root: Node) -> Node:
	var chosen: Node = selected if selected != null and selected != root and root.is_ancestor_of(selected) else null
	var type: String = entry.type
	match str(entry.kind):
		"world", "attach":
			if chosen != null and (chosen is Node3D or chosen.is_class("Folder") or (chosen.get_parent() == root and str(chosen.name) in STORAGE)):
				return chosen
			return root.get_node_or_null("Workspace")
		"script":
			if type == "ServerScript":
				return chosen if chosen != null and chosen.name != "Workspace" else root.get_node_or_null("ServerScriptService")
			if type == "LocalScript":
				return chosen if chosen != null else root.get_node_or_null("StarterPlayer/StarterPlayerScripts")
			return chosen if chosen != null else root.get_node_or_null("ReplicatedStorage")
		"screen_gui":
			return root.get_node_or_null("StarterGui")
		"gui":
			if chosen != null and (chosen is Control or chosen.is_class("ScreenGui") or chosen.is_class("BillboardGui") or chosen.is_class("SurfaceGui")):
				return chosen
			return null
		"gui_modifier":
			return chosen if chosen != null else root.get_node_or_null("StarterGui")
		"lighting":
			if chosen != null and (chosen.name == "Lighting" or chosen.is_class("Camera") or chosen is Camera3D):
				return chosen
			return root.get_node_or_null("Lighting")
		"terrain":
			var terrain := root.get_node_or_null("Workspace/Terrain")
			return terrain if terrain != null else root.get_node_or_null("Workspace")
		"sound_effect":
			if chosen != null and (chosen.is_class("RobloxSound") or chosen.is_class("RobloxSoundGroup") or chosen.name == "SoundService"):
				return chosen
			return root.get_node_or_null("SoundService")
		"sound_service":
			return chosen if chosen != null else root.get_node_or_null("SoundService")
		"remote":
			return chosen if chosen != null else root.get_node_or_null("ReplicatedStorage")
		"teams", "chat", "material":
			var service := root.get_node_or_null({"teams": "Teams", "chat": "TextChatService", "material": "MaterialService"}[entry.kind])
			if chosen != null and service != null and (chosen == service or service.is_ancestor_of(chosen)):
				return chosen
			return service
	return chosen if chosen != null else root.get_node_or_null("Workspace")

## Top-level selected nodes in tree order, without the root, services or nodes
## that belong to an instanced scene.
func _top_level(selection: Array[Node]) -> Array[Node]:
	var root := EditorInterface.get_edited_scene_root()
	var nodes: Array[Node] = []
	if root == null:
		return nodes
	for object in selection:
		if object == root or object.owner != root or not root.is_ancestor_of(object) or _is_service(object, root):
			continue
		var nested := false
		for other in selection:
			if other != object and other.is_ancestor_of(object):
				nested = true
		if not nested:
			nodes.append(object)
	nodes.sort_custom(func(a: Node, b: Node) -> bool: return b.is_greater_than(a))
	return nodes

func _is_service(node: Node, root: Node) -> bool:
	if node.get_parent() == root:
		return Services.SERVICES.has(str(node.name))
	return node.get_parent().get_parent() == root and node.get_parent().name == "StarterPlayer" and node.name in ["StarterCharacterScripts", "StarterPlayerScripts"]

func _group_selection(selection: Array[Node], container_type: String, title: String) -> void:
	var root := EditorInterface.get_edited_scene_root()
	var nodes := _top_level(selection)
	if nodes.is_empty() or not ClassDB.can_instantiate(container_type):
		return
	var parent := nodes[0].get_parent()
	var group := ClassDB.instantiate(container_type) as Node
	group.name = _unique_name(title, _child_names(parent))
	var inside := {}
	var moves := []
	var restores := []
	for i in nodes.size():
		moves.append(_move(nodes[i], group, i, _unique_name(str(nodes[i].name), inside)))
		restores.append(_move(nodes[i], nodes[i].get_parent(), nodes[i].get_index(), str(nodes[i].name)))
	var undo := get_undo_redo()
	undo.create_action("Agrupar en " + title, UndoRedo.MERGE_DISABLE, root)
	undo.add_do_method(self, "_attach", group, parent, nodes[0].get_index(), root)
	undo.add_do_method(self, "_reparent_moves", moves)
	undo.add_do_method(self, "_finish_moves", moves)
	undo.add_do_method(self, "_select_nodes", [group])
	undo.add_do_reference(group)
	undo.add_undo_method(self, "_reparent_moves", restores)
	undo.add_undo_method(self, "_detach", group)
	undo.add_undo_method(self, "_finish_moves", restores)
	undo.add_undo_method(self, "_select_nodes", nodes)
	undo.commit_action()
	EditorInterface.mark_scene_as_unsaved()

func _ungroup_selection(selection: Array[Node]) -> void:
	var root := EditorInterface.get_edited_scene_root()
	var containers: Array[Node] = []
	for node in _top_level(selection):
		if (node.is_class("RBXModel") or node.is_class("Folder")) and node.scene_file_path.is_empty():
			containers.append(node)
	if containers.is_empty():
		return
	# Later siblings first: the indices of earlier containers stay valid.
	containers.reverse()
	var names_by_parent := {}
	var steps := []
	var released := []
	for container in containers:
		var parent := container.get_parent()
		if not names_by_parent.has(parent):
			names_by_parent[parent] = _child_names(parent)
		var used: Dictionary = names_by_parent[parent]
		used.erase(str(container.name))
		var out := []
		var back := []
		for child in container.get_children():
			if child.owner != root:
				continue
			out.append(_move(child, parent, container.get_index() + out.size(), _unique_name(str(child.name), used)))
			back.append(_move(child, container, child.get_index(), str(child.name)))
			released.append(child)
		steps.append([container, parent, container.get_index(), out, back])
	var undo := get_undo_redo()
	undo.create_action("Desagrupar", UndoRedo.MERGE_DISABLE, root)
	for step: Array in steps:
		undo.add_do_method(self, "_reparent_moves", step[3])
		undo.add_do_method(self, "_detach", step[0])
		undo.add_do_method(self, "_finish_moves", step[3])
		undo.add_undo_reference(step[0])
	undo.add_do_method(self, "_select_nodes", released)
	for i in range(steps.size() - 1, -1, -1):
		undo.add_undo_method(self, "_attach", steps[i][0], steps[i][1], steps[i][2], root)
		undo.add_undo_method(self, "_reparent_moves", steps[i][4])
		undo.add_undo_method(self, "_finish_moves", steps[i][4])
	undo.add_undo_method(self, "_select_nodes", containers)
	undo.commit_action()
	EditorInterface.mark_scene_as_unsaved()

func _move(node: Node, parent: Node, index: int, title: String) -> Dictionary:
	var move := {"node": node, "parent": parent, "index": index, "name": title}
	if node is Node3D or node is Node2D:
		move["transform"] = node.global_transform
	return move

## Moves keep each node (and its ownership); the exact global transform is restored.
func _reparent_moves(moves: Array) -> void:
	for move: Dictionary in moves:
		var node: Node = move.node
		if node.get_parent() != move.parent:
			node.reparent(move.parent, true)
		# A temporary name avoids clashes while siblings are renamed.
		node.name = "_rome_move_%d" % node.get_instance_id()
		if move.has("transform"):
			node.global_transform = move.transform

func _finish_moves(moves: Array) -> void:
	for move: Dictionary in moves:
		var node: Node = move.node
		node.name = move.name
	var ordered := moves.duplicate()
	ordered.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return a.index < b.index)
	for move: Dictionary in ordered:
		var parent: Node = move.parent
		parent.move_child(move.node, clampi(move.index, 0, parent.get_child_count() - 1))

func _attach(node: Node, parent: Node, index: int, root: Node) -> void:
	parent.add_child(node)
	parent.move_child(node, clampi(index, 0, parent.get_child_count() - 1))
	node.owner = root

func _detach(node: Node) -> void:
	if node.get_parent() != null:
		node.get_parent().remove_child(node)

func _select_nodes(nodes: Array) -> void:
	var selection := EditorInterface.get_selection()
	selection.clear()
	for node in nodes:
		if is_instance_valid(node) and node.is_inside_tree():
			selection.add_node(node)

static func _unique_name(base: String, used: Dictionary) -> String:
	var candidate := base
	var suffix := 2
	while used.has(candidate):
		candidate = base + str(suffix)
		suffix += 1
	used[candidate] = true
	return candidate

static func _child_names(parent: Node) -> Dictionary:
	var names := {}
	for child in parent.get_children():
		names[str(child.name)] = true
	return names
