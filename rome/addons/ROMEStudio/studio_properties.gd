@tool
extends EditorProperty

## Roblox Studio style properties: collapsible sections, Name/ClassName/Parent
## under "Datos" and the GodotLuau attributes (node metadata) at the end.
## Values go through the Inspector, so every edit has Undo/Redo.

const Catalog = preload("res://addons/ROMEStudio/studio_catalog.gd")
const SECTIONS := [
	["Apariencia", ["Color", "BrickColor", "Material", "MaterialVariant", "Reflectance", "Transparency", "CastShadow", "DitherTransparency", "TextureScale",
		"BackgroundColor3", "BackgroundTransparency", "BorderColor3", "BorderMode", "BorderSizePixel", "Visible", "Style", "Brightness", "Range",
		"Angle", "Face", "Shadows", "LightEmission", "LightInfluence", "AlwaysOnTop"]],
	["Texto", ["Text", "TextSize", "TextColor3", "TextTransparency", "TextStrokeColor3", "TextStrokeTransparency", "TextXAlignment", "TextYAlignment",
		"TextWrapped", "TextTruncate", "TextScaled", "LineHeight", "RichText", "MaxVisibleGraphemes", "RobloxTextDirection", "FontFace",
		"PlaceholderText", "PlaceholderColor3", "ClearTextOnFocus", "MultiLine"]],
	["Imagen", ["Image", "ImageColor3", "ImageTransparency", "ScaleType", "SliceScale", "SliceCenter", "TileSize", "ImageRectOffset", "ImageRectSize", "ResampleMode"]],
	["Colisión", ["CanCollide", "CanTouch", "CanQuery", "CollisionGroup"]],
	["Comportamiento", ["Anchored", "Locked", "Enabled", "Disabled", "Archivable", "Active", "Interactable", "Selectable", "Draggable", "ClipsDescendants",
		"AutoButtonColor", "Modal", "Selected", "ResetOnSpawn", "IgnoreGuiInset", "DisplayOrder", "ZIndexBehavior", "Looped", "Playing", "PlayOnRemove",
		"AutoRotate", "AutoJumpEnabled", "RequiresHandle", "CanBeDropped"]],
	["Pieza", ["Shape", "MeshId", "TextureId", "MeshSize", "RenderFidelity", "CollisionFidelity", "DoubleSided"]],
	["Transformar", ["Size", "Position", "position", "rotation", "Orientation", "AnchorPoint", "Rotation", "SizeConstraint", "AutomaticSize",
		"LayoutOrder", "ZIndex", "ScaleFactor"]],
	["Física", ["Massless", "Density", "Friction", "Elasticity", "FrictionWeight", "ElasticityWeight", "RootPriority"]],
	["Superficie", ["TopSurface", "BottomSurface", "FrontSurface", "BackSurface", "LeftSurface", "RightSurface"]],
]
const FOLDED := ["Física", "Superficie"]
const LABELS := {"name": "Name", "position": "Position", "rotation": "Orientation"}
const EDITABLE := [TYPE_BOOL, TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_STRING_NAME, TYPE_VECTOR2, TYPE_VECTOR3, TYPE_VECTOR4, TYPE_RECT2, TYPE_COLOR]
# Types offered by "Agregar atributo", with the value GodotLuau stores for each.
const ATTRIBUTE_TYPES := {"string": "", "number": 0.0, "bool": false, "Color3": Color(1, 1, 1), "Vector3": Vector3.ZERO}
const HEADER := Color("25262d")
const REMOVE := 0

static var folded: Dictionary = {}

var target: Node
var filter := ""
var shown: Dictionary = {}
var table: Tree
var add_button: Button
var rows: Dictionary = {}
var sections: Dictionary = {}
var picker: ColorPicker
var color_popup: PopupPanel
var color_property := ""
var attribute_dialog: ConfirmationDialog
var attribute_name: LineEdit
var attribute_type: OptionButton
var attribute_error: Label
var refreshing := false

