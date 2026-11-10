@tool
extends EditorPlugin
## Enable only in a disposable new 2d/mixed project, after ROMEStudio.
## Waits for the real editor scaffolding, verifies it, saves the actual scene,
## and exits. It does not manufacture replacement services or starter content.

var _report := {"completed": false, "passed": false, "checks": [], "errors": []}

func _enter_tree() -> void:
	call_deferred("_run")

func _run() -> void:
	await get_tree().create_timer(0.2).timeout
	while EditorInterface.get_resource_filesystem().is_scanning():
		await get_tree().create_timer(0.1).timeout
	var main := _main_path()
	if EditorInterface.get_edited_scene_root() == null:
		EditorInterface.open_scene_from_path(main)
	var game: Node
	for attempt in 300:
		game = EditorInterface.get_edited_scene_root()
		if game != null and game.get_meta("rome_template_built", false) and game.has_node("RunService"):
			break
		await get_tree().create_timer(0.05).timeout
	if not _expect(game != null and game.get_meta("rome_template_built", false), "ROMEStudio finishes the new template"):
		_finish()
		return
	await _frames(8)
	var dimension := str(game.get_meta("rome_template", ""))
	_report["dimension"] = dimension
	_report["main_scene"] = main
	var mixed := dimension == "mixed"
	_expect(dimension in ["2d", "mixed"], "Fixture uses a dedicated 2D or mixed template")
	_expect(game.is_class("RobloxGame3D") if mixed else game.is_class("RobloxGame2D"), "Template uses the matching language-extension game root")
	_expect(str(ProjectSettings.get_setting("rome/studio/dimension", "")) == dimension, "Project dimension matches the template")
	var native := EditorInterface.get_base_control().get_parent()
	_expect(str(native.call("rome_get_scene_view")) == ("3D" if mixed else "2D"), "The new game opens in its matching world editor")
	var view_button := native.find_child("ROMEWorkspace3D" if mixed else "ROMEWorkspace2D", true, false) as Button
	_expect(view_button != null and view_button.button_pressed, "The matching workspace is actually visible, not only its saved preference")
	for service in ["Workspace", "Players", "Lighting", "MaterialService", "ReplicatedFirst", "ReplicatedStorage", "ServerScriptService", "ServerStorage", "StarterGui", "StarterPack", "StarterPlayer", "Teams", "SoundService", "TextChatService", "RunService"]:
		_expect(game.has_node(NodePath(service)), "Template retains service " + service)
	var workspace := game.get_node_or_null("Workspace")
	if not _expect(workspace != null, "Template has its authored Workspace"):
		_finish()
		return
	var world := workspace.get_node_or_null("World2D") if mixed else workspace
	if _expect(world is Node2D, "Template has its 2D authoring world"):
		if mixed:
			_expect(workspace is Node3D and world.get_class() == "Node2D", "Mixed scene retains a 3D Workspace and ordinary World2D")
			_expect(not _contains_class(game, "RobloxWorkspace2D"), "Mixed content does not create a second player-spawning workspace")
			_expect(_class_count(game, "RobloxGame3D") == 1 and _class_count(game, "RobloxGame2D") == 0, "Mixed content has one Game root")
		else:
			_expect(workspace.is_class("RobloxWorkspace2D") and not workspace is Node3D, "Pure 2D keeps the language extension's 2D workspace")
			_expect(workspace.get_node_or_null("BasePlate") is StaticBody2D, "Pure 2D retains its physical starter floor")
			_expect(workspace.get_node_or_null("CurrentCamera") is Camera2D, "Pure 2D retains its editable Camera2D")
			_expect(not _contains_class(game, "SpawnLocation") and not _contains_class(game, "RobloxWorkspace"), "Pure 2D does not acquire 3D starter content")
		for index in 3:
			var platform := world.get_node_or_null("Platform%d" % (index + 1))
			_expect(platform is StaticBody2D, "Starter platform %d is a 2D static body" % (index + 1))
			if platform != null:
				var collision := platform.get_node_or_null("CollisionShape2D") as CollisionShape2D
				_expect(collision != null and collision.shape is RectangleShape2D and collision.shape.size.x > 0 and collision.shape.size.y > 0, "Starter platform %d has a usable collision shape" % (index + 1))
		_expect(world.get_node_or_null("Start2D") is Marker2D and world.get_node_or_null("Guide2D") is Label, "The 2D starter has an origin marker and controls guide")
	var unowned: Array[String] = []
	for node in game.find_children("*", "Node", true, false):
		# Part render meshes in the 3D extension are regenerated, not authored
		# scene data. In mixed games verify the new canvas subtree's ownership.
		if mixed and node != world and not world.is_ancestor_of(node):
			continue
		if node.owner != game:
			unowned.append(str(game.get_path_to(node)))
	_expect(unowned.is_empty(), "Every generated descendant survives scene packing: " + str(unowned))
	if not mixed:
		var factory_scripts: Array[Node] = []
		for node in game.find_children("*", "Node", true, false):
			if node.is_class("LocalScript") or node.is_class("ServerScript"):
				factory_scripts.append(node)
		_expect(not factory_scripts.is_empty(), "The original factory scripts remain available for development")
		_expect(factory_scripts.all(func(node: Node) -> bool: return not bool(node.get("Enabled"))), "Unfinished 2D factory LocalScript and ServerScript startup is disabled")
	while not EditorInterface.get_file_system_dock().call("rome_is_resource_operation_ready"):
		await get_tree().create_timer(0.1).timeout
	_expect(EditorInterface.save_scene() == OK, "The prepared main scene saves through the native editor")
	await _frames(3)
	var stored := ResourceLoader.load(main, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene
	_expect(stored != null, "The saved main scene reloads as a PackedScene")
	if stored != null:
		var reloaded := stored.instantiate()
		_expect(reloaded.get_node_or_null("Workspace/World2D/Platform3" if mixed else "Workspace/Platform3") is StaticBody2D, "The saved scene contains the authored 2D platforms")
		reloaded.free()
	_finish()

func _expect(value: bool, label: String) -> bool:
	if value:
		_report.checks.append(label)
	else:
		_report.errors.append(label)
	return value

func _finish() -> void:
	_report.completed = true
	_report.passed = _report.errors.is_empty()
	var output := FileAccess.open("res://twod-template-result.json", FileAccess.WRITE)
	if output != null:
		output.store_string(JSON.stringify(_report, "  "))
		output.close()
	print("ROME 2D template bake: ", _report.dimension if _report.has("dimension") else "unavailable", ", passed=", _report.passed, ", checks=", _report.checks.size(), ", errors=", _report.errors)
	await get_tree().create_timer(0.6).timeout
	get_tree().quit(0 if _report.passed else 1)

func _main_path() -> String:
	var path := str(ProjectSettings.get_setting("application/run/main_scene", ""))
	return ResourceUID.get_id_path(ResourceUID.text_to_id(path)) if path.begins_with("uid://") else path

func _contains_class(node: Node, type: String) -> bool:
	return _class_count(node, type) > 0

func _class_count(node: Node, type: String) -> int:
	var count := 1 if node.is_class(type) else 0
	for child in node.get_children():
		count += _class_count(child, type)
	return count

func _frames(count: int) -> void:
	for frame in count:
		await get_tree().process_frame
