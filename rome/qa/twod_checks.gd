extends RefCounted
## Disposable editor fixture only. Creates one reusable canvas and one clean
## Luau document, then restores the original scene and existing documents.

static func run(root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var native := EditorInterface.get_base_control().get_parent()
	var candidates := native.find_children("*", "EditorSceneTabs", true, false)
	if not _expect(not candidates.is_empty() and native.has_method("rome_new_component_scene_with_dimension") and native.has_method("rome_get_scene_view"), "2D scene creation and view APIs are available", report):
		return report
	var tabs: Node = candidates[0]
	var original_documents: Array = tabs.call("rome_get_documents")
	var original_selection := EditorInterface.get_selection().get_selected_nodes()
	var main_path := root.scene_file_path
	var original_view := str(native.call("rome_get_scene_view"))
	var prefix := "TwoDQA" + str(Time.get_ticks_usec())
	var scene_path := "res://Scenes/" + prefix + ".tscn"
	var script_path := "res://Scripts/ModuleScript/" + prefix + ".lua"
	var button_2d := native.find_child("ROMEWorkspace2D", true, false) as Button
	var button_3d := native.find_child("ROMEWorkspace3D", true, false) as Button
	if not _expect(button_2d != null and button_3d != null, "Workspace offers explicit 2D and 3D view buttons", report):
		return report
	_compatible_root_checks(native, report)

	native.call("rome_new_component_scene_with_dimension", "2d")
	await _frames(5)
	var canvas := EditorInterface.get_edited_scene_root()
	if _expect(canvas is Node2D and canvas != root and not canvas.is_class("RobloxGame2D") and canvas.get_child_count() == 0, "Reusable 2D scene starts with a plain empty Node2D", report):
		_expect(str(native.call("rome_get_scene_view")) == "2D", "A new reusable canvas opens in the 2D workspace", report)
		await _canvas_objects(native, canvas, report)
		_expect(report.get("canvas_completed", false), "Canvas object checks completed without interruption", report)
		EditorInterface.save_scene_as(scene_path, false)
		await _frames(5)
		var packed := load(scene_path) as PackedScene
		if _expect(packed != null and packed.can_instantiate(), "The reusable 2D scene saves as a native PackedScene", report):
			var reloaded := packed.instantiate()
			_expect(reloaded is Node2D and reloaded.find_child("Sprite2D", true, false) != null and reloaded.find_child("CollisionShape2D", true, false) != null, "Saving and reloading retains the canvas sprite and collision children", report)
			reloaded.free()
		await _script_round_trip(native, tabs, canvas, script_path, report)
		_expect(report.get("script_completed", false), "Canvas script checks completed without interruption", report)
		button_3d.pressed.emit()
		await _frames(3)
		_expect(str(native.call("rome_get_scene_view")) == "3D" and EditorInterface.get_edited_scene_root() == canvas, "The 3D workspace can inspect the same 2D scene without changing its root", report)
		button_2d.pressed.emit()
		await _frames(3)
		_expect(str(native.call("rome_get_scene_view")) == "2D", "The 2D workspace button returns to the canvas", report)

	await _select_scene(tabs, main_path)
	if _expect(EditorInterface.get_edited_scene_root() == root, "The original main scene remains open after creating a canvas", report):
		await _mixed_objects(native, root, scene_path, report)
		_expect(report.get("mixed_completed", false), "Mixed object checks completed without interruption", report)
		await _composition_checks(native, root, prefix, report)
		_expect(report.get("composition_completed", false), "Viewport composition checks completed without interruption", report)
		await _select_scene(tabs, scene_path)
		_expect(EditorInterface.get_edited_scene_root() == canvas and str(native.call("rome_get_scene_view")) == "2D", "Selecting the reusable scene tab restores its 2D workspace", report)
		await _select_scene(tabs, main_path)
		_expect(EditorInterface.get_edited_scene_root() == root, "Selecting the main scene tab restores its own world", report)

	# Close only this suite's saved canvas and clean script. Existing scene/script
	# tabs and selection are retained; generated files remain for inspection.
	var script_index := _index(tabs.call("rome_get_documents"), "script", script_path)
	if script_index >= 0:
		tabs.call("rome_close_document", script_index)
	await _frames(3)
	var scene_index := _index(tabs.call("rome_get_documents"), "scene", scene_path)
	if scene_index >= 0:
		tabs.call("rome_select_document", scene_index)
		await _frames(3)
		EditorInterface.save_scene()
		await _frames(3)
		tabs.call("rome_close_document", scene_index)
	await _frames(4)
	_expect(_index(tabs.call("rome_get_documents"), "scene", scene_path) < 0, "The fixture's reusable canvas closes without leaving a dirty-document prompt", report)
	await _select_scene(tabs, main_path)
	(button_2d if original_view == "2D" else button_3d).pressed.emit()
	var selection := EditorInterface.get_selection()
	selection.clear()
	for node in original_selection:
		if is_instance_valid(node) and node.is_inside_tree():
			selection.add_node(node)
	await _frames(3)
	var final_documents: Array = tabs.call("rome_get_documents")
	var retained := true
	for document: Dictionary in original_documents:
		retained = retained and _index(final_documents, document.kind, document.path) >= 0
	_expect(retained, "2D foundation checks retain all previously open documents", report)
	report["completed"] = true
	return report

static func _canvas_objects(native: Node, canvas: Node, report: Dictionary) -> void:
	var undo := EditorInterface.get_editor_undo_redo()
	var history := undo.get_history_undo_redo(undo.get_object_history_id(canvas))
	var sprite := await _insert(native, "Sprite2D", canvas)
	if _expect(sprite is Sprite2D and sprite.get_parent() == canvas and sprite.owner == canvas, "Sprite2D inserts into the reusable canvas with saved ownership", report):
		history.undo()
		await _frames(3)
		_expect(sprite.get_parent() == null, "Canvas sprite insertion supports Undo", report)
		history.redo()
		await _frames(3)
		_expect(sprite.get_parent() == canvas and sprite.owner == canvas, "Canvas sprite insertion supports Redo without recreating the resource", report)
	var body := await _insert(native, "StaticBody2D", canvas)
	if _expect(body is StaticBody2D and body.get_parent() == canvas, "A native 2D physics body inserts into the canvas", report):
		var collision := await _insert(native, "CollisionShape2D", body)
		_expect(collision is CollisionShape2D and collision.get_parent() == body and collision.owner == canvas and collision.shape != null, "CollisionShape2D targets its body and supplies a serializable shape", report)
	var child_count := canvas.get_child_count()
	await _insert(native, "CollisionShape2D", canvas)
	_expect(canvas.get_child_count() == child_count, "A collision shape is rejected when its selected parent is not a physics body", report)
	var path := await _insert(native, "Path2D", canvas)
	if _expect(path is Path2D and path.get_parent() == canvas, "Path2D inserts into the canvas", report):
		var follow := await _insert(native, "PathFollow2D", path)
		_expect(follow is PathFollow2D and follow.get_parent() == path, "PathFollow2D targets the selected Path2D", report)
	child_count = canvas.get_child_count()
	await _insert(native, "PathFollow2D", canvas)
	_expect(canvas.get_child_count() == child_count, "A path follower is rejected outside Path2D", report)
	var camera := await _insert(native, "Camera2D", canvas)
	_expect(camera is Camera2D and camera.get_parent() == canvas and camera.owner == canvas, "Camera2D inserts as an editable canvas camera", report)
	report["canvas_completed"] = true

static func _script_round_trip(native: Node, tabs: Node, canvas: Node, script_path: String, report: Dictionary) -> void:
	var source := "-- ROME 2D document fixture\nlocal canvas_marker = 1\nreturn canvas_marker\n"
	DirAccess.make_dir_recursive_absolute(script_path.get_base_dir())
	var file := FileAccess.open(script_path, FileAccess.WRITE)
	if not _expect(file != null, "The canvas document fixture can create its own Luau file", report):
		return
	file.store_string(source)
	file.close()
	var script: Script = ClassDB.instantiate("LuauScript")
	script.source_code = source
	script.resource_name = script_path.get_file().get_basename()
	script.set_meta("rome_script_name", script.resource_name)
	script.set_meta("rome_script_type", "ModuleScript")
	script.take_over_path(script_path)
	EditorInterface.edit_resource(script)
	await _frames(5)
	var editor := EditorInterface.get_script_editor()
	_expect(editor.is_visible_in_tree() and editor.get_current_script() == script, "A Luau script opens beside the reusable 2D scene", report)
	_expect(str(native.call("rome_get_scene_view")) == "2D", "Opening code retains the scene's 2D world preference", report)
	var index := _index(tabs.call("rome_get_documents"), "script", script_path)
	if _expect(index >= 0, "The canvas script appears in the shared document bar", report):
		tabs.call("rome_close_document", index)
		await _frames(5)
		_expect(not editor.is_visible_in_tree() and EditorInterface.get_edited_scene_root() == canvas and str(native.call("rome_get_scene_view")) == "2D", "Closing the canvas script returns directly to its 2D world", report)
	report["script_completed"] = true

static func _mixed_objects(native: Node, root: Node, scene_path: String, report: Dictionary) -> void:
	var workspace := root.get_node_or_null("Workspace")
	if not _expect(workspace != null and root.is_class("RobloxGame3D"), "The mixed fixture retains one 3D DataModel and its Workspace", report):
		return
	var initial_world := workspace.get_node_or_null("World2D")
	var undo := EditorInterface.get_editor_undo_redo()
	var history := undo.get_history_undo_redo(undo.get_object_history_id(root))
	var sprite := await _insert(native, "Sprite2D", null)
	if _expect(sprite is Sprite2D and sprite.get_parent() is Node2D and workspace.is_ancestor_of(sprite), "A 2D sprite in a 3D game routes into a canvas branch below Workspace", report):
		var world := sprite.get_parent()
		_expect(not world.is_class("RobloxWorkspace2D") and not root.is_class("RobloxGame2D"), "Mixed content uses ordinary canvas nodes without a second DataModel or player service", report)
		history.undo()
		await _frames(3)
		_expect(not sprite.is_inside_tree() and (initial_world != null or world.get_parent() == null), "Undo removes the inserted sprite and a newly created canvas branch together", report)
		history.redo()
		await _frames(3)
		_expect(sprite.get_parent() == world and workspace.is_ancestor_of(sprite) and sprite.owner == root, "Mixed canvas insertion supports Redo and preserves ownership", report)
		history.undo()
		await _frames(3)
	var resources := native.find_child("ROMEAssets", true, false)
	if _expect(resources != null, "Resources is available for reusable 2D scenes", report):
		resources.call("refresh")
		var files := resources.find_child("ROMEAssetList", true, false) as Tree
		var row: TreeItem = resources.call("_find_item", files.get_root(), scene_path)
		if _expect(row != null, "Resources lists the saved reusable 2D scene", report):
			row.select(0)
			EditorInterface.get_selection().clear()
			resources.call("_insert_selected")
			await _frames(4)
			var instance := _selected()
			if _expect(instance is Node2D and instance.scene_file_path == scene_path and workspace.is_ancestor_of(instance), "Resources instances a reusable 2D scene inside a mixed game", report):
				_expect(instance.owner == root and instance.find_child("CollisionShape2D", true, false) != null, "The mixed scene instance retains its saved native collision hierarchy", report)
				history.undo()
				await _frames(3)
				_expect(not instance.is_inside_tree(), "Inserting a reusable canvas from Resources supports Undo", report)
	report["mixed_completed"] = true

static func _compatible_root_checks(native: Node, report: Dictionary) -> void:
	var resources := native.find_child("ROMEAssets", true, false)
	var plugin: Node = resources.get("plugin") if resources != null else null
	if not _expect(plugin != null and plugin.has_method("_destination"), "Compatible-root checks use the registered Studio insertion router", report):
		return
	var catalog = preload("res://addons/ROMEStudio/studio_catalog.gd")
	var body := CharacterBody2D.new()
	var collision: Dictionary = catalog.entry_for("CollisionShape2D")
	_expect(plugin.call("_destination", collision, body, body) == body, "A CharacterBody2D scene root is a valid collision parent", report)
	body.free()
	var path := Path2D.new()
	var follow: Dictionary = catalog.entry_for("PathFollow2D")
	_expect(plugin.call("_destination", follow, path, path) == path, "A Path2D scene root is a valid path-follower parent", report)
	path.free()
	var canvas := Node2D.new()
	_expect(plugin.call("_destination", collision, canvas, canvas) == null, "A plain Node2D scene root remains an invalid collision parent", report)
	canvas.free()

static func _composition_checks(native: Node, root: Node, prefix: String, report: Dictionary) -> void:
	var resources := native.find_child("ROMEAssets", true, false)
	var workspace := root.get_node_or_null("Workspace")
	if not _expect(resources != null and workspace != null, "Composition checks retain the main Workspace and Resources", report):
		return
	var undo := EditorInterface.get_editor_undo_redo()
	var history := undo.get_history_undo_redo(undo.get_object_history_id(root))
	var actions := 0
	var container := await _insert(native, "SubViewportContainer", root) as SubViewportContainer
	if not _expect(container != null, "A SubViewportContainer inserts into the active experience", report):
		return
	actions += 1
	_expect(container.owner == root, "The viewport container belongs to the edited scene", report)
	_expect(container.stretch and container.size.x > 0 and container.size.y > 0, "The viewport container starts with a visible size and stretch enabled", report)
	var viewport := await _insert(native, "SubViewport", container) as SubViewport
	if not _expect(viewport != null, "SubViewport inserts into the active experience", report):
		history.undo()
		await _frames(3)
		return
	actions += 1
	_expect(viewport.get_parent() == container and viewport.owner == root, "SubViewport inserts under the selected container", report)
	_expect(viewport.own_world_3d and viewport.transparent_bg and viewport.size == Vector2i(container.size), "A composed viewport has its own 3D world and follows the container size", report)
	var probe := SubViewport.new()
	preload("res://addons/ROMEStudio/studio_placement.gd").initialize_2d(probe)
	_expect(probe.size == Vector2i(640, 360) and probe.own_world_3d and probe.transparent_bg, "A standalone SubViewport starts at the configured 640 by 360 size", report)
	probe.free()
	var sprite := await _insert(native, "Sprite2D", viewport) as Sprite2D
	if sprite != null:
		actions += 1
	if _expect(sprite != null and sprite.get_parent() == viewport, "A sprite targets the selected SubViewport instead of the main Workspace", report):
		_expect(sprite.position.is_equal_approx(Vector2(viewport.size) * 0.5), "A direct viewport sprite is centered in its render target", report)
	var nested := await _insert(native, "Node2D", viewport) as Node2D
	if nested != null:
		actions += 1
	if _expect(nested != null and nested.get_parent() == viewport, "A nested canvas inserts inside the selected render target", report):
		nested.position = Vector2(55, 35)
		nested.rotation = 0.3
		nested.scale = Vector2(1.5, 0.8)
		viewport.global_canvas_transform = Transform2D(0.1, Vector2(15, -25))
		var nested_sprite := await _insert(native, "Sprite2D", nested) as Sprite2D
		if nested_sprite != null:
			actions += 1
		if _expect(nested_sprite != null and nested_sprite.get_parent() == nested, "A nested sprite keeps its selected local canvas parent", report):
			var rendered := viewport.global_canvas_transform * nested.global_transform * nested_sprite.position
			_expect(rendered.distance_to(Vector2(viewport.size) * 0.5) < 0.01, "Nested sprite placement respects local transforms and the viewport canvas transform", report)
	var model := Node3D.new()
	model.name = prefix + "Composed3D"
	var marker := Marker3D.new()
	marker.name = "ComposedMarker"
	model.add_child(marker)
	marker.owner = model
	var model_scene := PackedScene.new()
	var packed := model_scene.pack(model)
	model.free()
	var model_path := "res://Scenes/" + prefix + "Composed3D.tscn"
	if _expect(packed == OK and ResourceSaver.save(model_scene, model_path) == OK, "The composition fixture saves its own reusable 3D resource", report):
		var workspace_count := workspace.get_child_count()
		var instance := await _insert_resource(resources, model_path, viewport, report)
		if instance != null:
			actions += 1
		if _expect(instance is Node3D and instance.scene_file_path == model_path and instance.get_parent() == viewport, "A reusable 3D scene targets the selected SubViewport", report):
			_expect(instance.owner == root and instance.find_child("ComposedMarker", true, false) != null and workspace.get_child_count() == workspace_count, "The composed 3D instance preserves its hierarchy without changing the main Workspace", report)
	var collision := CollisionShape2D.new()
	collision.name = prefix + "PackedCollision"
	collision.shape = RectangleShape2D.new()
	var collision_scene := PackedScene.new()
	packed = collision_scene.pack(collision)
	collision.free()
	var collision_path := "res://Scenes/" + prefix + "PackedCollision.tscn"
	if _expect(packed == OK and ResourceSaver.save(collision_scene, collision_path) == OK, "The composition fixture saves a collision-root resource", report):
		var count := root.find_children("*", "CollisionShape2D", true, false).size()
		var inserted := await _insert_resource(resources, collision_path, nested if nested != null else viewport, report)
		if inserted != null:
			actions += 1
		_expect(inserted == null and root.find_children("*", "CollisionShape2D", true, false).size() == count, "Resources rejects a packed collision shape outside a physics parent", report)
	EditorInterface.get_selection().clear()
	for action in actions:
		history.undo()
		await _frames(3)
	_expect(not container.is_inside_tree(), "Composition checks undo only their own inserted viewport hierarchy", report)
	report["composition_completed"] = true

static func _insert_resource(resources: Node, path: String, parent: Node, report: Dictionary) -> Node:
	resources.call("refresh")
	var files := resources.find_child("ROMEAssetList", true, false) as Tree
	var row: TreeItem = resources.call("_find_item", files.get_root(), path)
	if not _expect(row != null, "Resources lists the composition fixture " + path.get_file(), report):
		return null
	row.select(0)
	var selection := EditorInterface.get_selection()
	selection.clear()
	selection.add_node(parent)
	resources.call("_insert_selected")
	await _frames(4)
	var selected := _selected()
	return selected if selected != parent else null

static func _insert(native: Node, type: String, parent: Node) -> Node:
	var selection := EditorInterface.get_selection()
	selection.clear()
	if parent != null:
		selection.add_node(parent)
	native.emit_signal("rome_insert_object", type)
	await _frames(3)
	var selected := _selected()
	return selected if selected != parent else null

static func _selected() -> Node:
	var selected := EditorInterface.get_selection().get_selected_nodes()
	return selected[0] if selected.size() == 1 else null

static func _select_scene(tabs: Node, path: String) -> void:
	var index := _index(tabs.call("rome_get_documents"), "scene", path)
	if index >= 0:
		tabs.call("rome_select_document", index)
	await _frames(5)

static func _index(documents: Array, kind: String, path: String) -> int:
	for index in documents.size():
		if documents[index].kind == kind and documents[index].path == path:
			return index
	return -1

static func _frames(count: int) -> void:
	var tree := EditorInterface.get_base_control().get_tree()
	for frame in count:
		await tree.process_frame

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(label)
	else:
		report.errors.append(label)
	return value