func _init(object: Node, filter_text := "") -> void:
	name = "ROMEStudioProperties"
	target = object
	filter = filter_text.strip_edges()
	var layout := VBoxContainer.new()
	add_child(layout)
	set_bottom_editor(layout)
	table = Tree.new()
	table.name = "ROMEPropertyGrid"
	table.columns = 2
	table.hide_root = true
	table.select_mode = Tree.SELECT_SINGLE
	table.scroll_vertical_enabled = false
	table.set_column_expand_ratio(0, 1)
	table.set_column_expand_ratio(1, 1)
	table.set_column_clip_content(0, true)
	table.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	# Property names are API names: Studio keeps them in English.
	table.auto_translate_mode = Node.AUTO_TRANSLATE_MODE_DISABLED
	table.add_theme_constant_override("v_separation", 4)
	table.add_theme_constant_override("item_margin", int(10 * EditorInterface.get_editor_scale()))
	layout.add_child(table)
	add_button = Button.new()
	add_button.name = "ROMEAddAttribute"
	add_button.text = "+ Agregar atributo"
	add_button.flat = true
	add_button.alignment = HORIZONTAL_ALIGNMENT_LEFT
	add_button.pressed.connect(_prompt_attribute)
	layout.add_child(add_button)
	var properties := panel_properties(target)
	shown["name"] = true
	for property: String in properties:
		shown[property] = true
	for key in attribute_names(target):
		shown["metadata/" + key] = true
	refreshing = true
	_build(properties)
	refreshing = false
	table.item_edited.connect(_edited)
	table.custom_popup_edited.connect(func(_arrow: bool) -> void: _edited())
	table.button_clicked.connect(_button_clicked)
	table.item_collapsed.connect(_section_folded)

## Properties edited by this grid; the plugin hides their default editors.
static func panel_properties(object: Object) -> Dictionary:
	var result := {}
	for info in object.get_property_list():
		var property: String = info.name
		var studio_name: bool = property == "name" or (property in ["position", "rotation"] and object is Node3D)
		if info.usage & (PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP):
			continue
		if not studio_name and (property.is_empty() or property[0] == "_" or property[0] != property[0].to_upper() or property.contains("/") or not info.usage & PROPERTY_USAGE_EDITOR):
			continue
		var type: int = info.type if info.type != TYPE_NIL else typeof(object.get(property))
		if type in EDITABLE:
			result[property] = {"type": type, "hint": info.hint, "hint_string": info.hint_string, "read_only": bool(info.usage & PROPERTY_USAGE_READ_ONLY)}
	return result

## Same rule as GodotLuau's GetAttributes; rome_ metadata belongs to the editor.
static func is_attribute_name(key: String) -> bool:
	for prefix in ["_", "GL", "gl_", "rbx", "rome_"]:
		if key.begins_with(prefix):
			return false
	return not key.is_empty()

static func attribute_names(object: Node) -> PackedStringArray:
	var names := PackedStringArray()
	for key in object.get_meta_list():
		if is_attribute_name(str(key)):
			names.append(str(key))
	names.sort()
	return names

func commit(property: String, value: Variant) -> void:
	emit_changed(property, value)

func add_attribute(key: String, type_name: String) -> String:
	var clean := key.strip_edges()
	if clean.is_empty():
		return "Escribe un nombre para el atributo."
	if clean.length() > 100:
		return "El nombre no puede pasar de 100 caracteres."
	for character in clean:
		if not (character == "_" or (character >= "0" and character <= "9") or (character.to_lower() >= "a" and character.to_lower() <= "z")):
			return "Usa solo letras, números y guiones bajos, sin espacios."
	if not is_attribute_name(clean) or clean.to_upper().begins_with("RBX"):
		return "Ese nombre está reservado."
	if target.has_meta(clean):
		return "Ya existe un atributo con ese nombre."
	if not ATTRIBUTE_TYPES.has(type_name):
		return "Tipo de atributo no válido."
	var undo := EditorInterface.get_editor_undo_redo()
	undo.create_action("Agregar atributo " + clean, UndoRedo.MERGE_DISABLE, target)
	undo.add_do_method(target, "set_meta", clean, ATTRIBUTE_TYPES[type_name])
	undo.add_do_method(target, "notify_property_list_changed")
	undo.add_undo_method(target, "remove_meta", clean)
	undo.add_undo_method(target, "notify_property_list_changed")
	undo.commit_action()
	return ""

