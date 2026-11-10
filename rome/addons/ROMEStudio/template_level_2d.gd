extends Node2D
## Readable template gameplay: collectibles, checkpoints, moving platforms and retry.
@export var mixed := false
var world: Node2D
var spawn := Vector2(200, 200)
var collected := 0
var total := 0
var elapsed := 0.0
var player: CharacterBody2D
var hud: Label
var paused_players := {}
var minigame: Control
var activated := false

func _ready() -> void:
	world = get_parent() as Node2D
	if mixed:
		call_deferred("_mixed_view")
	else:
		call_deferred("_start")

func _start() -> void:
	player = world.get_node_or_null("LocalPlayer") as CharacterBody2D
	if player == null:
		player = CharacterBody2D.new()
		player.name = "TemplatePlayer"
		player.set_script(preload("template_player_2d.gd"))
		var collision := CollisionShape2D.new()
		var shape := CapsuleShape2D.new()
		shape.radius = 12
		shape.height = 48
		collision.shape = shape
		collision.position.y = -24
		player.add_child(collision)
		world.add_child(player)
		player.position = spawn
		var camera := Camera2D.new()
		camera.position.y = -90
		camera.position_smoothing_enabled = true
		player.add_child(camera)
	var old_visual := player.get_node_or_null("BodyRect") as CanvasItem
	if old_visual != null:
		old_visual.hide()
	var visual := Polygon2D.new()
	visual.polygon = PackedVector2Array([Vector2(-14, -48), Vector2(14, -48), Vector2(14, -4), Vector2(9, 0), Vector2(-9, 0), Vector2(-14, -4)])
	visual.color = Color("ffc66d")
	player.add_child(visual)
	var visor := Polygon2D.new()
	visor.polygon = PackedVector2Array([Vector2(-10, -39), Vector2(10, -39), Vector2(10, -29), Vector2(-10, -29)])
	visor.color = Color("15304b")
	player.add_child(visor)
	var layer := CanvasLayer.new()
	add_child(layer)
	hud = Label.new()
	hud.position = Vector2(20, 16)
	hud.add_theme_font_size_override("font_size", 18)
	layer.add_child(hud)
	for area in world.find_children("*", "Area2D", true, false):
		var kind := str(area.get_meta("template_action", ""))
		if kind == "coin":
			total += 1
		area.body_entered.connect(_trigger.bind(area, kind))
	_update_hud()

func _trigger(body: Node2D, area: Area2D, kind: String) -> void:
	if body != player:
		return
	match kind:
		"coin":
			if area.visible:
				collected += 1
				area.hide()
		"hazard":
			call_deferred("_respawn")
		"checkpoint":
			spawn = area.position + Vector2(0, -30)
			area.modulate = Color("7ee8b7")
		"finish":
			hud.text = "META · %d / %d cristales · %.1f s · R para volver" % [collected, total, elapsed]
			return
	_update_hud()

func _update_hud() -> void:
	hud.text = "%d / %d cristales    A/D · mover    Espacio · saltar    R · reintentar%s" % [collected, total, "    F2 · volver a 3D" if mixed else ""]

func _respawn() -> void:
	if is_instance_valid(player):
		player.position = spawn
		player.velocity = Vector2.ZERO

func _physics_process(delta: float) -> void:
	if not is_instance_valid(player) or (mixed and not activated):
		return
	elapsed += delta
	for platform in world.get_children():
		if platform is AnimatableBody2D and platform.has_meta("motion_origin"):
			platform.position = platform.get_meta("motion_origin") + Vector2(sin(elapsed * 1.2) * 65, 0)
	if player.position.y > 650:
		_respawn()

func _unhandled_key_input(event: InputEvent) -> void:
	if not event is InputEventKey or not event.pressed or event.echo:
		return
	if event.keycode == KEY_R and (not mixed or activated):
		_respawn()

func _mixed_view() -> void:
	var root := get_tree().current_scene
	var layer := CanvasLayer.new()
	layer.name = "Mixed2DView"
	root.add_child(layer)
	var hint := Label.new()
	hint.text = "F2 · abrir / cerrar el circuito 2D"
	hint.position = Vector2(24, 70)
	layer.add_child(hint)
	minigame = SubViewportContainer.new()
	minigame.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	minigame.stretch = true
	layer.add_child(minigame)
	var viewport := SubViewport.new()
	viewport.size = Vector2i(1152, 648)
	viewport.disable_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	minigame.add_child(viewport)
	world.reparent(viewport)
	world.position = Vector2.ZERO
	_start()
	minigame.hide()
	player.set_physics_process(false)
	# The 2D world receives keys only while its viewport is visible. Listen on
	# the outer viewport as well so F2 can open it from the 3D game.
	var toggle := Node.new()
	toggle.set_script(preload("template_mixed_toggle.gd"))
	toggle.set("level", self)
	root.add_child(toggle)

func _set_active(value: bool) -> void:
	activated = value
	minigame.visible = value
	player.set_physics_process(value)
	if value:
		for character in get_tree().current_scene.find_children("*", "CharacterBody3D", true, false):
			paused_players[character] = character.is_physics_processing()
			character.set_physics_process(false)
	else:
		for character in paused_players:
			if is_instance_valid(character):
				character.set_physics_process(paused_players[character])
		paused_players.clear()
