extends RefCounted

## Real window/layout checks; run only in the disposable editor fixture.
static func run(native: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var scroll := native.find_child("ROMEToolbarTools", true, false) as ScrollContainer
	var row := native.find_child("ROMEToolbarToolsRow", true, false) as HBoxContainer
	var previous := native.find_child("ROMEToolbarPrevious", true, false) as Button
	var next := native.find_child("ROMEToolbarNext", true, false) as Button
	var add_tab := native.find_child("ROMEAddTab", true, false) as Button
	if not _expect(scroll != null and row != null and previous != null and next != null and add_tab != null,
			"Ribbon exposes explicit previous/next overflow controls", report):
		return report
	var tab_row := add_tab.get_parent()
	var home := tab_row.get_child(0) as Button
	var plugins := tab_row.get_child(5) as Button
	var active_index := 0
	for child in tab_row.get_children():
		if child is Button and child.toggle_mode and child.button_pressed:
			active_index = child.get_index()
	var original_custom_count: int = native.call("rome_get_custom_tabs").size()
	var original_options: Dictionary = native.call("rome_get_toolbar_options")
	var original_scroll := scroll.scroll_horizontal
	var window := native.get_window()
	var original_size := window.size
	var original_position := window.position
	var original_mode := window.mode
	window.mode = Window.MODE_WINDOWED
	window.size = Vector2i(1152, 680)
	native.call("rome_set_toolbar_options", {"compact": false, "labels": true, "collapsed": false, "left_aligned": false})
	_select(home)
	await _settle(native, 8)
	_expect(previous.visible and next.visible and previous.disabled and not next.disabled and scroll.scroll_horizontal == 0,
			"At 1152px the ribbon identifies hidden commands and starts at its first tool", report)
	next.pressed.emit()
	await _settle(native)
	_expect(scroll.scroll_horizontal > 0 and not previous.disabled,
			"The next chevron scrolls the actual command viewport", report)
	for step in 10:
		if next.disabled:
			break
		next.pressed.emit()
		await _settle(native)
	var resources := native.find_child("ToolRecursos", true, false) as Button
	var visible_end := false
	if resources != null:
		var viewport_rect := scroll.get_global_rect()
		var tool_rect := resources.get_global_rect()
		visible_end = tool_rect.position.x >= viewport_rect.position.x - 1.0 and tool_rect.end.x <= viewport_rect.end.x + 1.0
	_expect(next.disabled and not previous.disabled and visible_end,
			"Scrolling to the end exposes the complete Resources tool and disables the end chevron", report)
	var end_position := scroll.scroll_horizontal
	previous.pressed.emit()
	await _settle(native)
	_expect(scroll.scroll_horizontal < end_position,
			"The previous chevron returns toward earlier commands", report)
	_select(plugins)
	await _settle(native)
	_expect(scroll.scroll_horizontal == 0 and not previous.visible and not next.visible,
			"Switching to a short page resets scrolling and hides unnecessary chevrons", report)
	_select(home)
	await _settle(native)
	_expect(scroll.scroll_horizontal == 0 and previous.visible and previous.disabled,
			"Returning to a long page starts at Select with a disabled previous chevron", report)
	native.call("rome_set_toolbar_options", {"collapsed": true})
	await _settle(native)
	_expect(not row.visible and not scroll.visible and not previous.visible and not next.visible,
			"A collapsed ribbon hides its command row and overflow controls", report)
	native.call("rome_set_toolbar_options", {"collapsed": false})
	window.size = Vector2i(1920, 1080)
	await _settle(native, 8)
	_expect(not previous.visible and not next.visible,
			"At 1920px the complete Home page fits without overflow controls", report)
	var tabs_scroll := native.find_child("ROMEToolbarTabs", true, false) as ScrollContainer
	var centered := false
	if tabs_scroll != null:
		var mezzanine := tabs_scroll.get_parent() as Control
		centered = absf(tabs_scroll.get_global_rect().get_center().x - mezzanine.get_global_rect().get_center().x) <= 2.0
	_expect(centered, "At 1920px the ribbon tab strip is centered independently of the run controls", report)
	var options := native.find_child("ROMEToolbarOptions", true, false) as MenuButton
	var run_controls := native.find_child("ROMETestControls", true, false) as Control
	var workspace := native.find_child("ROMEWorkspaceControls", true, false) as Control
	_expect(tab_row.get_child(1).text == "Modelo" and workspace.get_global_rect().position.x >= tabs_scroll.get_global_rect().end.x - 1.0 and workspace.is_ancestor_of(options), "Modelo follows Inicio and workspace controls stay outside the scrollable tabs", report)
	options.get_popup().id_pressed.emit(3)
	await _settle(native, 6)
	var gap := tabs_scroll.get_global_rect().position.x - run_controls.get_global_rect().end.x
	_expect(gap >= 0.0 and gap <= 6.0 * EditorInterface.get_editor_scale() and bool(EditorInterface.get_editor_settings().get_setting("rome/studio/toolbar_left_aligned")), "Left alignment places the tabs next to test controls and saves the preference", report)
	options.get_popup().id_pressed.emit(3)
	await _settle(native, 6)
	_expect(absf(tabs_scroll.get_global_rect().get_center().x - tabs_scroll.get_parent().get_global_rect().get_center().x) <= 2.0, "Disabling left alignment recenters the tabs", report)
	var fixture_name := "CenterQA" + str(Time.get_ticks_usec())
	for index in 8:
		native.call("rome_create_custom_tab", fixture_name + " " + str(index))
	await _settle(native, 8)
	centered = tabs_scroll != null and absf(tabs_scroll.get_global_rect().get_center().x - tabs_scroll.get_parent().get_global_rect().get_center().x) <= 2.0
	_expect(centered and tab_row.get_combined_minimum_size().x > tabs_scroll.size.x and tabs_scroll.scroll_horizontal > 0 and window.size.x == 1920,
			"Many custom tabs stay centered and scroll to the active tab without growing the wide editor", report)
	window.size = Vector2i(1152, 680)
	_select(home)
	await _settle(native, 8)
	_expect(window.size.x == 1152 and row.size.x <= window.size.x and tabs_scroll.size.x < tab_row.get_combined_minimum_size().x and previous.visible and next.visible,
			"A wide editor with many custom tabs can shrink back to 1152px and retain every command", report)
	while native.call("rome_get_custom_tabs").size() > original_custom_count:
		native.call("rome_remove_custom_tab", native.call("rome_get_custom_tabs").size() - 1)
	await _settle(native)
	var pages := scroll.get_child(0) as Control
	window.size = Vector2i(ceili(pages.get_combined_minimum_size().x + window.size.x - row.size.x + 2), 680)
	await _settle(native, 8)
	var stable_visible := previous.visible
	var stable := true
	for frame in 10:
		await native.get_tree().process_frame
		stable = stable and previous.visible == stable_visible and next.visible == stable_visible
	_expect(stable, "Overflow visibility remains stable at the fit threshold", report)

	_select(tab_row.get_child(active_index) as Button)
	native.call("rome_set_toolbar_options", original_options)
	window.size = original_size
	window.position = original_position
	window.mode = original_mode
	await _settle(native, 8)
	scroll.scroll_horizontal = original_scroll
	report["completed"] = true
	return report

static func _select(button: Button) -> void:
	button.button_pressed = true
	button.pressed.emit()

static func _settle(native: Node, frames := 4) -> void:
	for frame in frames:
		await native.get_tree().process_frame

static func _expect(value: bool, title: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(title)
	else:
		report.errors.append(title)
	return value