func remove_attribute(key: String) -> void:
	if not target.has_meta(key):
		return
	var undo := EditorInterface.get_editor_undo_redo()
	undo.create_action("Eliminar atributo " + key, UndoRedo.MERGE_DISABLE, target)
	undo.add_do_method(target, "remove_meta", key)
	undo.add_do_method(target, "notify_property_list_changed")
	undo.add_undo_method(target, "set_meta", key, target.get_meta(key))
	undo.add_undo_method(target, "notify_property_list_changed")
	undo.commit_action()

func section_titles() -> PackedStringArray:
	var titles := PackedStringArray()
	for title: String in sections:
		if sections[title].visible:
			titles.append(title)
	return titles

func visible_rows(section_title: String) -> PackedStringArray:
	var labels := PackedStringArray()
	if sections.has(section_title):
		for row in sections[section_title].get_children():
			if row.visible:
				labels.append(row.get_text(0))
	return labels

func _build(properties: Dictionary) -> void:
	var tree_root := table.create_item()
	var data := _section(tree_root, "Datos")
	_row(data, "name", properties.get("name", {"type": TYPE_STRING_NAME}))
	_fixed(data, "ClassName", Catalog.class_name_of(target))
	var parent := target.get_parent()
	_fixed(data, "Parent", str(parent.name) if parent != null and target != EditorInterface.get_edited_scene_root() else "")
	var placed := {"name": true}
	for group: Array in SECTIONS:
		var section: TreeItem
		for property: String in group[1]:
			if properties.has(property) and not placed.has(property):
				if section == null:
					section = _section(tree_root, group[0])
				_row(section, property, properties[property])
				placed[property] = true
	var rest: TreeItem
	for property: String in properties:
		if not placed.has(property):
			if rest == null:
				rest = _section(tree_root, Catalog.class_name_of(target))
			_row(rest, property, properties[property])
	var attributes := _section(tree_root, "Atributos")
	var remove_icon := EditorInterface.get_editor_theme().get_icon("Remove", "EditorIcons")
	for key in attribute_names(target):
		var value: Variant = target.get_meta(key)
		var row: TreeItem
		if typeof(value) in EDITABLE:
			row = _row(attributes, "metadata/" + key, {"type": typeof(value)})
		else:
			row = _fixed(attributes, key, str(value))
		row.set_text(0, key)
		row.set_tooltip_text(0, key)
		row.set_metadata(1, key)
		row.add_button(1, remove_icon, REMOVE, false, "Eliminar atributo")
	_apply_filter()
	_update_property()

func _section(parent: TreeItem, title: String) -> TreeItem:
	var section := table.create_item(parent)
	section.set_text(0, title)
	section.set_selectable(0, false)
	section.set_selectable(1, false)
	section.set_expand_right(0, true)
	section.set_custom_font(0, EditorInterface.get_editor_theme().get_font("bold", "EditorFonts"))
	section.set_custom_bg_color(0, HEADER)
	section.set_custom_bg_color(1, HEADER)
	section.collapsed = filter.is_empty() and bool(folded.get(title, title in FOLDED))
	sections[title] = section
	return section

func _row(parent: TreeItem, property: String, info: Dictionary) -> TreeItem:
	var row := table.create_item(parent)
	var type: int = info.type
	var meta := {"property": property, "type": type}
	row.set_text(0, LABELS.get(property, property))
	row.set_tooltip_text(0, property)
	row.set_editable(1, not info.get("read_only", false))
	if type == TYPE_BOOL:
		row.set_cell_mode(1, TreeItem.CELL_MODE_CHECK)
	elif type == TYPE_COLOR:
		row.set_cell_mode(1, TreeItem.CELL_MODE_CUSTOM)
		row.set_custom_as_button(1, true)
	elif type == TYPE_INT and info.get("hint", 0) == PROPERTY_HINT_ENUM:
		var names := PackedStringArray()
		var values := []
		var next := 0
		for option in str(info.hint_string).split(","):
			var pieces := option.split(":")
			if pieces.size() > 1 and pieces[1].strip_edges().is_valid_int():
				next = pieces[1].strip_edges().to_int()
			names.append(pieces[0].strip_edges())
			values.append(next)
			next += 1
		row.set_cell_mode(1, TreeItem.CELL_MODE_RANGE)
		row.set_text(1, ",".join(names))
		row.set_range_config(1, 0, maxi(names.size() - 1, 0), 1)
		meta["values"] = values
	elif info.get("hint", 0) == PROPERTY_HINT_RANGE:
		meta["range"] = str(info.hint_string)
	row.set_metadata(0, meta)
	rows[property] = row
	return row

