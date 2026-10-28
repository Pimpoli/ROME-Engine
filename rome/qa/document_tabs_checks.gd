extends RefCounted
## Run only inside a disposable ROME editor fixture. Each test creates its own
## scripts and reusable scene; existing documents are not closed or overwritten.

static func run(root: Node) -> Dictionary:
	var report := {"checks": [], "errors": []}
	var candidates := EditorInterface.get_base_control().find_children("*", "EditorSceneTabs", true, false)
	if candidates.is_empty() or not candidates[0].has_method("rome_get_documents"):
		report.errors.append("Unified document tab API is available")
		return report
	var tabs: Node = candidates[0]
	var scripts := EditorInterface.get_script_editor()
	var prefix := "DocQA" + str(Time.get_ticks_usec())
	var source := "-- ROME document integration fixture\nlocal marker = 1\nlocal another = 2\n"
	var local_node := _make_script(root, "LocalScript", prefix + "Local", source)
	var server_node := _make_script(root, "ServerScript", prefix + "Server", source)
	if not _expect(local_node != null and server_node != null, "Creates two real Luau script nodes", report):
		return report
	await _frames(4)
	var local_script: Script = local_node.call("get_codigo_luau")
	var server_script: Script = server_node.call("get_codigo_luau")
	var main_path := root.scene_file_path
	EditorInterface.edit_resource(local_script)
	await _frames(4)
	EditorInterface.edit_resource(server_script)
	await _frames(4)
	var documents: Array = tabs.call("rome_get_documents")
	_expect(_index(documents, "scene", main_path) >= 0, "Main scene remains in the shared document bar", report)
	var local_index := _index(documents, "script", local_script.resource_path)
	var server_index := _index(documents, "script", server_script.resource_path)
	if not _expect(local_index >= 0 and server_index >= 0, "Both Luau scripts appear beside scenes", report):
		return report
	_expect(str(documents[local_index].label) == str(local_node.name) and documents[local_index].type == "LocalScript" and documents[local_index].has_icon, "LocalScript tab shows its logical name with its type icon", report)
	_expect(str(documents[server_index].label) == str(server_node.name) and documents[server_index].type == "ServerScript" and documents[server_index].has_icon, "ServerScript tab shows its logical name with its type icon", report)
	tabs.call("rome_select_document", local_index)
	await _frames(4)
	_expect(scripts.get_current_script() == local_script and scripts.is_visible_in_tree(), "Selecting a script opens its exact code document", report)
	var editor := scripts.get_current_editor()
	var code := editor.get_base_editor() as CodeEdit if editor != null else null
	if not _expect(code != null, "Luau document provides an editable code surface", report):
		return report
	code.set_caret_line(2)
	code.set_caret_column(5)
	tabs.call("rome_select_document", _index(tabs.call("rome_get_documents"), "scene", main_path))
	await _frames(4)
	_expect(EditorInterface.get_edited_scene_root() == root and not scripts.is_visible_in_tree(), "Selecting the current scene directly returns to the world", report)
	tabs.call("rome_select_document", _index(tabs.call("rome_get_documents"), "script", local_script.resource_path))
	await _frames(4)
	_expect(scripts.get_current_script() == local_script and scripts.get_current_editor() == editor, "Returning to a script reuses the same editor and resource", report)
	_expect(code.get_caret_line() == 2 and code.get_caret_column() == 5, "Scene switching preserves the script caret", report)

	# A separately named scene must coexist with scripts and switch the Explorer.
	var native := EditorInterface.get_base_control().get_parent()
	native.call("rome_new_component_scene")
	await _frames(4)
	var reusable := EditorInterface.get_edited_scene_root()
	var scene_path := "res://Scenes/" + prefix + "Scene.tscn"
	EditorInterface.save_scene_as(scene_path, false)
	await _frames(4)
	documents = tabs.call("rome_get_documents")
	_expect(_index(documents, "scene", scene_path) >= 0 and _index(documents, "script", local_script.resource_path) >= 0, "Reusable scenes and scripts coexist in the same bar", report)
	tabs.call("rome_select_document", _index(documents, "scene", main_path))
	await _frames(4)
	_expect(EditorInterface.get_edited_scene_root() == root, "Selecting the main scene restores its scene tree", report)
	tabs.call("rome_select_document", _index(tabs.call("rome_get_documents"), "scene", scene_path))
	await _frames(4)
	_expect(EditorInterface.get_edited_scene_root() == reusable and not scripts.is_visible_in_tree(), "Selecting a reusable scene shows its own scene tree and viewport", report)
	tabs.call("rome_select_document", _index(tabs.call("rome_get_documents"), "scene", main_path))
	await _frames(4)
	var reusable_index := _index(tabs.call("rome_get_documents"), "scene", scene_path)
	if reusable_index >= 0:
		tabs.call("rome_close_document", reusable_index)
	await _frames(4)

	# Cancelling a native unsaved-document prompt must preserve the code. Saving
	# from a second prompt must write the edited source before the tab disappears.
	tabs.call("rome_select_document", _index(tabs.call("rome_get_documents"), "script", local_script.resource_path))
	await _frames(4)
	code = scripts.get_current_editor().get_base_editor() as CodeEdit
	var pending := "-- " + prefix + " pending edit"
	code.set_caret_line(code.get_line_count() - 1)
	code.set_caret_column(code.get_line(code.get_line_count() - 1).length())
	code.insert_text_at_caret("\n" + pending)
	await _frames(3)
	documents = tabs.call("rome_get_documents")
	local_index = _index(documents, "script", local_script.resource_path)
	_expect(local_index >= 0 and str(documents[local_index].label).ends_with("(*)"), "Unsaved script tabs show a dirty marker", report)
	tabs.call("rome_close_document", local_index)
	await _frames(3)
	var prompt := _close_prompt(scripts, prefix)
	if _expect(prompt != null, "Closing an edited script asks before saving or discarding", report):
		_expect(scripts.get_open_scripts().has(local_script), "Unsaved script stays open while confirmation is pending", report)
		prompt.emit_signal("canceled")
		prompt.hide()
		await _frames(3)
		_expect(scripts.get_open_scripts().has(local_script) and code.text.contains(pending), "Cancel preserves the open document and unsaved text", report)
		tabs.call("rome_close_document", _index(tabs.call("rome_get_documents"), "script", local_script.resource_path))
		await _frames(3)
		prompt = _close_prompt(scripts, prefix)
		if prompt != null:
			prompt.emit_signal("confirmed")
			prompt.hide()
			await _frames(6)
			_expect(FileAccess.get_file_as_string(local_script.resource_path).contains(pending), "Save confirmation writes pending Luau edits", report)
			_expect(not scripts.get_open_scripts().has(local_script), "Saved script closes after confirmation", report)
			_expect(not scripts.is_visible_in_tree() and EditorInterface.get_edited_scene_root() == root, "Closing the active script returns directly to the world", report)
			_expect(scripts.get_open_scripts().has(server_script), "Closing one script leaves other script tabs open", report)
		else:
			report.errors.append("Save prompt reopens after cancelling")

	# Close only this helper's remaining clean script, retaining all pre-existing
	# editor documents. Fixture resources remain on disk for inspection.
	server_index = _index(tabs.call("rome_get_documents"), "script", server_script.resource_path)
	if server_index >= 0:
		tabs.call("rome_close_document", server_index)
	await _frames(4)
	return report

