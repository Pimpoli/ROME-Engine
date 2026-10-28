extends SceneTree

var checks: Array[String] = []
func check(value: bool, title: String) -> void:
	if not value:
		push_error("ROME Obby runtime: " + title)
		quit(1)
		return
	checks.append(title)
	print("ROME Obby runtime PASS: ", title)

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	change_scene_to_file(str(ProjectSettings.get_setting("application/run/main_scene")))
	await create_timer(4).timeout
	var world := current_scene
	check(world != null, "Scene loads")
	var players: Node = world.get_node("Players")
	var player: Node = players.call("get_local_player")
	check(player != null, "Player spawns")
	check(player.has_meta("ROMEObbyCheckpoint"), "Server script initializes progress")
	var mover: Node3D = world.get_node("Workspace/ClassicObby/Stage03/MovingPlatform01")
	var previous := mover.position
	await create_timer(0.3).timeout
	check(not mover.position.is_equal_approx(previous), "LocalScript moves platforms")
	var gui := player.find_child("ClassicObbyHUD", true, false)
	check(gui != null, "LocalScript builds progress HUD")
	var character: Node3D = player.call("get_character")
	var humanoid := character.get_node("Humanoid")
	check(float(humanoid.get("Health")) > 0, "Spawn is safe")
	var checkpoint: Node3D = world.get_node("Workspace/ClassicObby/Stage01/Checkpoint01")
	character.global_position = checkpoint.global_position + Vector3(0, 4, 0)
	await create_timer(0.8).timeout
	check(int(player.get_meta("ROMEObbyCheckpoint", 0)) == 1, "Server awards checkpoint on actual contact")
	var hazard: Node3D = world.get_node("Workspace/ClassicObby/Stage02/Laser01")
	character.global_position = hazard.global_position + Vector3(0, 3, 0)
	await create_timer(0.4).timeout
	check(float(humanoid.get("Health")) <= 0, "Server kills actual hazard contact")
	await create_timer(2).timeout
	character = player.call("get_character")
	humanoid = character.get_node("Humanoid")
	check(absf(character.global_position.z - checkpoint.global_position.z) < 5, "Server restores saved checkpoint after death")
	character.global_position = mover.global_position + Vector3(0, 4, 0)
	await create_timer(0.5).timeout
	var old_relative := character.global_position - mover.global_position
	await create_timer(0.9).timeout
	var new_relative := character.global_position - mover.global_position
	check((new_relative - old_relative).length() < 0.3, "Local moving platform carries a grounded passenger")
	var root_part: Node3D = character.get_node("HumanoidRootPart")
	root_part.global_position = Vector3(0, -15, 0)
	await create_timer(0.25).timeout
	check(float(humanoid.get("Health")) <= 0, "Server kills falls")
	await create_timer(2.5).timeout
	var new_character: Node3D = player.call("get_character")
	check(new_character != null and float(new_character.get_node("Humanoid").get("Health")) > 0, "Player respawns after server death")
	var file := FileAccess.open("res://runtime-result.json", FileAccess.WRITE)
	file.store_string(JSON.stringify({"checks": checks}, "  "))
	quit()
