@tool
extends EditorDock

var plugin: EditorPlugin
var search: LineEdit
var category: OptionButton
var files: Tree
var hint: Label
var insert: Button
var refresh_timer: Timer
var folder_dialog: ConfirmationDialog
var folder_name: LineEdit
var folder_error: Label
var folder_location: Label
var context: PopupMenu
var _new_folder_parent := "res://"
var _folder_expansion: Dictionary = {}
var _refreshing := false
var _count := 0
var _scan_queued := false

const SCENES := ["tscn", "scn"]
const IMAGES := ["png", "jpg", "jpeg", "webp", "svg", "tga", "exr", "hdr"]
const AUDIO := ["wav", "ogg", "mp3"]
const MODELS := ["glb", "gltf", "fbx", "obj", "blend"]

func _init() -> void:
	name = "ROMEAssets"
	title = "Recursos"
	layout_key = "ROMEAssets"
	closable = true
	default_slot = EditorDock.DOCK_SLOT_BOTTOM
	available_layouts = EditorDock.DOCK_LAYOUT_ALL
	var layout := VBoxContainer.new()
	add_child(layout)
	var actions := HBoxContainer.new()
	layout.add_child(actions)
	var create := Button.new()
	create.name = "ROMENewFolder"
	create.text = "Nueva carpeta"
	create.tooltip_text = "Crear una carpeta dentro de la selección para organizar tus recursos"
	create.pressed.connect(_prompt_new_folder)
	actions.add_child(create)
	var browse := Button.new()
	browse.text = "Archivos"
	browse.tooltip_text = "Abrir todos los archivos del proyecto para importar y organizar recursos"
	browse.pressed.connect(_show_in_filesystem)
	actions.add_child(browse)
	search = LineEdit.new()
	search.placeholder_text = "Buscar recursos"
	search.clear_button_enabled = true
	layout.add_child(search)
	category = OptionButton.new()
	for label in ["Todos los recursos", "Escenas y modelos", "Imágenes", "Audio", "Otros"]:
		category.add_item(label)
	layout.add_child(category)
	files = Tree.new()
	files.name = "ROMEAssetList"
	files.hide_root = true
	files.allow_rmb_select = true
	files.size_flags_vertical = Control.SIZE_EXPAND_FILL
	files.custom_minimum_size = Vector2(200, 220)
	files.item_activated.connect(_open_selected)
	files.item_selected.connect(_selection_changed)
	files.item_mouse_selected.connect(_show_context)
	files.empty_clicked.connect(_empty_context)
	files.item_collapsed.connect(_folder_collapsed)
	files.set_drag_forwarding(_resource_drag, _can_drop_resource, _drop_resource)
	layout.add_child(files)
	context = PopupMenu.new()
	context.name = "ROMEResourceContext"
	context.add_item("Nueva carpeta", 0)
	context.add_item("Abrir", 1)
	context.add_separator()
	context.add_item("Organizar en Archivos", 2)
	context.id_pressed.connect(_context_action)
	add_child(context)
	var row := HBoxContainer.new()
	layout.add_child(row)
	var open := Button.new()
	open.text = "Abrir"
	open.pressed.connect(_open_selected)
	row.add_child(open)
	insert = Button.new()
	insert.text = "Insertar en escena"
	insert.disabled = true
	insert.pressed.connect(_insert_selected)
	row.add_child(insert)
	hint = Label.new()
	hint.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	hint.add_theme_font_size_override("font_size", 11)
	hint.modulate = Color("a3a6b0")
	layout.add_child(hint)
	refresh_timer = Timer.new()
	refresh_timer.one_shot = true
	refresh_timer.wait_time = 0.15
	refresh_timer.timeout.connect(refresh)
	add_child(refresh_timer)
	search.text_changed.connect(func(_value: String) -> void: refresh_timer.start())
	category.item_selected.connect(func(_value: int) -> void: refresh())

func _ready() -> void:
	_ensure_default_folders()
	var filesystem := EditorInterface.get_resource_filesystem()
	if not filesystem.filesystem_changed.is_connected(_queue_refresh):
		filesystem.filesystem_changed.connect(_queue_refresh)
	refresh()

func _exit_tree() -> void:
	var filesystem := EditorInterface.get_resource_filesystem()
	if filesystem.filesystem_changed.is_connected(_queue_refresh):
		filesystem.filesystem_changed.disconnect(_queue_refresh)

