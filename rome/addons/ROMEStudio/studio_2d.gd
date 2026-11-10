@tool
extends RefCounted
## New-experience scaffolding only. The language extension remains unchanged.

static func build(root: Node, mixed: bool) -> void:
	var workspace := root.get_node_or_null("Workspace")
	if workspace == null:
		return
	var world: Node2D = workspace as Node2D
	if mixed:
		world = Node2D.new()
		world.name = "World2D"
		world.position = Vector2(180, 500)
		workspace.add_child(world)
		world.owner = root
	else:
		# Factory modules still assume 3D and missing platform/chat modules. Keep
		# their source available for the Luau team, without executing that startup.
		for node in root.find_children("*", "Node", true, false):
			if node.get_class() in ["LocalScript", "ServerScript"] and "Enabled" in node:
				node.set("Enabled", false)
		var ground := workspace.get_node_or_null("BasePlate/GroundVisual") as ColorRect
		var floor := workspace.get_node_or_null("BasePlate")
		if floor != null:
			floor.set_meta("_edit_group_", true)
		if ground != null:
			ground.color = Color("354a65")
			ground.mouse_filter = Control.MOUSE_FILTER_IGNORE
	if world == null:
		return
	for i in 3:
		var platform := StaticBody2D.new()
		platform.name = "Platform%d" % (i + 1)
		platform.set_meta("_edit_group_", true) # Pick the body and move its visual/collision together.
		platform.position = Vector2(380 + i * 180, 220 - i * 65) if not mixed else Vector2(i * 170, -i * 50)
		world.add_child(platform)
		platform.owner = root
		var collision := CollisionShape2D.new()
		collision.name = "CollisionShape2D"
		var shape := RectangleShape2D.new()
		shape.size = Vector2(140, 24)
		collision.shape = shape
		platform.add_child(collision)
		collision.owner = root
		var visual := Polygon2D.new()
		visual.name = "Visual"
		visual.polygon = PackedVector2Array([Vector2(-70, -12), Vector2(70, -12), Vector2(70, 12), Vector2(-70, 12)])
		visual.color = [Color("63c6ed"), Color("9a91ed"), Color("f6bf69")][i]
		platform.add_child(visual)
		visual.owner = root
	var origin := Marker2D.new()
	origin.name = "Start2D"
	origin.position = Vector2(200, 200) if not mixed else Vector2(0, -50)
	world.add_child(origin)
	origin.owner = root
	var label := Label.new()
	label.name = "Guide2D"
	label.text = "MUNDO 2D · Selecciona 2D para editar" if mixed else "ROME 2D · A/D o flechas para moverte · Espacio para saltar"
	label.position = Vector2(-65, -190) if mixed else Vector2(-80, 70)
	label.add_theme_color_override("font_color", Color("a9c9e9"))
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	world.add_child(label)
	label.owner = root
	_enrich(world, root, mixed)
	# The extension may have built descendants before the editor's root was
	# assigned. All scaffolding in this fresh experience must survive packing.
	for generated in root.find_children("*", "Node", true, false):
		if mixed and generated != world and not world.is_ancestor_of(generated):
			continue
		generated.owner = root

