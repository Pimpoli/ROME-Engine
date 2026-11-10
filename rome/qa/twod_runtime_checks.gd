extends SceneTree
## Run against a disposable scene saved by twod_template_bake.gd. Uses actual
## Character2D physics and key events, without changing GodotLuau or source code.

var _report := {"completed": false, "passed": false, "checks": [], "errors": []}

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var main := str(ProjectSettings.get_setting("application/run/main_scene", ""))
	if main.begins_with("uid://"):
		main = ResourceUID.get_id_path(ResourceUID.text_to_id(main))
	var packed := load(main) as PackedScene
	if not _expect(packed != null, "The baked main scene loads at runtime"):
		_finish()
		return
	var game := packed.instantiate()
	get_root().add_child(game)
	current_scene = game
	var dimension := str(game.get_meta("rome_template", ""))
	_report["dimension"] = dimension
	await _physics_frames(120)
	if dimension == "mixed":
		_expect(game.is_class("RobloxGame3D"), "Mixed runtime keeps its single 3D Game root")
		var workspace := game.get_node_or_null("Workspace")
		var world := game.find_child("World2D", true, false)
		_expect(workspace is Node3D and world is Node2D and world.get_class() == "Node2D", "Native 2D authoring content coexists with the 3D workspace")
		_expect(_count_class(game, "RobloxGame2D") == 0 and _count_class(game, "RobloxWorkspace2D") == 0, "Mixed runtime does not start a second 2D player system")
		_expect(world.get_node_or_null("Platform1/CollisionShape2D") is CollisionShape2D, "Mixed runtime retains native 2D physics content")
		_expect(world.get_viewport() is SubViewport, "Mixed 2D play uses its own viewport without replacing the 3D camera")
		_key(KEY_F2, true)
		_key(KEY_F2, false)
		await _physics_frames(3)
		_expect(world.get_node("TemplateGameplay").activated and world.get_node("TemplateGameplay").player.is_physics_processing(), "F2 activates the mixed template's playable 2D circuit")
		await _level_checks(world)
		_key(KEY_F2, true)
		_key(KEY_F2, false)
		await _physics_frames(3)
		_expect(not world.get_node("TemplateGameplay").activated and not world.get_node("TemplateGameplay").player.is_physics_processing(), "F2 returns to 3D and suspends the 2D player")
		_finish()
		return
	if not _expect(dimension == "2d" and game.is_class("RobloxGame2D"), "Pure 2D runtime uses Game2D"):
		_finish()
		return
	var workspace := game.get_node_or_null("Workspace")
	var player := game.get_node_or_null("Workspace/LocalPlayer") as CharacterBody2D
	if not _expect(player != null and player.is_class("Character2D"), "Workspace creates its actual Character2D player"):
		_finish()
		return
	_expect(player.get_node_or_null("Humanoid") != null and player.get_node("Humanoid").is_class("Humanoid2D"), "The 2D character retains Humanoid2D")
	var camera := player.get_node_or_null("Camera2D") as Camera2D
	_expect(camera != null and camera.enabled, "The character creates an enabled 2D camera")
	_expect(camera != null and get_root().get_camera_2d() == camera, "The runtime viewport uses the character's 2D camera")
	var floor := workspace.get_node_or_null("BasePlate") as StaticBody2D
	_expect(floor != null and floor.find_children("*", "CollisionShape2D", true, false).any(func(node: CollisionShape2D) -> bool: return node.shape != null and not node.disabled), "The baked floor has active 2D collision")
	_expect(player.is_on_floor(), "Gravity settles Character2D on the baked floor")
	var origin := player.global_position
	_key(KEY_D, true)
	await _physics_frames(24)
	_key(KEY_D, false)
	await _physics_frames(6)
	_expect(player.global_position.x > origin.x + 20.0, "A real D key event moves Character2D to the right")
	_expect(player.is_on_floor(), "Horizontal movement retains floor collision")
	var grounded_y := player.global_position.y
	_key(KEY_SPACE, true)
	await _physics_frames(5)
	_key(KEY_SPACE, false)
	_expect(player.global_position.y < grounded_y - 5.0 and player.velocity.y < 0.0, "A real Space key event makes Character2D jump")
	await _physics_frames(120)
	_expect(player.is_on_floor() and absf(player.global_position.y - grounded_y) < 2.0, "The jump lands back on the collision floor")
	await _level_checks(workspace)
	_expect(_count_class(game, "RobloxGame3D") == 0 and _count_class(game, "RobloxWorkspace") == 0, "Pure 2D runtime does not create a 3D world")
	_finish()

func _key(code: Key, pressed: bool) -> void:
	var event := InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = pressed
	Input.parse_input_event(event)

func _physics_frames(count: int) -> void:
	for frame in count:
		await physics_frame

func _count_class(node: Node, type: String) -> int:
	var count := 1 if node.is_class(type) else 0
	for child in node.get_children():
		count += _count_class(child, type)
	return count

func _expect(value: bool, label: String) -> bool:
	if value:
		_report.checks.append(label)
	else:
		_report.errors.append(label)
	return value

func _finish() -> void:
	_key(KEY_D, false)
	_key(KEY_SPACE, false)
	_report.completed = true
	_report.passed = _report.errors.is_empty()
	var output := FileAccess.open("res://twod-runtime-result.json", FileAccess.WRITE)
	if output != null:
		output.store_string(JSON.stringify(_report, "  "))
		output.close()
	print("ROME 2D runtime: ", _report.dimension if _report.has("dimension") else "unavailable", ", passed=", _report.passed, ", checks=", _report.checks.size(), ", errors=", _report.errors)
	quit(0 if _report.passed else 1)

func _level_checks(world: Node2D) -> void:
	var moving := world.get_node("MovingBridge") as AnimatableBody2D
	var before := moving.position
	await _physics_frames(20)
	_expect(moving.position.distance_to(before) > 1, "The 2D bridge moves during physics frames")
	var player: CharacterBody2D = world.get_node("TemplateGameplay").player
	var coin: Area2D = world.get_node("Crystal1")
	player.position = coin.position
	player.velocity = Vector2.ZERO
	await _physics_frames(3)
	_expect(world.get_node("TemplateGameplay").collected == 1 and not coin.visible, "Touching a crystal updates the real collectible counter")
	var checkpoint: Area2D = world.get_node("Checkpoint")
	player.position = checkpoint.position
	player.velocity = Vector2.ZERO
	await _physics_frames(3)
	_expect(world.get_node("TemplateGameplay").spawn.x == checkpoint.position.x, "Touching the checkpoint changes the respawn location")
	player.position = Vector2(900, 700)
	await _physics_frames(3)
	_expect(absf(player.position.x - world.get_node("TemplateGameplay").spawn.x) < 1 and player.position.y < 200, "Falling out of the world returns the player to the checkpoint")
