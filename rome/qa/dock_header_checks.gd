extends RefCounted

## Reversible native dock actions in a disposable editor project.
static func run(native: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var dock := native.find_child("ROMEAssets", true, false) as EditorDock
	if not _expect(dock != null, "Dock headers have a registered Resources panel", report):
		return report
	dock.make_visible()
	await _frames(native, 3)
	var tabs := dock.get_parent() as TabContainer
	if not _expect(tabs != null, "Resources opens in a dock tab container", report):
		return report
	var header := tabs.get_tab_bar().get_parent()
	var actions := header.find_child("ROMEDockHeaderActions", false, false) as HBoxContainer
	var close := header.find_child("ROMEDockClose", true, false) as Button
	var floating := header.find_child("ROMEDockFloat", true, false) as Button
	if not _expect(actions != null and close != null and floating != null, "Dock headers expose quick Close and Float actions", report):
		return report
	var tab_count := tabs.get_tab_count()
	_expect(tab_count > 0 and tabs.get_tab_control(tabs.current_tab) == dock, "Header action controls do not become editor dock tabs", report)
	_expect(tabs.get_tab_bar().get_global_rect().end.x <= actions.get_global_rect().position.x + 1.0 and actions.size.x >= 36 * EditorInterface.get_editor_scale(), "Header actions reserve layout width without covering tab names", report)
	var was_global := dock.is_global()
	var was_closable := dock.is_closable()
	var layouts := dock.get_available_layouts()
	dock.set_global(false)
	dock.set_closable(false)
	dock.set_available_layouts(EditorDock.DOCK_LAYOUT_VERTICAL)
	tabs.tab_changed.emit(tabs.current_tab)
	_expect(close.disabled and floating.disabled, "A restricted dock disables unsupported header actions", report)
	close.pressed.emit()
	floating.pressed.emit()
	_expect(dock.get_parent() == tabs and tabs.get_tab_count() == tab_count, "Header action dispatch revalidates close and floating permissions", report)
	dock.set_global(was_global)
	dock.set_closable(was_closable)
	dock.set_available_layouts(layouts)
	tabs.tab_changed.emit(tabs.current_tab)
	if not close.disabled:
		close.pressed.emit()
		await _frames(native, 3)
		_expect(dock.get_parent() != tabs and tabs.get_tab_count() == tab_count - 1 and is_instance_valid(dock), "Quick Close hides the registered panel without freeing its contents", report)
		dock.make_visible()
		await _frames(native, 3)
		_expect(dock.get_parent() == tabs and tabs.get_tab_count() == tab_count and dock.is_visible_in_tree(), "The panel reopens in its original dock slot", report)
	if not floating.disabled:
		var main_window := native.get_window()
		floating.pressed.emit()
		await _frames(native, 3)
		var external := dock.get_window()
		var floated := external != main_window
		_expect(floated and external.title.contains("ROME Engine"), "Quick Float uses a real ROME-branded editor window", report)
		if floated:
			external.close_requested.emit()
			await _frames(native, 4)
		_expect(dock.get_parent() == tabs and dock.get_window() == main_window and tabs.get_tab_count() == tab_count, "Closing the floating window restores its panel to the original slot", report)
	else:
		_expect(floating.disabled, "Single-window mode leaves Float unavailable", report)
	_expect(dock.is_global() == was_global and dock.is_closable() == was_closable and dock.get_available_layouts() == layouts, "Dock header checks restore panel capabilities", report)
	await _check_studio_layout(native, dock, tabs, report)
	report["completed"] = true
	return report

static func _check_studio_layout(native: Node, resources: EditorDock, resource_slot: TabContainer, report: Dictionary) -> void:
	var quick := native.find_child("ROMEQuickPanels", true, false) as MenuButton
	if not _expect(quick != null, "Workspace has a quick panel menu", report):
		return
	var popup := quick.get_popup()
	quick.show_popup()
	_expect(popup.visible and popup.position.x + popup.size.x <= quick.get_screen_position().x + quick.size.x + 1.0, "The quick panel popup fits inside the right edge of the editor", report)
	popup.hide()
	var preset := -1
	for index in popup.item_count:
		if popup.get_item_text(index).ends_with("estilo Studio"):
			preset = popup.get_item_id(index)
	if not _expect(preset >= 0, "Quick panel menu exposes the Studio layout preset", report):
		return

	var scene := EditorInterface.get_edited_scene_root()
	var selected := EditorInterface.get_selection().get_selected_nodes()
	popup.id_pressed.emit(preset)
	await _frames(native, 5)
	var upper := native.find_child("DockSlotRightUR", true, false) as TabContainer
	var lower := native.find_child("DockSlotRightBR", true, false) as TabContainer
	var output := native.find_child("Output", true, false) as EditorDock
	_expect(upper.get_tab_title(upper.current_tab) == "Explorador" and lower.is_ancestor_of(EditorInterface.get_inspector()) and upper.get_global_rect().end.y <= lower.get_global_rect().position.y + 1.0, "Studio layout stacks Explorer above Properties on the right", report)
	_expect(upper.tab_alignment == TabBar.ALIGNMENT_LEFT and lower.tab_alignment == TabBar.ALIGNMENT_LEFT, "Studio side panels use left-aligned headers", report)
	_expect(output.get_parent().get_class() == "EditorBottomPanel" and output.is_visible_in_tree(), "Studio layout keeps Output expanded below the world", report)
	_expect(EditorInterface.get_edited_scene_root() == scene and EditorInterface.get_selection().get_selected_nodes() == selected, "Applying the workspace preset preserves the open scene and selection", report)
	popup.about_to_popup.emit()
	_expect(popup.is_item_checked(0) and popup.is_item_checked(1) and not popup.is_item_checked(2) and popup.is_item_checked(3), "Quick panel check marks reflect the current workspace", report)
	popup.id_pressed.emit(2)
	await _frames(native, 3)
	_expect(resources.is_visible_in_tree() and resources.get_parent() == resource_slot and popup.is_item_checked(2), "Resources reopens at its original slot from the quick menu", report)
	popup.id_pressed.emit(2)
	await _frames(native, 3)
	_expect(not resources.is_visible_in_tree() and not popup.is_item_checked(2), "Quick panels can hide Resources again", report)
	popup.id_pressed.emit(2)
	await _frames(native, 3)

static func _frames(native: Node, count: int) -> void:
	for frame in count:
		await native.get_tree().process_frame

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(label)
	else:
		report.errors.append(label)
	return value
