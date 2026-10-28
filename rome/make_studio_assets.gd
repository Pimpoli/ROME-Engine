extends SceneTree

func _initialize() -> void:
	DirAccess.make_dir_recursive_absolute("res://rome/assets/templates")
	var image := Image.create_empty(256, 256, false, Image.FORMAT_RGBA8)
	for y in 256:
		for x in 256:
			var tile := (x / 64 + y / 64) % 2
			var color := Color("a7afbd") if tile == 0 else Color("929cab")
			if x % 16 == 0 or y % 16 == 0:
				color = color.darkened(0.045)
			image.set_pixel(x, y, color)
	image.save_png("res://rome/assets/templates/baseplate_grid.png")
	var path := ""
	for i in 8:
		var angle := -PI / 2.0 + i * TAU / 8.0
		var tip := Vector2(128, 128) + Vector2.from_angle(angle) * 110
		var control_a := Vector2(128, 128) + Vector2.from_angle(angle + TAU / 64.0) * 49
		var valley := Vector2(128, 128) + Vector2.from_angle(angle + TAU / 16.0) * 52
		var control_b := Vector2(128, 128) + Vector2.from_angle(angle + TAU * 7.0 / 64.0) * 49
		var next_tip := Vector2(128, 128) + Vector2.from_angle(angle + TAU / 8.0) * 110
		if i == 0:
			path = "M%f %f " % [tip.x, tip.y]
		path += "Q%f %f %f %f Q%f %f %f %f " % [control_a.x, control_a.y, valley.x, valley.y, control_b.x, control_b.y, next_tip.x, next_tip.y]
	path += "Z M164 128 A36 36 0 1 0 92 128 A36 36 0 1 0 164 128 Z"
	var svg := '<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 256 256"><path fill="#172031" fill-rule="evenodd" d="%s"/></svg>' % path
	var source := FileAccess.open("res://rome/assets/templates/spawn_marker.svg", FileAccess.WRITE)
	source.store_string(svg)
	source.close()
	var spawn := Image.new()
	spawn.load_svg_from_string(svg)
	spawn.save_png("res://rome/assets/templates/spawn_marker.png")
	quit()
