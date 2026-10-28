@tool
extends RefCounted

## The layout is shared by the playable template and its rendered thumbnail.
## All gameplay uses ordinary Parts and editable Luau scripts; no extension
## implementation or default character scripts are changed by this builder.
const CLIENT_SOURCE := "res://addons/ROMEStudio/templates/ObbyClient.luau"
const SERVER_SOURCE := "res://addons/ROMEStudio/templates/ObbyServer.luau"
const STAGES := ["Saltos", "Láseres", "Plataformas móviles", "Equilibrio", "Zigzag", "La cima"]
const COLORS := ["8c78ed", "ff667d", "4bc9de", "edbd62", "72d5ae", "a694ff"]

static func layout() -> Array[Dictionary]:
	var pieces: Array[Dictionary] = []
	_add(pieces, "StartIsland", Vector3(0, 6, 0), Vector3(24, 1.5, 20), "dce3ef", 0)
	for i in 3:
		_add(pieces, "Jump%02d" % (i + 1), Vector3([0, 3, -2][i], 6.5 + i * 0.55, -18 - i * 11), Vector3(8, 1.2, 8), COLORS[0], 1)
	_checkpoint(pieces, 1, Vector3(0, 6, -53))
	_add(pieces, "LaserRunway", Vector3(0, 6, -82), Vector3(18, 1.5, 38), "dce3ef", 2)
	for i in 3:
		_hazard(pieces, "Laser%02d" % (i + 1), Vector3(0, 7.12, -71 - i * 12), Vector3(18, 0.75, 1.35), 2)
	_checkpoint(pieces, 2, Vector3(0, 6, -115))
	for i in 3:
		_add(pieces, "MovingPlatform%02d" % (i + 1), Vector3(14 + i * 13, 6, -115), Vector3(8, 1.5, 9), COLORS[2], 3)
		pieces[-1]["motion"] = Vector3(0, 0, 3)
		pieces[-1]["period"] = 6.0 + i * 0.4
		pieces[-1]["phase"] = i * 0.7
	_checkpoint(pieces, 3, Vector3(54, 6, -115))
	for i in 4:
		_add(pieces, "Balance%02d" % (i + 1), Vector3([54, 57, 51, 56][i], 6, -99 + i * 10), Vector3(4, 1.5, 6), COLORS[3], 4)
	_checkpoint(pieces, 4, Vector3(54, 6, -55))
	_add(pieces, "ZigzagRunway", Vector3(54, 6, -31), Vector3(18, 1.5, 34), "dce3ef", 5)
	for i in 3:
		_hazard(pieces, "ZigzagHazard%02d" % (i + 1), Vector3(54 + (-4 if i % 2 == 0 else 4), 7.12, -40 + i * 10), Vector3(10, 0.75, 3), 5)
	_checkpoint(pieces, 5, Vector3(54, 6, -2))
	for i in 4:
		_add(pieces, "SummitStep%02d" % (i + 1), Vector3(68 + i * 11, 6.8 + i * 0.8, -2), Vector3(8, 1.5, 8), COLORS[5], 6)
	_add(pieces, "Finish", Vector3(116, 10, -2), Vector3(20, 1.5, 18), "dce3ef", 6)
	pieces[-1]["checkpoint"] = 6
	_hazard(pieces, "Void", Vector3(46, -12, -52), Vector3(210, 1, 210), 0)
	pieces[-1]["color"] = "1c354b"
	pieces[-1]["collide"] = false
	return pieces

static func _add(pieces: Array[Dictionary], title: String, pos: Vector3, size: Vector3, color: String, stage: int) -> void:
	pieces.append({"name": title, "position": pos, "size": size, "color": color, "stage": stage})

static func _checkpoint(pieces: Array[Dictionary], index: int, pos: Vector3) -> void:
	_add(pieces, "Checkpoint%02d" % index, pos, Vector3(18, 1.5, 16), "dce3ef", index)
	pieces[-1]["checkpoint"] = index

static func _hazard(pieces: Array[Dictionary], title: String, pos: Vector3, size: Vector3, stage: int) -> void:
	_add(pieces, title, pos, size, "ff536d", stage)
	pieces[-1]["hazard"] = true

func build(root: Node, workspace: Node) -> void:
	if workspace.has_node("ClassicObby"):
		return
	var floor_node := workspace.get_node_or_null("Baseplate")
	if floor_node != null:
		workspace.remove_child(floor_node)
		floor_node.queue_free()
	var course := _folder("ClassicObby", workspace, root)
	course.set_meta("ROMEObbyRevision", 2)
	var stages: Array[Node] = []
	stages.append(_folder("Start", course, root))
	for index in 6:
		stages.append(_folder("Stage%02d" % (index + 1), course, root))
	for data in layout():
		var part: Node3D = ClassDB.instantiate("Part")
		part.name = data.name
		part.set("Anchored", true)
		part.set("Size", data.size)
		part.set("Color", Color(data.color))
		part.set("CanCollide", data.get("collide", true))
		part.set("CanTouch", data.has("hazard") or data.has("checkpoint"))
		part.position = data.position
		if data.has("hazard"):
			part.set_meta("ROMEHazard", true)
		if data.has("checkpoint"):
			part.set_meta("ROMECheckpoint", data.checkpoint)
		if data.has("motion"):
			part.set_meta("ROMEMotionAxis", data.motion)
			part.set_meta("ROMEMotionPeriod", data.period)
			part.set_meta("ROMEMotionPhase", data.phase)
		stages[data.stage].add_child(part)
		part.owner = root
	var spawn := workspace.get_node_or_null("SpawnLocation") as Node3D
	if spawn != null:
		spawn.position = Vector3(0, 7.25, 3)
		spawn.set("Size", Vector3(10, 1, 10))
		spawn.set("Color", Color("eef1f7"))
	var decorations := Node3D.new()
	decorations.name = "SignsAndTrim"
	course.add_child(decorations)
	decorations.owner = root
	decorate(decorations, root)
	var starter := root.get_node_or_null("StarterPlayer/StarterPlayerScripts")
	var server := root.get_node_or_null("ServerScriptService")
	if starter != null and server != null:
		_script_node("LocalScript", "ObbyClient", CLIENT_SOURCE, starter, root)
		_script_node("ServerScript", "ObbyServer", SERVER_SOURCE, server, root)
	root.set_meta("rome_obby_revision", 2)

