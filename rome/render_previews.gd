extends SceneTree

var viewport: SubViewport
var world: Node3D

func mat(color: Color) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	m.roughness = 0.88
	return m

func box(pos: Vector3, size: Vector3, color: Color) -> MeshInstance3D:
	var mesh := MeshInstance3D.new()
	var shape := BoxMesh.new()
	shape.size = size
	mesh.mesh = shape
	mesh.material_override = mat(color)
	mesh.position = pos
	world.add_child(mesh)
	return mesh

func _initialize() -> void:
	root.mode = Window.MODE_MINIMIZED
	call_deferred("render_all")

func build(kind: String) -> void:
	viewport = SubViewport.new()
	viewport.size = Vector2i(1024, 1024)
	viewport.own_world_3d = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	viewport.msaa_3d = Viewport.MSAA_4X
	root.add_child(viewport)
	world = Node3D.new()
	viewport.add_child(world)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	var sky_mat := ProceduralSkyMaterial.new()
	sky_mat.sky_top_color = Color("548fc4")
	sky_mat.sky_horizon_color = Color("b3cbdc")
	sky_mat.ground_horizon_color = Color("b3cbdc")
	sky_mat.ground_bottom_color = Color("7f9db8")
	var sky := Sky.new()
	sky.sky_material = sky_mat
	env.environment.sky = sky
	env.environment.background_mode = Environment.BG_SKY
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color("dae0eb")
	env.environment.ambient_light_energy = 0.35
	env.environment.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	world.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-48, -28, 0)
	sun.light_energy = 0.85
	sun.shadow_enabled = true
	world.add_child(sun)
	var camera := Camera3D.new()
	world.add_child(camera)
	camera.current = true
	camera.fov = 58
	camera.position = Vector3(18, 13, 24)
	camera.look_at(Vector3(0, 0, 0))
	if kind == "empty":
		camera.position = Vector3(0, 8, 20)
		camera.look_at(Vector3(0, 8, 0))
		return
	if kind == "obby":
		var course = load("res://rome/addons/ROMEStudio/studio_obby.gd")
		for data in course.layout():
			box(data.position, data.size, Color(data.color))
		var details := Node3D.new()
		world.add_child(details)
		course.decorate(details)
		# Small in-world instructions become noisy at card size. The playable
		# scene retains them; the gallery emphasizes the actual route silhouette.
		for label in details.find_children("*", "Label3D", true, false):
			label.visible = label.text == "META"
		box(Vector3(0, 7.25, 3), Vector3(10, 1, 10), Color("eef1f7"))
		spawn_emblem(Vector3(0, 7.765, 3), 8)
		camera.position = Vector3(132, 150, 118)
		camera.projection = Camera3D.PROJECTION_ORTHOGONAL
		camera.size = 163
		camera.look_at(Vector3(46, 5, -52))
		return
	var floor_mesh := box(Vector3(0, -0.5, 0), Vector3(1000, 1, 1000), Color.WHITE)
	var checker := Image.create_empty(256, 256, false, Image.FORMAT_RGB8)
	for y in 256:
		for x in 256:
			var color := Color("788394") if (x / 64 + y / 64) % 2 == 0 else Color("667387")
			if x % 16 == 0 or y % 16 == 0:
				color = color.darkened(0.035)
			checker.set_pixel(x, y, color)
	var floor_mat := mat(Color.WHITE)
	floor_mat.albedo_texture = ImageTexture.create_from_image(checker)
	floor_mat.uv1_scale = Vector3(125, 125, 125)
	floor_mat.texture_filter = BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
	floor_mesh.material_override = floor_mat
	box(Vector3(0, 0.6, 0), Vector3(12, 1, 12), Color("e5e9f1"))
	spawn_emblem(Vector3(0, 1.115, 0), 10)

func spawn_emblem(pos: Vector3, size: float) -> void:
	var emblem := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(size, size)
	emblem.mesh = plane
	var emblem_mat := mat(Color.WHITE)
	emblem_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	emblem_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	emblem_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	emblem_mat.albedo_texture = ImageTexture.create_from_image(Image.load_from_file("res://rome/assets/templates/spawn_marker.png"))
	emblem.material_override = emblem_mat
	emblem.position = pos
	world.add_child(emblem)

func render_all() -> void:
	var output := OS.get_environment("ROME_PREVIEW_OUTPUT")
	if output.is_empty():
		quit(1)
		return
	DirAccess.make_dir_recursive_absolute(output)
	for kind in ["baseplate", "empty", "obby"]:
		build(kind)
		for frame in 8:
			await process_frame
		RenderingServer.force_draw(false)
		var image := viewport.get_texture().get_image()
		image.save_png(output.path_join(kind + ".png"))
		root.remove_child(viewport)
		viewport.queue_free()
		print("Rendered ", kind)
	quit()
