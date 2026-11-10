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
var grid: ItemList
var view_toggle: Button
var location_row: HBoxContainer
var location: Label
var up: Button
var _view := "tree"
var _folder := "res://"
var _preview_generation := 0
var _previewed: Dictionary = {}
var rename_dialog: ConfirmationDialog
var rename_name: LineEdit
var rename_error: Label
var _rename_path := ""
var move_dialog: EditorFileDialog

const SCENES := ["tscn", "scn"]
const IMAGES := ["png", "jpg", "jpeg", "webp", "svg", "tga", "exr", "hdr"]
const AUDIO := ["wav", "ogg", "mp3"]
const MODELS := ["glb", "gltf", "fbx", "obj", "blend"]

func _init() -> void:
	name = "ROMEAssets"
	title = "Recursos"
	layout_key = "ROMEAssets"
	closable = true
	default_slot = EditorDock.DOCK_SLOT_LEFT_BL
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
	var scenes := MenuButton.new()
	scenes.name = "ROMECreateScene"
	scenes.text = "+"
	scenes.tooltip_text = "Crear una escena reutilizable 2D o 3D"
	scenes.get_popup().add_item("Escena 2D", 0)
	scenes.get_popup().add_item("Escena 3D", 1)
	scenes.get_popup().id_pressed.connect(func(id: int) -> void:
		EditorInterface.get_base_control().get_parent().call("rome_new_component_scene_with_dimension", "2d" if id == 0 else "3d")
	)
	actions.add_child(scenes)
	view_toggle = Button.new()
	view_toggle.name = "ROMEResourceViewToggle"
	view_toggle.toggle_mode = true
	view_toggle.tooltip_text = "Alternar árbol y miniaturas"
	view_toggle.toggled.connect(func(enabled: bool) -> void: set_view("grid" if enabled else "tree"))
	actions.add_child(view_toggle)
	search = LineEdit.new()
	search.placeholder_text = "Buscar recursos"
	search.clear_button_enabled = true
	layout.add_child(search)
	category = OptionButton.new()
	for label in ["Todos los recursos", "Escenas y modelos", "Imágenes", "Audio", "Otros"]:
		category.add_item(label)
	layout.add_child(category)
	location_row = HBoxContainer.new()
	location_row.name = "ROMEResourceLocation"
	location_row.visible = false
	layout.add_child(location_row)
	up = Button.new()
	up.name = "ROMEResourceUp"
	up.tooltip_text = "Subir una carpeta"
	up.pressed.connect(func() -> void: browse_folder("res://" + _folder.trim_prefix("res://").get_base_dir()))
	location_row.add_child(up)
	location = Label.new()
	location.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	location.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	location_row.add_child(location)
	files = Tree.new()
	files.name = "ROMEAssetList"
	files.add_theme_constant_override("v_separation", int(5 * EditorInterface.get_editor_scale()))
	files.hide_root = true
	files.allow_rmb_select = true
	files.size_flags_vertical = Control.SIZE_EXPAND_FILL
	files.custom_minimum_size = Vector2(200, 128) * EditorInterface.get_editor_scale()
	files.item_activated.connect(_open_selected)
	files.item_selected.connect(_selection_changed)
	files.item_mouse_selected.connect(_show_context)
	files.empty_clicked.connect(_empty_context)
	files.item_collapsed.connect(_folder_collapsed)
	files.set_drag_forwarding(_resource_drag, _can_drop_resource, _drop_resource)
	files.gui_input.connect(_resource_key)
	layout.add_child(files)
	grid = ItemList.new()
	grid.name = "ROMEResourceGrid"
	grid.visible = false
	grid.size_flags_vertical = Control.SIZE_EXPAND_FILL
	grid.custom_minimum_size = files.custom_minimum_size
	grid.icon_mode = ItemList.ICON_MODE_TOP
	grid.fixed_icon_size = Vector2i(64, 64) * EditorInterface.get_editor_scale()
	grid.fixed_column_width = int(94 * EditorInterface.get_editor_scale())
	grid.max_columns = 0
	grid.max_text_lines = 2
	grid.allow_rmb_select = true
	grid.item_selected.connect(_grid_selected)
	grid.item_activated.connect(func(_index: int) -> void: _open_selected())
	grid.item_clicked.connect(func(index: int, position: Vector2, button: int) -> void:
		_grid_selected(index)
		_show_context(position, button)
	)
	grid.empty_clicked.connect(_empty_context)
	grid.set_drag_forwarding(_resource_drag, _can_drop_resource, _drop_resource)
	grid.gui_input.connect(_resource_key)
	layout.add_child(grid)
	context = PopupMenu.new()
	context.name = "ROMEResourceContext"
	context.add_item("Nueva carpeta", 0)
	context.add_item("Abrir", 1)
	context.add_item("Cambiar nombre…", 3, KEY_F2)
	context.add_item("Mover a carpeta…", 4)
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
	hint.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	hint.tooltip_text = "Haz doble clic para abrir. Crea carpetas para organizar tus escenas y scripts."
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
	var theme := EditorInterface.get_editor_theme()
	view_toggle.icon = theme.get_icon("FileThumbnail", "EditorIcons")
	up.icon = theme.get_icon("ArrowUp", "EditorIcons")
	var settings := EditorInterface.get_editor_settings()
	_folder = str(settings.get_project_metadata("rome", "resource_folder", "res://"))
	if not _allowed_folder(_folder):
		_folder = "res://"
	_ensure_default_folders()
	var filesystem := EditorInterface.get_resource_filesystem()
	if not filesystem.filesystem_changed.is_connected(_queue_refresh):
		filesystem.filesystem_changed.connect(_queue_refresh)
	refresh()
	set_view(str(settings.get_project_metadata("rome", "resource_view", "tree")), false)

