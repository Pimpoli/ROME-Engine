@tool
extends ConfirmationDialog

## "Insertar objeto" as in Roblox Studio: search, objects grouped by category
## and the recently used ones first. Double click or Enter inserts the object
## into the selected parent.

signal object_chosen(title: String)

const Catalog = preload("res://addons/ROMEStudio/studio_catalog.gd")
const RECENT_TITLE := "Usados recientemente"
const RECENT_LIMIT := 8

var search: LineEdit
var list: Tree
var destination: Label
var describe: Callable
var _folded: Dictionary = {}
var _refreshing := false

func _init() -> void:
	name = "ROMEInsertObject"
	title = "Insertar objeto"
	ok_button_text = "Insertar"
	cancel_button_text = "Cerrar"
	var layout := VBoxContainer.new()
	add_child(layout)
	search = LineEdit.new()
	search.name = "ROMEInsertSearch"
	search.placeholder_text = "Buscar objeto"
	search.clear_button_enabled = true
	search.text_changed.connect(func(_text: String) -> void: refresh())
	search.text_submitted.connect(func(_text: String) -> void: choose())
	search.gui_input.connect(_search_input)
	layout.add_child(search)
	list = Tree.new()
	list.name = "ROMEInsertList"
	list.hide_root = true
	list.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	list.custom_minimum_size = Vector2(0, 380)
	list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	list.item_activated.connect(choose)
	list.item_selected.connect(_selection_changed)
	list.item_collapsed.connect(_category_collapsed)
	layout.add_child(list)
	destination = Label.new()
	destination.modulate = Color("a3a6b0")
	destination.add_theme_font_size_override("font_size", 11)
	destination.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	layout.add_child(destination)
	confirmed.connect(choose)

func open() -> void:
	search.text = ""
	refresh()
	popup_centered(Vector2i(420, 560))
	search.grab_focus()

func refresh() -> void:
	_refreshing = true
	list.clear()
	var tree_root := list.create_item()
	var query := search.text.strip_edges()
	var scene := EditorInterface.get_edited_scene_root()
	var is_2d := scene != null and scene.is_class("RobloxGame2D")
	var first: TreeItem
	if query.is_empty():
		var section: TreeItem
		for title in recent_objects():
			if Catalog.entries().has(title) and _available(title, is_2d):
				if section == null:
					section = _category(tree_root, RECENT_TITLE)
				var item := _object(section, title)
				first = item if first == null else first
	for category: Array in Catalog.CATEGORIES:
		var section: TreeItem
		var whole: bool = not query.is_empty() and str(category[0]).containsn(query)
		for title: String in category[2]:
			if not _available(title, is_2d) or not (query.is_empty() or whole or title.containsn(query)):
				continue
			if section == null:
				section = _category(tree_root, category[0])
			var item := _object(section, title)
			first = item if first == null else first
		if section != null and query.is_empty():
			section.collapsed = _folded.get(category[0], category[0] != "Básico")
	_refreshing = false
	if first != null:
		first.select(0)
		list.scroll_to_item(first)
	_selection_changed()

func choose() -> void:
	var item := list.get_selected()
	if item == null or item.get_metadata(0) == null:
		return
	hide()
	object_chosen.emit(str(item.get_metadata(0)))

## Selects an object by name, as typing it and pressing Enter would.
func select_object(title: String) -> bool:
	search.text = title
	refresh()
	var item := _find(list.get_root(), title)
	if item != null:
		item.select(0)
	return item != null

static func recent_objects() -> Array:
	var stored: Variant = EditorInterface.get_editor_settings().get_project_metadata("rome", "recent_objects", [])
	return stored if stored is Array else []

static func remember(title: String) -> void:
	var recent := recent_objects().duplicate()
	recent.erase(title)
	recent.push_front(title)
	EditorInterface.get_editor_settings().set_project_metadata("rome", "recent_objects", recent.slice(0, RECENT_LIMIT))

func _available(title: String, is_2d: bool) -> bool:
	return (is_2d or not title in Catalog.ONLY_2D) and ClassDB.can_instantiate(Catalog.entries()[title].type)

func _category(parent: TreeItem, title: String) -> TreeItem:
	var item := list.create_item(parent)
	item.set_text(0, title)
	item.set_selectable(0, false)
	item.set_custom_font(0, EditorInterface.get_editor_theme().get_font("bold", "EditorFonts"))
	item.set_custom_bg_color(0, Color("25262d"))
	return item

func _object(parent: TreeItem, title: String) -> TreeItem:
	var item := list.create_item(parent)
	var entry: Dictionary = Catalog.entries()[title]
	item.set_text(0, title)
	item.set_icon(0, Catalog.icon_for(title))
	item.set_icon_max_width(0, int(16 * EditorInterface.get_editor_scale()))
	item.set_metadata(0, title)
	item.set_tooltip_text(0, title if entry.type == title else title + " (" + entry.type + ")")
	return item

func _find(item: TreeItem, title: String) -> TreeItem:
	if item == null:
		return null
	if item.get_metadata(0) == title:
		return item
	for child in item.get_children():
		var found := _find(child, title)
		if found != null:
			return found
	return null

func _selection_changed() -> void:
	var item := list.get_selected()
	if item == null or item.get_metadata(0) == null or not describe.is_valid():
		destination.text = ""
		return
	destination.text = "Se insertará en: " + str(describe.call(str(item.get_metadata(0))))

func _category_collapsed(item: TreeItem) -> void:
	if not _refreshing and search.text.strip_edges().is_empty() and item.get_metadata(0) == null:
		_folded[item.get_text(0)] = item.collapsed

func _search_input(event: InputEvent) -> void:
	if event.is_action_pressed("ui_down") or event.is_action_pressed("ui_up"):
		list.grab_focus()
		search.accept_event()
