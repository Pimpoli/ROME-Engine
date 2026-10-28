@tool
extends EditorProperty

var target: Node
var table: Tree
var rows: Dictionary = {}
var picker: ColorPicker
var color_popup: PopupPanel
var refreshing := false

func _init(object: Node) -> void:
	target = object
	table = Tree.new()
	table.columns = 2
	table.hide_root = true
	table.select_mode = Tree.SELECT_SINGLE
	table.set_column_expand(0, false)
	table.set_column_custom_minimum_width(0, 98)
	table.custom_minimum_size = Vector2(195, 560)
	table.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	table.add_theme_constant_override("v_separation", 4)
	add_child(table)
	set_bottom_editor(table)
	table.item_edited.connect(_edited)
	table.custom_popup_edited.connect(_color_requested)
	var root := table.create_item()
	var properties := {}
	for info in target.get_property_list():
		properties[info.name] = info
	var appearance := _section(root, "Apariencia")
	for property in ["Color", "Material", "Transparency", "Reflectance", "CastShadow"]:
		if properties.has(property):
			_row(appearance, property, property, int(properties[property].type))
	var data := _section(root, "Datos")
	_row(data, "Name", "name", TYPE_STRING_NAME)
	var class_row := table.create_item(data)
	class_row.set_text(0, "ClassName")
	class_row.set_text(1, target.get_class().trim_prefix("Roblox"))
	var parent_row := table.create_item(data)
	parent_row.set_text(0, "Parent")
	parent_row.set_text(1, str(target.get_parent().name) if target.get_parent() != null else "")
	var transform := _section(root, "Transformacion")
	for property in ["Size", "position", "rotation"]:
		if properties.has(property):
			_row(transform, "Orientation" if property == "rotation" else property.capitalize(), property, TYPE_VECTOR3)
	var behavior := _section(root, "Comportamiento")
	for property in ["Anchored", "Locked", "CanCollide", "CanTouch", "CanQuery", "Massless"]:
		if properties.has(property):
			_row(behavior, property, property, TYPE_BOOL)
	_update_property()

func _section(root: TreeItem, title: String) -> TreeItem:
	var section := table.create_item(root)
	section.set_text(0, title)
	section.set_selectable(0, false)
	section.set_selectable(1, false)
	section.set_custom_bg_color(0, Color("25262d"))
	section.set_custom_bg_color(1, Color("25262d"))
	return section

func _row(parent: TreeItem, title: String, property: String, type: int) -> void:
	var row := table.create_item(parent)
	row.set_text(0, title)
	row.set_metadata(0, {"property": property, "type": type})
	row.set_editable(1, true)
	if type == TYPE_BOOL:
		row.set_cell_mode(1, TreeItem.CELL_MODE_CHECK)
	elif type == TYPE_COLOR:
		row.set_cell_mode(1, TreeItem.CELL_MODE_CUSTOM)
		row.set_custom_as_button(1, true)
	elif property == "Material":
		row.set_cell_mode(1, TreeItem.CELL_MODE_RANGE)
		row.set_text(1, "Plastic:0,Wood:3,Metal:10,Concrete:7,Glass:16,Neon:2,Grass:14")
		row.set_range_config(1, 0, 20, 1)
	rows[property] = row

func _update_property() -> void:
	if table == null or not is_instance_valid(target):
		return
	for editor in table.find_children("*", "LineEdit", true, false):
		if editor.has_focus():
			return
	refreshing = true
	for property in rows:
		var row: TreeItem = rows[property]
		var value: Variant = target.get(property)
		if value is bool:
			row.set_checked(1, value)
		elif value is Color:
			row.set_custom_bg_color(1, value)
			row.set_custom_color(1, Color.BLACK if value.get_luminance() > 0.5 else Color.WHITE)
			row.set_text(1, "#" + value.to_html(false).to_upper())
		elif property == "Material":
			row.set_range(1, float(value))
		elif value is Vector3:
			var vector: Vector3 = value * (180.0 / PI) if property == "rotation" else value
			row.set_text(1, "%s, %s, %s" % [snappedf(vector.x, 0.01), snappedf(vector.y, 0.01), snappedf(vector.z, 0.01)])
		else:
			row.set_text(1, str(value))
	refreshing = false

func _edited() -> void:
	if refreshing:
		return
	var row := table.get_edited()
	if row == null or table.get_edited_column() != 1:
		return
	var info: Dictionary = row.get_metadata(0)
	if info.is_empty():
		return
	var property: String = info.property
	var value: Variant = row.get_text(1)
	match int(info.type):
		TYPE_BOOL:
			value = row.is_checked(1)
		TYPE_INT:
			value = int(row.get_range(1)) if property == "Material" else int(value)
		TYPE_FLOAT:
			value = float(value)
		TYPE_VECTOR3:
			var parts := str(value).split(",")
			if parts.size() != 3 or not parts[0].strip_edges().is_valid_float() or not parts[1].strip_edges().is_valid_float() or not parts[2].strip_edges().is_valid_float():
				_update_property()
				return
			value = Vector3(float(parts[0]), float(parts[1]), float(parts[2]))
			if property == "rotation":
				value *= PI / 180.0
	emit_changed(property, value)

func _color_requested() -> void:
	var row := table.get_edited()
	if row == null or row.get_metadata(0).get("property", "") != "Color":
		return
	if color_popup == null:
		color_popup = PopupPanel.new()
		add_child(color_popup)
		picker = ColorPicker.new()
		picker.edit_alpha = false
		picker.color_changed.connect(func(color: Color) -> void: emit_changed("Color", color))
		color_popup.add_child(picker)
	picker.color = target.get("Color")
	color_popup.popup_centered(Vector2i(340, 420))