func _exit_tree() -> void:
	_preview_generation += 1
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
	hint.text = str(_count) + " recursos"
	if not search.text.is_empty() and _count == 0:
		hint.text = "No hay recursos que coincidan con la búsqueda."
	_refreshing = false
	_selection_changed()
	if _view == "grid":
		_refresh_grid()

func set_view(mode: String, persist := true) -> void:
	_view = "grid" if mode == "grid" else "tree"
	view_toggle.set_pressed_no_signal(_view == "grid")
	files.visible = _view == "tree"
	grid.visible = _view == "grid"
	location_row.visible = grid.visible
	_preview_generation += 1
	if persist:
		EditorInterface.get_editor_settings().set_project_metadata("rome", "resource_view", _view)
	if grid.visible:
		_refresh_grid()
	else:
		refresh()

func _allowed_folder(path: String) -> bool:
	return path.begins_with("res://") and not path.split("/").has("..") and path != "res://addons" and not path.begins_with("res://addons/") and path != "res://GodotLuau" and not path.begins_with("res://GodotLuau/") and DirAccess.dir_exists_absolute(path)

func browse_folder(path: String) -> void:
	if not _allowed_folder(path):
		return
	_folder = path.trim_suffix("/") if path != "res://" else path
	EditorInterface.get_editor_settings().set_project_metadata("rome", "resource_folder", _folder)
	files.deselect_all()
	_selection_changed()
	_refresh_grid()

func _refresh_grid() -> void:
	_preview_generation += 1
	_previewed.clear()
	grid.clear()
	if not _allowed_folder(_folder):
		_folder = "res://"
	location.text = "Recursos" if _folder == "res://" else _folder.trim_prefix("res://").replace("Scenes", _scene_folder_label())
	location.tooltip_text = _folder
	up.disabled = _folder == "res://"
	var parent := files.get_root() if _folder == "res://" else _find_item(files.get_root(), _folder)
	var items: Array[TreeItem] = []
	if not search.text.is_empty():
		_collect_grid_matches(files.get_root(), items)
	elif parent != null:
		items.assign(parent.get_children())
	var queued := 0
	var selected := _selected_path()
	for item in items:
		var path := str(item.get_metadata(0))
		var directory := bool(item.get_meta("rome_directory", false))
		var index := grid.add_item(item.get_text(0), item.get_icon(0))
		grid.set_item_metadata(index, path)
		grid.set_item_tooltip(index, path.trim_prefix("res://") + ("/" if directory else ""))
		grid.set_item_icon_modulate(index, item.get_icon_modulate(0))
		if selected == path:
			grid.select(index)
		# A bounded queue avoids starting a render for every asset in a large project.
		if not directory and queued < 32 and path.get_extension().to_lower() in SCENES + IMAGES + MODELS + ["tres", "res"]:
			queued += 1
			EditorInterface.get_resource_previewer().queue_resource_preview(path, self, "_grid_preview", {"generation": _preview_generation, "index": index})
	if items.is_empty():
		hint.text = "No hay recursos en esta carpeta." if search.text.is_empty() else "No hay recursos que coincidan con la búsqueda."
	else:
		hint.text = str(items.size()) + " elementos"

