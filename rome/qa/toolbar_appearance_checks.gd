extends RefCounted

## Only run in a disposable editor fixture. Restore preferences and custom tabs.
static func run(native: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var original: Dictionary = native.call("rome_get_toolbar_options")
	var custom_count: int = native.call("rome_get_custom_tabs").size()
	var piece := native.find_child("ToolPieza", true, false) as Button
	var scroll := native.find_child("ROMEToolbarTools", true, false) as ScrollContainer
	var options := native.find_child("ROMEToolbarOptions", true, false) as MenuButton
	if not _expect(piece != null and scroll != null and options != null, "Toolbar exposes its appearance menu and tools", report):
		return report
	native.call("rome_set_toolbar_options", {"compact": false, "labels": true, "collapsed": false})
	var normal := piece.custom_minimum_size
	native.call("rome_set_toolbar_options", {"compact": true, "labels": false})
	var icon := piece.get_node("ToolContent/ToolIcon") as TextureRect
	_expect(piece.custom_minimum_size.x < normal.x and piece.custom_minimum_size.y < normal.y and is_equal_approx(icon.custom_minimum_size.x, 18 * EditorInterface.get_editor_scale()), "Compact mode shrinks buttons and icons", report)
	_expect(not piece.get_node("ToolContent/ToolCaption").visible and piece.get_node("Arrow").visible and not piece.tooltip_text.is_empty(), "Icon-only tools retain dropdowns and tooltips", report)
	var every_caption_hidden := true
	for caption in native.find_children("ToolCaption", "Label", true, false):
		every_caption_hidden = every_caption_hidden and not caption.visible
	_expect(every_caption_hidden, "Label visibility applies to every standard page", report)
	native.call("rome_set_toolbar_options", {"compact": "false"})
	_expect(bool(native.call("rome_get_toolbar_options").compact), "Invalid preference types are ignored", report)
	var name := "Appearance QA " + str(Time.get_ticks_usec())
	var created: int = native.call("rome_create_custom_tab", name)
	_expect(created == custom_count + 6, "A custom tab can be created in compact mode", report)
	if created >= 6:
		native.call("rome_set_custom_tools", created - 6, PackedStringArray(["Pieza", "Recursos", "@divider"]))
		var add := native.find_child("AddTools" + str(created - 6), true, false) as Button
		var custom_piece := add.get_parent().find_child("ToolPieza", false, false) as Button
		_expect(custom_piece != null and not custom_piece.get_node("ToolContent/ToolCaption").visible and add.text == "+" and add.custom_minimum_size.y < normal.y, "Rebuilt custom pages inherit compact and icon-only appearance", report)
	options.get_popup().id_pressed.emit(1)
	_expect(piece.get_node("ToolContent/ToolCaption").visible and bool(native.call("rome_get_toolbar_options").labels), "Appearance menu toggles captions on existing controls", report)
	options.get_popup().id_pressed.emit(2)
	var tab_row := native.find_child("ROMEAddTab", true, false).get_parent()
	_expect(not scroll.visible and tab_row.is_visible_in_tree(), "Collapsed toolbar keeps the tab strip visible", report)
	var home := tab_row.get_child(0) as Button
	home.mouse_entered.emit()
	_expect(scroll.visible and home.button_pressed, "Hovering a standard tab reveals its tools", report)
	if created >= 6:
		var custom_tab := tab_row.get_child(created) as Button
		custom_tab.mouse_entered.emit()
		_expect(scroll.visible and custom_tab.button_pressed, "New custom tabs support temporary hover reveal", report)
	var settings := EditorInterface.get_editor_settings()
	_expect(bool(settings.get_setting("rome/studio/toolbar_compact")) and bool(settings.get_setting("rome/studio/toolbar_labels")) and bool(settings.get_setting("rome/studio/toolbar_collapsed")), "Toolbar appearance is saved in editor preferences", report)
	# Test overflow without hover-preview changing the active tab under the mouse.
	native.call("rome_set_toolbar_options", {"collapsed": false})
	var window_size := native.get_window().size
	for index in 5:
		native.call("rome_create_custom_tab", name.left(18) + " Extra " + str(index))
	_expect(native.call("rome_get_custom_tabs").size() == custom_count + 6, "Overflow fixture creates five additional valid custom tabs", report)
	for frame in 5:
		await native.get_tree().process_frame
	var tabs_scroll := native.find_child("ROMEToolbarTabs", true, false) as ScrollContainer
	_expect(tabs_scroll != null and tabs_scroll.size.x < tab_row.get_combined_minimum_size().x and native.get_window().size == window_size, "Extra custom tabs scroll without widening the editor window", report)
	if tabs_scroll != null:
		var active_tab := tab_row.get_child(created + 5) as Button
		var tab_rect := active_tab.get_global_rect()
		var strip_rect := tabs_scroll.get_global_rect()
		_expect(active_tab.button_pressed and tab_rect.position.x >= strip_rect.position.x - 1.0 and tab_rect.end.x <= strip_rect.end.x + 1.0, "The active custom tab is fully visible even when the strip overflows", report)
	while native.call("rome_get_custom_tabs").size() > custom_count + 1:
		native.call("rome_remove_custom_tab", native.call("rome_get_custom_tabs").size() - 1)
	if created >= 6:
		native.call("rome_remove_custom_tab", created - 6)
	native.call("rome_set_toolbar_options", original)
	_expect(native.call("rome_get_custom_tabs").size() == custom_count and native.call("rome_get_toolbar_options") == original, "Appearance checks restore preferences and existing custom tabs", report)
	report["completed"] = true
	return report

static func _expect(value: bool, title: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(title)
	else:
		report.errors.append(title)
	return value