static func _make_script(root: Node, type: String, title: String, source: String) -> Node:
	var parent: Node = root.get_node_or_null("StarterPlayer/StarterPlayerScripts") if type == "LocalScript" else root.get_node_or_null("ServerScriptService")
	if parent == null:
		return null
	var folder := "res://Scripts/" + type
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(folder))
	var path := folder.path_join(title + ".lua")
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return null
	file.store_string(source)
	file.close()
	var script: Script = ClassDB.instantiate("LuauScript")
	script.source_code = source
	script.resource_name = title
	script.set_meta("rome_script_name", title)
	script.set_meta("rome_script_type", type)
	script.take_over_path(path)
	var node: Node = ClassDB.instantiate(type)
	node.name = title
	node.set("codigo_luau", script)
	parent.add_child(node)
	node.owner = root
	return node

static func _index(documents: Array, kind: String, path: String) -> int:
	for index in documents.size():
		if documents[index].kind == kind and documents[index].path == path:
			return index
	return -1

static func _expect(value: bool, label: String, report: Dictionary) -> bool:
	if value:
		report.checks.append(label)
	else:
		report.errors.append(label)
	return value

static func _close_prompt(scripts: Node, prefix: String) -> ConfirmationDialog:
	for dialog: ConfirmationDialog in scripts.find_children("*", "ConfirmationDialog", true, false):
		if dialog.visible and dialog.dialog_text.contains(prefix):
			return dialog
	return null

static func _frames(count: int) -> void:
	var tree := EditorInterface.get_base_control().get_tree()
	for frame in count:
		await tree.process_frame