static func _enrich(world: Node2D, root: Node, mixed: bool) -> void:
	world.position = Vector2.ZERO
	var gameplay := Node2D.new()
	gameplay.name = "TemplateGameplay"
	gameplay.set_script(preload("template_level_2d.gd"))
	gameplay.set("mixed", mixed)
	world.add_child(gameplay)
	var background := Polygon2D.new()
	background.name = "Backdrop"
	background.z_index = -20
	background.polygon = PackedVector2Array([Vector2(-3000, -2000), Vector2(5000, -2000), Vector2(5000, 2000), Vector2(-3000, 2000)])
	background.color = Color("132538")
	world.add_child(background)
	for i in 20:
		var line := Line2D.new()
		line.name = "Grid%02d" % i
		line.z_index = -19
		line.width = 1
		line.default_color = Color("21364a")
		line.points = PackedVector2Array([Vector2(-600 + i * 140, -1000), Vector2(-600 + i * 140, 1000)])
		world.add_child(line)
	var ground := world.get_node_or_null("BasePlate") as StaticBody2D
	if ground != null:
		ground.position = Vector2(100, 300)
		for child in ground.get_children():
			if child is CollisionShape2D and child.shape is RectangleShape2D:
				child.shape = child.shape.duplicate()
				child.shape.size = Vector2(400, 40)
			elif child is ColorRect:
				child.position = Vector2(-200, -20)
				child.size = Vector2(400, 40)
				child.color = Color("456076")
	else:
		_platform(world, "StartPlatform", Vector2(100, 300), Vector2(400, 40))
	for i in 3:
		var platform := world.get_node_or_null("Platform%d" % (i + 1)) as Node2D
		if platform != null:
			platform.position = Vector2(380 + i * 180, 220 - i * 65)
	var moving := _platform(world, "MovingBridge", Vector2(960, 90), Vector2(130, 24), true)
	moving.set_meta("motion_origin", moving.position)
	_platform(world, "UpperLanding", Vector2(1180, 30), Vector2(160, 24))
	_platform(world, "FinishPlatform", Vector2(1400, -30), Vector2(240, 28))
	for i in 6:
		_sensor(world, "Crystal%d" % (i + 1), "coin", Vector2(380 + i * 180, 160 - mini(i, 4) * 65), Vector2(22, 28), Color("ffd077"))
	_sensor(world, "Checkpoint", "checkpoint", Vector2(740, 55), Vector2(32, 40), Color("65d9cd"))
	_sensor(world, "Finish", "finish", Vector2(1450, -70), Vector2(40, 70), Color("98e8b5"))
	_sensor(world, "FallZone", "hazard", Vector2(900, 510), Vector2(2600, 30), Color("d96571"))
	var guide := world.get_node_or_null("Guide2D") as Label
	if guide != null:
		guide.position = Vector2(15, 100)
		guide.text = "01 / CIRCUITO DE CRISTALES\nSalta · activa el punto de control · llega a la meta"
	for node in world.find_children("*", "Node", true, false):
		node.owner = root

static func _platform(parent: Node, title: String, at: Vector2, size: Vector2, moving := false) -> StaticBody2D:
	var body: StaticBody2D = AnimatableBody2D.new() if moving else StaticBody2D.new()
	body.name = title
	body.position = at
	body.set_meta("_edit_group_", true)
	if body is AnimatableBody2D:
		body.sync_to_physics = false
	parent.add_child(body)
	var collision := CollisionShape2D.new()
	collision.name = "CollisionShape2D"
	var shape := RectangleShape2D.new()
	shape.size = size
	collision.shape = shape
	body.add_child(collision)
	var visual := Polygon2D.new()
	visual.name = "Visual"
	visual.polygon = PackedVector2Array([Vector2(-size.x / 2, -size.y / 2), Vector2(size.x / 2, -size.y / 2), size / 2, Vector2(-size.x / 2, size.y / 2)])
	visual.color = Color("e6ac60") if moving else Color("649fad")
	body.add_child(visual)
	return body

static func _sensor(parent: Node, title: String, kind: String, at: Vector2, size: Vector2, color: Color) -> void:
	var area := Area2D.new()
	area.name = title
	area.position = at
	area.set_meta("template_action", kind)
	parent.add_child(area)
	var collision := CollisionShape2D.new()
	collision.name = "CollisionShape2D"
	var shape := RectangleShape2D.new()
	shape.size = size
	collision.shape = shape
	area.add_child(collision)
	var visual := Polygon2D.new()
	visual.name = "Visual"
	visual.polygon = PackedVector2Array([Vector2(0, -size.y / 2), Vector2(size.x / 2, 0), Vector2(0, size.y / 2), Vector2(-size.x / 2, 0)]) if kind == "coin" else PackedVector2Array([Vector2(-size.x / 2, -size.y / 2), Vector2(size.x / 2, -size.y / 2), size / 2, Vector2(-size.x / 2, size.y / 2)])
	visual.color = color
	area.add_child(visual)
