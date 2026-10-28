extends SceneTree

func _initialize() -> void:
	var image := Image.load_from_file("res://rome/assets/branding/rome.png")
	if image == null:
		quit(1)
		return
	image.convert(Image.FORMAT_RGBA8)
	var wordmark := Image.load_from_file("res://rome/assets/branding/rome-engine.png") if FileAccess.file_exists("res://rome/assets/branding/rome-engine.png") else image
	wordmark.convert(Image.FORMAT_RGBA8)
	DirAccess.make_dir_recursive_absolute("res://rome/assets/branding/rendered")
	for size in [16, 24, 32, 48, 64, 128, 256, 512, 1024]:
		var icon := image.duplicate() as Image
		icon.resize(size, size, Image.INTERPOLATE_LANCZOS)
		icon.save_png("res://rome/assets/branding/rendered/icon%d.png" % size)
	var targets := ["main/app_icon.png", "main/splash.png", "misc/logo/icon.png", "misc/logo/icon_outlined.png", "misc/logo/logo.png", "misc/logo/logo_outlined.png", "platform/android/java/editor/src/picoos/assets/vr_splash.png", "platform/android/java/editor/src/horizonos/assets/vr_splash.png", "misc/dist/apple_embedded_xcode/godot_apple_embedded/Images.xcassets/SplashImage.imageset/splash@2x.png", "misc/dist/apple_embedded_xcode/godot_apple_embedded/Images.xcassets/SplashImage.imageset/splash@3x.png"]
	for target: String in targets:
		if not FileAccess.file_exists("res://" + target):
			continue
		var original := Image.load_from_file("res://" + target)
		var width := original.get_width()
		var height := original.get_height()
		var result := Image.create_empty(width, height, false, Image.FORMAT_RGBA8)
		result.fill(Color.TRANSPARENT)
		var full_mark: bool = target.contains("splash") or target.contains("/logo.png") or target.contains("/logo_outlined.png")
		var icon := (wordmark if full_mark else image).duplicate() as Image
		var factor := minf(float(width) / icon.get_width(), float(height) / icon.get_height())
		var size := Vector2i(maxi(1, int(icon.get_width() * factor)), maxi(1, int(icon.get_height() * factor)))
		icon.resize(size.x, size.y, Image.INTERPOLATE_LANCZOS)
		result.blit_rect(icon, Rect2i(Vector2i.ZERO, size), Vector2i((width - size.x) / 2, (height - size.y) / 2))
		result.save_png("res://" + target)
	# Android ships bitmap defaults independently of the desktop editor icons.
	var android := "res://platform/android/java/lib/src/main/res/"
	for folder in ["mipmap", "mipmap-mdpi", "mipmap-hdpi", "mipmap-xhdpi", "mipmap-xxhdpi", "mipmap-xxxhdpi", "drawable"]:
		for filename in DirAccess.get_files_at(android + folder):
			if not filename.ends_with(".webp") or not (filename.begins_with("icon") or filename.begins_with("splash_")):
				continue
			var path: String = android + folder + "/" + filename
			var original := Image.load_from_file(path)
			var target_size := Vector2i(original.get_width(), original.get_height())
			var result := Image.create_empty(target_size.x, target_size.y, false, Image.FORMAT_RGBA8)
			result.fill(Color("172333") if filename == "icon_background.webp" else Color.TRANSPARENT)
			if filename != "icon_background.webp":
				var mark := (wordmark if filename == "splash_branding_image.webp" else image).duplicate() as Image
				var padding := 0.65 if filename == "icon_foreground.webp" else 1.0
				var factor := minf(float(target_size.x) / mark.get_width(), float(target_size.y) / mark.get_height()) * padding
				var size := Vector2i(maxi(1, int(mark.get_width() * factor)), maxi(1, int(mark.get_height() * factor)))
				mark.resize(size.x, size.y, Image.INTERPOLATE_LANCZOS)
				result.blit_rect(mark, Rect2i(Vector2i.ZERO, size), (target_size - size) / 2)
			result.save_webp(path, true)
	# The instrumented Android sample also has a default project icon.
	var sample := "res://platform/android/java/app/src/instrumented/assets/icon.svg"
	var svg := FileAccess.get_file_as_string("res://rome/assets/branding/rome.svg")
	var file := FileAccess.open(sample, FileAccess.WRITE)
	if file != null:
		file.store_string(svg)
		file.close()
	print("ROME raster branding generated")
	quit()