func _collect_grid_matches(item: TreeItem, result: Array[TreeItem]) -> void:
	for child in item.get_children():
		if child.get_meta("rome_directory", false):
			_collect_grid_matches(child, result)
		else:
			result.append(child)

func _grid_preview(path: String, preview: Texture2D, small: Texture2D, data: Dictionary) -> void:
	if not is_inside_tree() or _view != "grid" or int(data.generation) != _preview_generation:
		return
	var index := int(data.index)
	if index < 0 or index >= grid.item_count or str(grid.get_item_metadata(index)) != path:
		return
	var texture := preview if preview != null else small
	if texture != null:
		grid.set_item_icon(index, texture)
		grid.set_item_icon_modulate(index, Color.WHITE)
		_previewed[path] = true

func _grid_selected(index: int) -> void:
	if index < 0 or index >= grid.item_count:
		return
	var row := _find_item(files.get_root(), str(grid.get_item_metadata(index)))
	if row != null:
		row.select(0)
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
		item.set_icon_modulate(0, _resource_color(child_path, true))
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
		item.set_icon_modulate(0, _resource_color(file_path, false))
		if file_path == selected:
			item.select(0)
		_count += 1

## Semantic tints keep names readable while making types easy to distinguish.
func _resource_color(path: String, directory: bool) -> Color:
	if path.begins_with("res://Scripts/LocalScript"):
		return Color("65b8ff")
	if path.begins_with("res://Scripts/ServerScript"):
		return Color("ffa75f")
	if path.begins_with("res://Scripts/ModuleScript"):
		return Color("bc91ff")
	if directory:
		return Color("82c7ff") if path == "res://Scenes" else Color("dfc27a")
	var extension := path.get_extension().to_lower()
	if extension in SCENES:
		return Color("82c7ff")
	if extension in MODELS:
		return Color("73d4b3")
	if extension in IMAGES:
		return Color("eea1cf")
	if extension in AUDIO:
		return Color("e8c774")
	return Color("b8bdc9")

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
		if _view == "grid":
			browse_folder(path)
			return
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
	for action in [3, 4]:
		context.set_item_disabled(context.get_item_index(action), not _editable_path(_selected_path()))
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
		3: _prompt_rename()
		4: _prompt_move()