func _queue_refresh() -> void:
	refresh_timer.start()

func refresh() -> void:
	var selected := _selected_path()
	_refreshing = true
	files.clear()
	var tree_root := files.create_item()
	_count = 0
	_populate("res://", tree_root, selected)
	hint.text = "Haz doble clic para abrir. Crea carpetas para organizar tus escenas y scripts."
	if not search.text.is_empty() and _count == 0:
		hint.text = "No hay recursos que coincidan con la búsqueda."
	_refreshing = false
	_selection_changed()

func _ensure_default_folders() -> void:
	var changed := false
	for path in ["res://Scenes", "res://Scripts/LocalScript", "res://Scripts/ServerScript", "res://Scripts/ModuleScript"]:
		if not DirAccess.dir_exists_absolute(path):
			if DirAccess.make_dir_recursive_absolute(path) == OK:
				changed = true
	if changed:
		call_deferred("_request_rescan")

func _request_rescan() -> void:
	if _scan_queued:
		return
	_scan_queued = true
	# Plugin registration happens during the first filesystem scan. Never start
	# a nested scan in _ready(), and batch folders/scripts created in one frame.
	await get_tree().process_frame
	await get_tree().process_frame
	var filesystem := EditorInterface.get_resource_filesystem()
	while filesystem.is_scanning():
		await get_tree().create_timer(0.15).timeout
	filesystem.scan()
	_scan_queued = false

func _scene_folder_label() -> String:
	# A localized display alias preserves scene references when the editor language changes.
	return str(TranslationServer.get_or_add_domain("godot.properties").translate("Scenes"))

func _ordered_folders(path: String, directory: DirAccess) -> Array[String]:
	var names: Array[String] = []
	names.assign(directory.get_directories())
	names.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	var ordered: Array[String] = []
	var fixed: Array[String] = []
	if path == "res://":
		fixed.assign(["Scenes", "Scripts"])
	elif path == "res://Scripts":
		fixed.assign(["LocalScript", "ServerScript", "ModuleScript"])
	for folder in fixed:
		if names.has(folder):
			ordered.append(folder)
			names.erase(folder)
	ordered.append_array(names)
	return ordered

func _populate(path: String, parent: TreeItem, selected: String) -> void:
	var directory := DirAccess.open(path)
	if directory == null:
		return
	for folder in _ordered_folders(path, directory):
		if folder.begins_with(".") or (path == "res://" and folder in ["GodotLuau", "addons"]):
			continue
		var child_path := path.path_join(folder)
		var item := files.create_item(parent)
		item.set_text(0, _scene_folder_label() if child_path == "res://Scenes" else folder)
		item.set_icon(0, EditorInterface.get_editor_theme().get_icon("Folder", "EditorIcons"))
		item.set_tooltip_text(0, child_path.trim_prefix("res://") + "/")
		item.set_metadata(0, child_path)
		item.set_meta("rome_directory", true)
		_populate(child_path, item, selected)
		var fixed := child_path in ["res://Scenes", "res://Scripts", "res://Scripts/LocalScript", "res://Scripts/ServerScript", "res://Scripts/ModuleScript"]
		if not search.text.is_empty() and not fixed and item.get_child_count() == 0 and not child_path.to_lower().contains(search.text.to_lower()):
			item.free()
			continue
		item.collapsed = search.text.is_empty() and not bool(_folder_expansion.get(child_path, true)) and not selected.begins_with(child_path + "/")
		if child_path == selected:
			item.select(0)
	var filenames := directory.get_files()
	filenames.sort()
	for file in filenames:
		var file_path := path.path_join(file)
		if not _matches_file(file_path):
			continue
		var item := files.create_item(parent)
		item.set_text(0, file)
		item.set_tooltip_text(0, file_path.trim_prefix("res://"))
		item.set_metadata(0, file_path)
		var extension := file.get_extension().to_lower()
		var scene := extension in SCENES or extension in MODELS
		var icon := "PackedScene" if scene else "ImageTexture" if extension in IMAGES else "AudioStreamWAV" if extension in AUDIO else "Script" if extension in ["lua", "luau"] else "Object"
		item.set_icon(0, EditorInterface.get_editor_theme().get_icon(icon, "EditorIcons"))
		if file_path == selected:
			item.select(0)
		_count += 1