static func decorate(parent: Node3D, owner_root: Node = null) -> void:
	# Meshes stay under a separate Node3D, away from Part's adopted body mesh.
	for data in layout():
		if data.has("checkpoint"):
			var index: int = data.checkpoint
			var color := Color(COLORS[index - 1])
			var p: Vector3 = data.position
			_trim(parent, p + Vector3(0, 0.82, 0), Vector3(data.size.x - 1, 0.12, data.size.z - 1), color, owner_root)
			_trim(parent, p + Vector3(0, 0.90, 0), Vector3(data.size.x - 2, 0.12, data.size.z - 2), Color("dce3ef"), owner_root)
			_label(parent, "%02d / 06" % index, p + Vector3(0, 1.01, 0), color.darkened(0.45), true, owner_root)
		elif data.has("motion"):
			var p: Vector3 = data.position
			# A static guide rail makes the platform's travel direction readable.
			_trim(parent, p + Vector3(0, -1.2, 0), Vector3(0.25, 0.25, 15), Color("3b6379"), owner_root)
	_label(parent, "CLASSIC OBBY", Vector3(0, 13.5, -7), Color("eef1f8"), false, owner_root)
	_label(parent, "6 retos  ·  alcanza cada punto de control", Vector3(0, 11.1, -7), Color("b5c4dc"), false, owner_root, 38)
	_label(parent, "LAS SUPERFICIES ROJAS MATAN", Vector3(0, 10.2, -64), Color("ff8a9b"), false, owner_root, 37)
	_label(parent, "ESPERA · SALTA · VIAJA", Vector3(27, 11, -123), Color("7dd8e9"), false, owner_root, 42)
	_trim(parent, Vector3(108, 15, -7), Vector3(0.8, 10, 0.8), Color("8c78ed"), owner_root)
	_trim(parent, Vector3(124, 15, -7), Vector3(0.8, 10, 0.8), Color("4bc9de"), owner_root)
	_trim(parent, Vector3(116, 20, -7), Vector3(16.8, 0.9, 0.8), Color("edbd62"), owner_root)
	_label(parent, "META", Vector3(116, 17.3, -7), Color("ffffff"), false, owner_root, 72)

static func _trim(parent: Node3D, pos: Vector3, size: Vector3, color: Color, owner_root: Node) -> void:
	var mesh := MeshInstance3D.new()
	mesh.name = "Accent"
	var box := BoxMesh.new()
	box.size = size
	mesh.mesh = box
	var material := StandardMaterial3D.new()
	material.albedo_color = color
	material.roughness = 0.85
	mesh.material_override = material
	mesh.position = pos
	parent.add_child(mesh)
	if owner_root != null:
		mesh.owner = owner_root

static func _label(parent: Node3D, title: String, pos: Vector3, color: Color, flat: bool, owner_root: Node, font_size: int = 56) -> void:
	var label := Label3D.new()
	label.name = "Sign"
	label.text = title
	label.font_size = font_size
	label.pixel_size = 0.018
	label.modulate = color
	label.outline_modulate = Color("17263b")
	label.outline_size = 6 if not flat else 0
	label.no_depth_test = false
	label.position = pos
	if flat:
		label.rotation_degrees.x = -90
	else:
		label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	parent.add_child(label)
	if owner_root != null:
		label.owner = owner_root

func _folder(title: String, parent: Node, root: Node) -> Node:
	var folder: Node = ClassDB.instantiate("Folder")
	folder.name = title
	parent.add_child(folder)
	folder.owner = root
	return folder

func _script_node(type_name: String, title: String, source_path: String, parent: Node, root: Node) -> void:
	var source := FileAccess.get_file_as_string(source_path)
	if source.is_empty():
		push_error("ROME Obby: missing template " + source_path)
		return
	var script_files = preload("res://addons/ROMEStudio/studio_script_files.gd")
	# The bundled runtime's resource loader recognizes .lua (the contents are Luau).
	var path: String = script_files.unique_path(type_name, title)
	# The files belong to the game, outside the language extension's directory.
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_error("ROME Obby: cannot create " + path)
		return
	file.store_string(source)
	file.close()
	var resource: Resource = ClassDB.instantiate("LuauScript")
	resource.call("gl_set_fuente", source)
	resource.take_over_path(path)
	var node: Node = ClassDB.instantiate(type_name)
	node.name = title
	node.set("codigo_luau", resource)
	parent.add_child(node)
	node.owner = root
