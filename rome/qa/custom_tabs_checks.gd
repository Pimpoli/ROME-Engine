extends RefCounted
## Extra native ribbon checks. Returns failures; an empty array means success.
## Every temporary tab is removed and the original preferences are preserved.

static func run(native: Node) -> Array[String]:
	var errors: Array[String] = []
	var original: Array = native.call("rome_get_custom_tabs")
	var prefix := "Ribbon " + str(Time.get_ticks_usec()).right(10)
	var first: int = int(native.call("rome_create_custom_tab", prefix + " A")) - 6
	var second: int = int(native.call("rome_create_custom_tab", prefix + " B")) - 6
	_expect(first == original.size() and second == first + 1, "Temporary tabs append after existing preferences", errors)
	if first >= 0 and second >= 0:
		native.call("rome_set_custom_tools", first, PackedStringArray(["Mover", "@divider", "Recursos", "Script", "@divider", "Mover", "Invalid QA Tool"]))
		_expect(_tools(native, first) == PackedStringArray(["Mover", "@divider", "Recursos", "Script", "@divider"]), "Dividers preserve order; commands remain validated and deduplicated", errors)
		_tool_action(native, first, 2, 0, errors)
		_expect(_tools(native, first) == PackedStringArray(["Mover", "Recursos", "@divider", "Script", "@divider"]), "Move left reorders a tool across a divider", errors)
		_tool_action(native, first, 2, 1, errors)
		_expect(_tools(native, first) == PackedStringArray(["Mover", "Recursos", "Script", "@divider", "@divider"]), "Move right reorders the selected divider", errors)
		_tool_action(native, first, 3, 2, errors)
		_expect(_tools(native, first) == PackedStringArray(["Mover", "Recursos", "Script", "@divider"]), "Remove separator removes only the selected entry", errors)
		_tool_action(native, first, 1, 2, errors)
		_expect(_tools(native, first) == PackedStringArray(["Mover", "Script", "@divider"]), "Remove tool leaves other shortcuts intact", errors)

		var before_move := _tools(native, first)
		_tab_action(native, second, 2, errors)
		_expect(_name(native, first) == prefix + " B" and _tools(native, second) == before_move, "Moving a tab preserves each tab's tools", errors)
		# Indices changed: obtain the rebuilt menu at the new index.
		_tab_action(native, second, 0, errors)
		var dialog := native.find_child("ROMENewTabDialog", true, false) as ConfirmationDialog
		var input := native.find_child("ROMENewTabName", true, false) as LineEdit
		if dialog != null and input != null:
			input.text = prefix + " B"
			dialog.emit_signal("confirmed")
			_expect(_name(native, second) == prefix + " A", "Rename rejects a duplicate after tab reordering", errors)
			input.text = prefix + " Renamed"
			dialog.emit_signal("confirmed")
			_expect(_name(native, second) == prefix + " Renamed" and _tools(native, second) == before_move, "Rename targets the current tab index and preserves tools", errors)
			dialog.hide()
		else:
			errors.append("Rename dialog is available")

		_tab_action(native, second, 1, errors)
		var tabs: Array = native.call("rome_get_custom_tabs")
		_expect(tabs.size() == original.size() + 3 and tabs[-1].tools == before_move, "Duplicate copies ordered shortcuts and dividers", errors)
		_expect(tabs[-1].name != tabs[second].name, "Duplicate gets a unique tab name", errors)
		var saved: Array = EditorInterface.get_editor_settings().get_setting("rome/studio/custom_tabs")
		_expect(var_to_str(saved) == var_to_str(tabs), "All contextual changes persist in editor preferences", errors)

	# Clean up by unique prefix even if any assertion above failed.
	var final_tabs: Array = native.call("rome_get_custom_tabs")
	for index in range(final_tabs.size() - 1, -1, -1):
		if str(final_tabs[index].name).begins_with(prefix):
			native.call("rome_remove_custom_tab", index)
	_expect(var_to_str(native.call("rome_get_custom_tabs")) == var_to_str(original), "Temporary tabs leave original preferences unchanged", errors)
	return errors

static func _expect(value: bool, label: String, errors: Array[String]) -> void:
	if not value:
		errors.append(label)

static func _tools(native: Node, index: int) -> PackedStringArray:
	var tabs: Array = native.call("rome_get_custom_tabs")
	return tabs[index].tools if index >= 0 and index < tabs.size() else PackedStringArray()

static func _name(native: Node, index: int) -> String:
	var tabs: Array = native.call("rome_get_custom_tabs")
	return str(tabs[index].name) if index >= 0 and index < tabs.size() else ""

static func _tab_action(native: Node, index: int, action: int, errors: Array[String]) -> void:
	var menu := native.find_child("ROMECustomTabMenu" + str(index), true, false) as PopupMenu
	if menu == null:
		errors.append("Context menu exists for tab " + str(index))
		return
	menu.emit_signal("id_pressed", action)

static func _tool_action(native: Node, tab: int, tool: int, action: int, errors: Array[String]) -> void:
	var page_menu := native.find_child("ROMECustomPageMenu" + str(tab), true, false) as PopupMenu
	var menu: PopupMenu
	if page_menu != null:
		menu = page_menu.get_parent().get_parent().find_child("ROMECustomToolMenu" + str(tool), true, false) as PopupMenu
	if menu == null:
		errors.append("Context menu exists for custom tool " + str(tool))
		return
	menu.emit_signal("id_pressed", action)