func _resource_key(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_F2:
		_prompt_rename()
		accept_event()

func _editable_path(path: String) -> bool:
	var clean := path.trim_suffix("/").simplify_path()
	return clean.begins_with("res://") and clean != "res://" and clean.trim_prefix("res://").get_slice("/", 0).to_lower() not in ["addons", "godotluau", "project.godot"] and not clean.trim_prefix("res://").begins_with(".")

func _prompt_rename() -> void:
	_rename_path = _selected_path()
	if not _editable_path(_rename_path):
		return
	if rename_dialog == null:
		rename_dialog = ConfirmationDialog.new()
		rename_dialog.title = "Cambiar nombre"
		rename_dialog.ok_button_text = "Renombrar"
		rename_dialog.dialog_hide_on_ok = false
		add_child(rename_dialog)
		var layout := VBoxContainer.new()
		rename_dialog.add_child(layout)
		rename_name = LineEdit.new()
		layout.add_child(rename_name)
		rename_error = Label.new()
		rename_error.modulate = Color("ed8188")
		layout.add_child(rename_error)
		rename_dialog.confirmed.connect(_confirm_rename)
		rename_name.text_submitted.connect(func(_text: String) -> void: _confirm_rename())
	rename_name.text = _rename_path.get_file()
	rename_error.text = ""
	rename_dialog.popup_centered(Vector2i(420, 120))
	rename_name.grab_focus()
	rename_name.select(0, -1 if DirAccess.dir_exists_absolute(_rename_path) else rename_name.text.rfind("."))

func _confirm_rename() -> void:
	var clean := rename_name.text.strip_edges()
	if clean != rename_name.text or clean.is_empty() or not clean.is_valid_filename() or clean.begins_with(".") or clean.ends_with("."):
		rename_error.text = "Escribe un nombre válido, sin rutas."
		return
	var result := relocate_resource(_rename_path, _rename_path.get_base_dir().path_join(clean))
	if result == OK:
		rename_dialog.hide()
	else:
		rename_error.text = _relocation_error(result)

func _prompt_move() -> void:
	_rename_path = _selected_path()
	if not _editable_path(_rename_path):
		return
	if move_dialog == null:
		move_dialog = EditorFileDialog.new()
		move_dialog.title = "Mover a carpeta"
		move_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_DIR
		move_dialog.access = EditorFileDialog.ACCESS_RESOURCES
		move_dialog.dir_selected.connect(func(destination: String) -> void:
			var result := relocate_resource(_rename_path, destination.path_join(_rename_path.get_file()))
			if result != OK:
				hint.text = _relocation_error(result)
		)
		add_child(move_dialog)
	move_dialog.current_dir = _rename_path.get_base_dir()
	move_dialog.popup_centered_ratio(0.6)

func _relocation_error(result: Error) -> String:
	if result == ERR_ALREADY_EXISTS:
		return "Ya existe un archivo o carpeta con ese nombre."
	if result == ERR_BUSY:
		return "Espera a que termine de actualizarse Recursos y vuelve a intentarlo."
	return "No se pudo mover. Revisa la ubicación, el nombre y los permisos."

func relocate_resource(source: String, destination: String) -> Error:
	var result: Error = EditorInterface.get_file_system_dock().call("rome_relocate_resource", source, destination)
	if result == OK:
		_folder_expansion[destination.get_base_dir()] = true
		if _folder == source or _folder.begins_with(source + "/"):
			_folder = destination + _folder.trim_prefix(source)
		hint.text = "Recurso organizado; referencias actualizadas."
		refresh_timer.start()
	return result

func _prompt_new_folder() -> void:
	_new_folder_parent = _selected_path()
	if _new_folder_parent.is_empty():
		_new_folder_parent = _folder if _view == "grid" else "res://"
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
	(grid if _view == "grid" else files).set_drag_preview(preview)
	return {"type": "files_and_dirs" if directory else "files", "files": PackedStringArray([path + "/" if directory else path])}

func _drop_folder(position: Vector2) -> String:
	var target := _folder if _view == "grid" else "res://"
	if _view == "grid":
		var index := grid.get_item_at_position(position, true)
		if index >= 0:
			target = str(grid.get_item_metadata(index))
	else:
		var item := files.get_item_at_position(position)
		if item != null:
			target = str(item.get_metadata(0))
	return target if DirAccess.dir_exists_absolute(target) else target.get_base_dir()

func _can_drop_resource(position: Vector2, data: Variant) -> bool:
	if not data is Dictionary or data.get("type", "") not in ["files", "files_and_dirs"] or data.get("files", []).size() != 1:
		return false
	var source := str(data.files[0]).trim_suffix("/")
	var target := _drop_folder(position)
	return _editable_path(source) and (target == "res://" or _editable_path(target)) and target != source and not target.begins_with(source + "/") and target != source.get_base_dir()

func _drop_resource(position: Vector2, data: Variant) -> void:
	if not _can_drop_resource(position, data):
		return
	var source := str(data.files[0]).trim_suffix("/")
	var result := relocate_resource(source, _drop_folder(position).path_join(source.get_file()))
	if result != OK:
		hint.text = _relocation_error(result)

func _insert_selected() -> void:
	var path := _selected_path()
	if not path.is_empty():
		plugin.call("insert_resource_scene", path)