func _matches_file(path: String) -> bool:
	var extension := path.get_extension().to_lower()
	if extension not in SCENES + IMAGES + AUDIO + MODELS + ["tres", "res", "gdshader", "gdshaderinc", "luau", "lua"]:
		return false
	var scene := extension in SCENES or extension in MODELS
	if category.selected == 1 and not scene:
		return false
	if category.selected == 2 and extension not in IMAGES:
		return false
	if category.selected == 3 and extension not in AUDIO:
		return false
	if category.selected == 4 and (scene or extension in IMAGES or extension in AUDIO):
		return false
	return search.text.is_empty() or path.to_lower().contains(search.text.to_lower())

func _folder_collapsed(item: TreeItem) -> void:
	if not _refreshing and search.text.is_empty() and item.has_meta("rome_directory"):
		_folder_expansion[str(item.get_metadata(0))] = not item.collapsed

func _selected_path() -> String:
	var item := files.get_selected() if files != null else null
	return str(item.get_metadata(0)) if item != null else ""

func _selection_changed() -> void:
	var path := _selected_path()
	var root := EditorInterface.get_edited_scene_root()
	var main := str(ProjectSettings.get_setting("application/run/main_scene", ""))
	if main.begins_with("uid://"):
		main = ResourceUID.get_id_path(ResourceUID.text_to_id(main))
	insert.disabled = path.is_empty() or path.get_extension().to_lower() not in SCENES + MODELS or root == null or path == root.scene_file_path or path == main

func _open_selected() -> void:
	var path := _selected_path()
	if path.is_empty():
		return
	if DirAccess.dir_exists_absolute(path):
		var item := files.get_selected()
		item.collapsed = not item.collapsed
		return
	if path.get_extension().to_lower() in SCENES:
		EditorInterface.open_scene_from_path(path)
	elif ResourceLoader.exists(path):
		EditorInterface.edit_resource(load(path))

func _show_in_filesystem() -> void:
	var path := _selected_path()
	if path.is_empty():
		path = "res://"
	if DirAccess.dir_exists_absolute(path) and not path.ends_with("/"):
		path += "/"
	var browser := EditorInterface.get_file_system_dock()
	browser.navigate_to_path(path)
	browser.make_visible()

func _show_context(_position: Vector2, button: int) -> void:
	if button != MOUSE_BUTTON_RIGHT:
		return
	context.set_item_disabled(context.get_item_index(1), _selected_path().is_empty())
	context.position = DisplayServer.mouse_get_position()
	context.popup()

func _empty_context(position: Vector2, button: int) -> void:
	if button == MOUSE_BUTTON_RIGHT:
		files.deselect_all()
		_show_context(position, button)

func _context_action(action: int) -> void:
	match action:
		0: _prompt_new_folder()
		1: _open_selected()
		2: _show_in_filesystem()

func _prompt_new_folder() -> void:
	_new_folder_parent = _selected_path()
	if _new_folder_parent.is_empty():
		_new_folder_parent = "res://"
	elif not DirAccess.dir_exists_absolute(_new_folder_parent):
		_new_folder_parent = _new_folder_parent.get_base_dir()
	if folder_dialog == null:
		folder_dialog = ConfirmationDialog.new()
		folder_dialog.name = "ROMENewFolderDialog"
		folder_dialog.title = "Nueva carpeta"
		folder_dialog.ok_button_text = "Crear"
		folder_dialog.dialog_hide_on_ok = false
		add_child(folder_dialog)
		var layout := VBoxContainer.new()
		folder_dialog.add_child(layout)
		folder_location = Label.new()
		folder_location.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		layout.add_child(folder_location)
		folder_name = LineEdit.new()
		folder_name.name = "ROMEFolderName"
		folder_name.placeholder_text = "Nombre de la carpeta"
		folder_name.text_submitted.connect(func(_value: String) -> void: _confirm_new_folder())
		layout.add_child(folder_name)
		folder_error = Label.new()
		folder_error.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		folder_error.modulate = Color("ed8188")
		layout.add_child(folder_error)
		folder_dialog.confirmed.connect(_confirm_new_folder)
	folder_location.text = "Ubicación: " + _new_folder_parent.trim_prefix("res://")
	folder_name.text = ""
	folder_error.text = ""
	folder_dialog.popup_centered(Vector2i(360, 140))
	folder_name.grab_focus()