func _fixed(parent: TreeItem, title: String, text: String) -> TreeItem:
	var row := table.create_item(parent)
	row.set_text(0, title)
	row.set_tooltip_text(0, title)
	row.set_text(1, text)
	row.set_custom_color(1, Color("a3a6b0"))
	return row

func _apply_filter() -> void:
	add_button.visible = filter.is_empty() or "Atributos".containsn(filter)
	if filter.is_empty():
		return
	for title: String in sections:
		var section: TreeItem = sections[title]
		var whole := title.containsn(filter)
		var any := false
		for row in section.get_children():
			row.visible = whole or row.get_text(0).containsn(filter) or row.get_tooltip_text(0).containsn(filter)
			any = any or row.visible
		section.visible = any or whole

func _update_property() -> void:
	if table == null or not is_instance_valid(target):
		return
	for editor in table.find_children("*", "LineEdit", true, false):
		if editor.is_visible_in_tree() and editor.has_focus():
			return
	refreshing = true
	for property: String in rows:
		var row: TreeItem = rows[property]
		var meta: Dictionary = row.get_metadata(0)
		var value: Variant = _value(property)
		match int(meta.type):
			TYPE_BOOL:
				row.set_checked(1, bool(value))
			TYPE_COLOR:
				var color: Color = value if value is Color else Color.BLACK
				row.set_icon(1, _swatch(color))
				row.set_text(1, "[%d, %d, %d]" % [color.r8, color.g8, color.b8])
			_:
				if meta.has("values"):
					row.set_range(1, maxi(meta.values.find(int(value)), 0))
				else:
					row.set_text(1, _format(value, property))
	refreshing = false

func _value(property: String) -> Variant:
	if property.begins_with("metadata/"):
		return target.get_meta(property.trim_prefix("metadata/"), null)
	return target.get(property)

static func _format(value: Variant, property: String) -> String:
	match typeof(value):
		TYPE_FLOAT:
			return _number(value)
		TYPE_VECTOR2, TYPE_VECTOR3:
			var vector: Variant = value * (180.0 / PI) if property == "rotation" else value
			var parts := PackedStringArray()
			for i in (2 if value is Vector2 else 3):
				parts.append(_number(vector[i]))
			return ", ".join(parts)
		TYPE_VECTOR4:
			return "{%s, %s}, {%s, %s}" % [_number(value.x), _number(value.y), _number(value.z), _number(value.w)]
		TYPE_RECT2:
			return "%s, %s, %s, %s" % [_number(value.position.x), _number(value.position.y), _number(value.size.x), _number(value.size.y)]
	return str(value)

static func _number(value: float) -> String:
	var text := String.num(snappedf(value, 0.001), 3)
	if text.contains("."):
		text = text.rstrip("0").rstrip(".")
	return "0" if text == "-0" else text

static func _swatch(color: Color) -> ImageTexture:
	var side := int(12 * EditorInterface.get_editor_scale())
	var image := Image.create(side, side, false, Image.FORMAT_RGBA8)
	image.fill(Color(0, 0, 0, 0.7))
	image.fill_rect(Rect2i(1, 1, side - 2, side - 2), Color(color, 1.0))
	return ImageTexture.create_from_image(image)

func _edited() -> void:
	if refreshing:
		return
	var row := table.get_edited()
	if row == null or table.get_edited_column() != 1 or not row.get_metadata(0) is Dictionary:
		return
	var meta: Dictionary = row.get_metadata(0)
	if int(meta.type) == TYPE_COLOR:
		_open_color(meta.property)
		return
	var value: Variant = _parse(row, meta)
	if value == null:
		call_deferred("_update_property")
		return
	commit(meta.property, value)

