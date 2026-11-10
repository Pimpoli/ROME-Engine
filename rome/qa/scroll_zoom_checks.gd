extends RefCounted

## Real editor-surface input; run only in the disposable 3D editor fixture.
const NAVIGATION := "editors/3d/navigation/rome_studio_controls"
const ZOOM_INERTIA := "editors/3d/navigation_feel/zoom_inertia"
const TRANSLATION_INERTIA := "editors/3d/navigation_feel/translation_inertia"
const ORBIT_INERTIA := "editors/3d/navigation_feel/orbit_inertia"
const TARGET := Vector3(120, 40, 120)
const DISTANCE := 8.0

static func run(_root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var native := EditorInterface.get_base_control().get_parent()
	var editors := EditorInterface.get_base_control().find_children("*", "Node3DEditor", true, false)
	var viewport := EditorInterface.get_editor_viewport_3d(0)
	var camera := viewport.get_camera_3d() if viewport != null else null
	var surface: Control
	if viewport != null:
		for child in viewport.get_parent().get_parent().get_children():
			if child is Control and child.focus_mode == Control.FOCUS_ALL and not child.get_signal_connection_list("gui_input").is_empty():
				surface = child
	if not _expect(not editors.is_empty() and camera != null and surface != null,
			"Wheel zoom has a real native 3D camera and input surface", report):
		return report
	var editor: Node = editors[0]
	var original_view: Dictionary = editor.call("rome_get_view_state").duplicate(true)
	var selection := EditorInterface.get_selection()
	var original_selection := selection.get_selected_nodes()
	var workspace_2d := native.find_child("ROMEWorkspace2D", true, false) as Button
	var workspace_3d := native.find_child("ROMEWorkspace3D", true, false) as Button
	var original_screen := "2D" if workspace_2d != null and workspace_2d.button_pressed else "3D" if workspace_3d != null and workspace_3d.button_pressed else "Script"
	var settings := EditorInterface.get_editor_settings()
	var saved_settings := {}
	for key in [NAVIGATION, ZOOM_INERTIA, TRANSLATION_INERTIA, ORBIT_INERTIA]:
		saved_settings[key] = settings.get_setting(key)
	settings.set_setting(NAVIGATION, true)
	settings.set_setting(ZOOM_INERTIA, 0.0)
	settings.set_setting(TRANSLATION_INERTIA, 0.0)
	settings.set_setting(ORBIT_INERTIA, 0.0)
	EditorInterface.set_main_screen_editor("3D")
	selection.clear()
	native.get_viewport().gui_release_focus()
	await _settings_updated(native)

	await _pose(editor, TARGET, DISTANCE)
	var before := camera.global_transform
	var forward := -before.basis.z
	var fov := camera.fov
	var near_plane := camera.near
	var far_plane := camera.far
	var original_cursor := _viewport_state(editor)
	for tick in 150:
		_wheel(surface, MOUSE_BUTTON_WHEEL_UP)
	await _frames(native, 3)
	var first := camera.global_position
	var first_travel := (first - before.origin).dot(forward)
	_expect(first_travel > DISTANCE + 1.0 and first.is_finite(),
			"Repeated wheel-up crosses the old orbit center instead of stopping there", report)
	for tick in 150:
		_wheel(surface, MOUSE_BUTTON_WHEEL_UP)
	await _frames(native, 3)
	var second_travel := (camera.global_position - first).dot(forward)
	_expect(second_travel > DISTANCE and absf(first_travel - second_travel) < 0.05 and camera.global_position.is_finite(),
			"Wheel-up continues advancing after the center at a useful stable rate", report)
	var after_cursor := _viewport_state(editor)
	_expect(is_equal_approx(float(after_cursor.distance), DISTANCE) and before.basis.is_equal_approx(camera.global_basis) and is_equal_approx(camera.fov, fov),
			"Continuous wheel travel preserves orbit radius, orientation and field of view", report)
	_expect(is_equal_approx(camera.near, near_plane) and is_equal_approx(camera.far, far_plane),
			"Continuous wheel travel does not expand the camera clipping planes", report)

	# A distance near the previous maximum demonstrates that travel is no longer
	# coupled to that zoom bound; coordinates remain modest even with large clips.
	var old_max := minf(camera.far / 4.0, 10000.0)
	var retreat_radius := old_max * 0.98
	await _pose(editor, Vector3.ZERO, retreat_radius)
	before = camera.global_transform
	for tick in 24:
		_wheel(surface, MOUSE_BUTTON_WHEEL_DOWN)
	await _frames(native, 3)
	_expect((camera.global_position - before.origin).dot(before.basis.z) > old_max and camera.global_position.is_finite() and is_equal_approx(float(_viewport_state(editor).distance), retreat_radius),
			"Wheel-down continues retreating beyond the previous maximum without growing orbit distance", report)

	await _pose(editor, TARGET, DISTANCE)
	before = camera.global_transform
	for tick in 80:
		_wheel(surface, MOUSE_BUTTON_WHEEL_UP)
		_wheel(surface, MOUSE_BUTTON_WHEEL_DOWN)
	await _frames(native, 3)
	_expect(camera.global_position.distance_to(before.origin) < 0.01 and before.basis.is_equal_approx(camera.global_basis),
			"Equal forward and backward wheel inputs return to the same camera position", report)
	_wheel(surface, MOUSE_BUTTON_WHEEL_UP)
	await _frames(native, 3)
	var full_step := camera.global_position.distance_to(before.origin)
	await _pose(editor, TARGET, DISTANCE)
	before = camera.global_transform
	_wheel(surface, MOUSE_BUTTON_WHEEL_UP, 0.25)
	await _frames(native, 3)
	var fractional_step := camera.global_position.distance_to(before.origin)
	_expect(full_step > 0.01 and fractional_step > full_step * 0.1 and fractional_step < full_step * 0.5 and (camera.global_position - before.origin).dot(-before.basis.z) > 0.0,
			"High-resolution fractional wheel factors retain direction and smaller travel", report)
	var release_position := camera.global_position
	_wheel(surface, MOUSE_BUTTON_WHEEL_UP, 1.0, false)
	await _frames(native, 3)
	_expect(camera.global_position.distance_to(release_position) < 0.001,
			"A wheel release cannot apply the same scroll movement a second time", report)

	# Native RMB navigation keeps wheel events on its speed-control path.
	await _pose(editor, TARGET, DISTANCE)
	before = camera.global_transform
	var rmb := InputEventMouseButton.new()
	rmb.button_index = MOUSE_BUTTON_RIGHT
	rmb.position = surface.size * 0.5
	rmb.global_position = surface.global_position + rmb.position
	rmb.button_mask = MOUSE_BUTTON_MASK_RIGHT
	rmb.pressed = true
	surface.gui_input.emit(rmb)
	_wheel(surface, MOUSE_BUTTON_WHEEL_UP)
	rmb.button_mask = 0
	rmb.pressed = false
	surface.gui_input.emit(rmb)
	await _frames(native, 3)
	_expect(before.origin.distance_to(camera.global_position) < 0.001 and is_equal_approx(float(_viewport_state(editor).distance), DISTANCE),
			"Wheel input while holding RMB retains native freelook speed handling without dollying", report)

	settings.set_setting(NAVIGATION, false)
	await _settings_updated(native)
	await _pose(editor, TARGET, DISTANCE)
	for tick in 350:
		_wheel(surface, MOUSE_BUTTON_WHEEL_UP)
	await _frames(native, 3)
	var old_min := maxf(camera.near * 4.0, 0.01)
	var native_state := _viewport_state(editor)
	_expect((native_state.position as Vector3).is_equal_approx(TARGET) and is_equal_approx(float(native_state.distance), old_min),
			"Disabling Studio controls restores the native minimum orbital zoom", report)
	for tick in 400:
		_wheel(surface, MOUSE_BUTTON_WHEEL_DOWN)
	await _frames(native, 3)
	native_state = _viewport_state(editor)
	_expect((native_state.position as Vector3).is_equal_approx(TARGET) and is_equal_approx(float(native_state.distance), old_max) and camera.global_position.is_finite(),
			"Disabling Studio controls restores the native maximum orbital zoom", report)

	settings.set_setting(NAVIGATION, true)
	await _settings_updated(native)
	await _pose(editor, TARGET, DISTANCE, true)
	for tick in 350:
		_wheel(surface, MOUSE_BUTTON_WHEEL_UP)
	await _frames(native, 3)
	var orthogonal_state := _viewport_state(editor)
	_expect(camera.projection == Camera3D.PROJECTION_ORTHOGONAL and (orthogonal_state.position as Vector3).is_equal_approx(TARGET) and is_equal_approx(float(orthogonal_state.distance), old_min) and camera.size > 0 and is_finite(camera.size),
			"Orthographic wheel zoom retains a positive bounded scale and fixed focus", report)
	for tick in 400:
		_wheel(surface, MOUSE_BUTTON_WHEEL_DOWN)
	await _frames(native, 3)
	orthogonal_state = _viewport_state(editor)
	_expect((orthogonal_state.position as Vector3).is_equal_approx(TARGET) and is_equal_approx(float(orthogonal_state.distance), old_max) and camera.global_position.is_finite(),
			"Orthographic retreat retains its native maximum scale", report)

	# Zoom inertia remains effective even when normal pan inertia is zero. Compare
	# convergence with the already measured zero-inertia movement, not its formula.
	await _pose(editor, TARGET, DISTANCE)
	settings.set_setting(ZOOM_INERTIA, 0.5)
	await _settings_updated(native)
	before = camera.global_transform
	_wheel(surface, MOUSE_BUTTON_WHEEL_UP)
	await _frames(native, 2)
	var early_step := camera.global_position.distance_to(before.origin)
	await native.get_tree().create_timer(4.0).timeout
	await _frames(native, 2)
	var final_step := camera.global_position.distance_to(before.origin)
	_expect(early_step > 0.0 and early_step < full_step * 0.95 and absf(final_step - full_step) < 0.01,
			"Wheel travel follows zoom inertia and converges to the same final position (%s / %s / %s)" % [early_step, final_step, full_step], report)

	for key in saved_settings:
		settings.set_setting(key, saved_settings[key])
	await _settings_updated(native)
	editor.call("rome_set_view_state", original_view)
	selection.clear()
	for node in original_selection:
		if is_instance_valid(node) and node.is_inside_tree():
			selection.add_node(node)
	EditorInterface.set_main_screen_editor(original_screen)
	await _frames(native, 4)
	var settings_restored := true
	for key in saved_settings:
		settings_restored = settings_restored and settings.get_setting(key) == saved_settings[key]
	_expect(settings_restored and _viewport_state(editor).position == original_view.viewports[0].position and is_equal_approx(float(_viewport_state(editor).distance), float(original_view.viewports[0].distance)),
			"Wheel checks restore camera pose, navigation preferences and selection", report)
	report["completed"] = true
	return report

static func _pose(editor: Node, position: Vector3, distance: float, orthogonal := false) -> void:
	editor.call("rome_set_view_state", {"viewports": [{"position": position, "distance": distance, "x_rotation": 0.35, "y_rotation": 0.55, "orthogonal": orthogonal, "auto_orthogonal_enabled": false, "view_type": 0}]})
	await _frames(editor, 3)

static func _viewport_state(editor: Node) -> Dictionary:
	var state: Dictionary = editor.call("rome_get_view_state")
	return state.viewports[0]

static func _wheel(surface: Control, button: MouseButton, factor := 1.0, pressed := true) -> void:
	var event := InputEventMouseButton.new()
	event.button_index = button
	event.position = surface.size * 0.5
	event.global_position = surface.global_position + event.position
	event.factor = factor
	event.pressed = pressed
	surface.gui_input.emit(event)

static func _settings_updated(native: Node) -> void:
	await native.get_tree().create_timer(1.7).timeout
	await _frames(native, 2)

static func _frames(native: Node, count: int) -> void:
	for frame in count:
		await native.get_tree().process_frame

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(label)
	else:
		report.errors.append(label)
	return value