func _confirm_new_folder() -> void:
	var result := create_folder(folder_name.text, _new_folder_parent)
	if result != OK:
		folder_error.text = "Ya existe un archivo o carpeta con ese nombre." if result == ERR_ALREADY_EXISTS else "Usa un nombre válido, sin rutas ni caracteres especiales." if result == ERR_INVALID_PARAMETER else "No se pudo crear la carpeta en esta ubicación."
		return
	folder_dialog.hide()

func create_folder(new_name: String, parent_path: String = "res://") -> Error:
	var clean := new_name.strip_edges()
	var reserved := ["CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"]
	if clean.is_empty() or clean != new_name or clean in [".", ".."] or not clean.is_valid_filename() or clean.ends_with(".") or clean.begins_with(".") or clean.get_basename().to_upper() in reserved:
		return ERR_INVALID_PARAMETER
	var parent := parent_path.simplify_path().trim_suffix("/")
	if parent_path == "res://":
		parent = "res://"
	if not parent.begins_with("res://") or parent == "res://GodotLuau" or parent.begins_with("res://GodotLuau/") or parent == "res://addons" or parent.begins_with("res://addons/") or not DirAccess.dir_exists_absolute(parent):
		return ERR_INVALID_PARAMETER
	var path := parent.path_join(clean)
	if DirAccess.dir_exists_absolute(path) or FileAccess.file_exists(path):
		return ERR_ALREADY_EXISTS
	var result := DirAccess.make_dir_absolute(path)
	if result == OK:
		_folder_expansion[parent] = true
		_folder_expansion[path] = true
		call_deferred("_request_rescan")
		refresh()
		var item := _find_item(files.get_root(), path)
		if item != null:
			item.select(0)
			files.scroll_to_item(item)
	return result

func _find_item(item: TreeItem, path: String) -> TreeItem:
	if item == null:
		return null
	if str(item.get_metadata(0)) == path:
		return item
	for child in item.get_children():
		var found := _find_item(child, path)
		if found != null:
			return found
	return null

func _resource_drag(_position: Vector2) -> Variant:
	var path := _selected_path()
	if path.is_empty():
		return null
	var directory := DirAccess.dir_exists_absolute(path)
	var preview := Label.new()
	preview.text = path.get_file()
	files.set_drag_preview(preview)
	return {"type": "files_and_dirs" if directory else "files", "files": PackedStringArray([path + "/" if directory else path])}

func _can_drop_resource(_position: Vector2, _data: Variant) -> bool:
	return false

func _drop_resource(_position: Vector2, _data: Variant) -> void:
	pass

func _insert_selected() -> void:
	var path := _selected_path()
	var root := EditorInterface.get_edited_scene_root()
	if path.is_empty() or root == null or path == root.scene_file_path:
		return
	var packed := load(path) as PackedScene
	if packed == null or not packed.can_instantiate():
		return
	if packed.get_state().get_node_type(0) in ["Game3D", "RobloxGame3D"]:
		return
	var parent := root.get_node_or_null("Workspace") if root.is_class("RobloxGame3D") else root
	var selection := EditorInterface.get_selection().get_selected_nodes()
	if not selection.is_empty() and selection[0] != root:
		parent = selection[0]
	if parent == null:
		return
	var instance := packed.instantiate(PackedScene.GEN_EDIT_STATE_INSTANCE)
	var workspace := root.get_node_or_null("Workspace")
	if instance is Node3D and workspace != null and (parent == workspace or workspace.is_ancestor_of(parent)):
		preload("res://addons/ROMEStudio/studio_placement.gd").place(instance as Node3D, parent, root)
	var undo := plugin.get_undo_redo()
	undo.create_action("Insertar " + path.get_file(), UndoRedo.MERGE_DISABLE, root)
	undo.add_do_method(parent, "add_child", instance, true)
	undo.add_do_method(instance, "set_owner", root)
	undo.add_do_reference(instance)
	undo.add_undo_method(parent, "remove_child", instance)
	undo.commit_action()
	EditorInterface.get_selection().clear()
	EditorInterface.get_selection().add_node(instance)
	EditorInterface.edit_node(instance)