func _parse(row: TreeItem, meta: Dictionary) -> Variant:
	var text := row.get_text(1).strip_edges()
	match int(meta.type):
		TYPE_BOOL:
			return row.is_checked(1)
		TYPE_INT:
			if meta.has("values"):
				return meta.values[clampi(int(row.get_range(1)), 0, meta.values.size() - 1)]
			return int(_clamp(roundf(text.to_float()), meta)) if text.is_valid_float() else null
		TYPE_FLOAT:
			return _clamp(text.to_float(), meta) if text.is_valid_float() else null
		TYPE_STRING, TYPE_STRING_NAME:
			return null if meta.property == "name" and text.is_empty() else row.get_text(1)
	var numbers := PackedFloat64Array()
	for piece in text.replace("{", "").replace("}", "").replace("[", "").replace("]", "").replace("(", "").replace(")", "").split(","):
		if not piece.strip_edges().is_valid_float():
			return null
		numbers.append(piece.strip_edges().to_float())
	match int(meta.type):
		TYPE_VECTOR2:
			return Vector2(numbers[0], numbers[1]) if numbers.size() == 2 else null
		TYPE_VECTOR3:
			if numbers.size() != 3:
				return null
			var vector := Vector3(numbers[0], numbers[1], numbers[2])
			return vector * (PI / 180.0) if meta.property == "rotation" else vector
		TYPE_VECTOR4:
			return Vector4(numbers[0], numbers[1], numbers[2], numbers[3]) if numbers.size() == 4 else null
		TYPE_RECT2:
			return Rect2(numbers[0], numbers[1], numbers[2], numbers[3]) if numbers.size() == 4 else null
	return null

static func _clamp(value: float, meta: Dictionary) -> float:
	var hint := str(meta.get("range", "")).replace(" ", "").split(",")
	if hint.size() >= 2 and hint[0].is_valid_float() and hint[1].is_valid_float():
		if not "or_less" in hint:
			value = maxf(value, hint[0].to_float())
		if not "or_greater" in hint:
			value = minf(value, hint[1].to_float())
	return value

func _open_color(property: String) -> void:
	color_property = property
	if color_popup == null:
		color_popup = PopupPanel.new()
		add_child(color_popup)
		picker = ColorPicker.new()
		picker.edit_alpha = false
		picker.color_changed.connect(func(color: Color) -> void: commit(color_property, color))
		color_popup.add_child(picker)
	var current: Variant = _value(property)
	picker.color = current if current is Color else Color.WHITE
	color_popup.popup_centered(Vector2i(340, 420))

func _button_clicked(item: TreeItem, _column: int, id: int, _mouse_button: int) -> void:
	if id == REMOVE and item.get_metadata(1) is String:
		remove_attribute(item.get_metadata(1))

func _section_folded(item: TreeItem) -> void:
	if not refreshing and filter.is_empty() and sections.get(item.get_text(0)) == item:
		folded[item.get_text(0)] = item.collapsed
	table.update_minimum_size()

func _prompt_attribute() -> void:
	if attribute_dialog == null:
		attribute_dialog = ConfirmationDialog.new()
		attribute_dialog.name = "ROMEAttributeDialog"
		attribute_dialog.title = "Agregar atributo"
		attribute_dialog.ok_button_text = "Agregar"
		attribute_dialog.dialog_hide_on_ok = false
		add_child(attribute_dialog)
		var layout := VBoxContainer.new()
		attribute_dialog.add_child(layout)
		attribute_name = LineEdit.new()
		attribute_name.placeholder_text = "Nombre"
		attribute_name.text_submitted.connect(func(_text: String) -> void: _confirm_attribute())
		layout.add_child(attribute_name)
		attribute_type = OptionButton.new()
		for type_name: String in ATTRIBUTE_TYPES:
			attribute_type.add_item(type_name)
		layout.add_child(attribute_type)
		attribute_error = Label.new()
		attribute_error.modulate = Color("ed8188")
		attribute_error.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		layout.add_child(attribute_error)
		attribute_dialog.confirmed.connect(_confirm_attribute)
	attribute_name.text = ""
	attribute_error.text = ""
	attribute_dialog.popup_centered(Vector2i(320, 150))
	attribute_name.grab_focus()

func _confirm_attribute() -> void:
	var error := add_attribute(attribute_name.text, attribute_type.get_item_text(attribute_type.selected))
	if error.is_empty():
		attribute_dialog.hide()
	else:
		attribute_error.text = error
