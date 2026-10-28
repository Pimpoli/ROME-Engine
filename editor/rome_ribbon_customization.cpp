#include "editor_node.h"

#include "core/object/callable_mp.h"
#include "core/input/input_event.h"
#include "core/io/image.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/separator.h"
#include "scene/gui/texture_rect.h"
#include "scene/gui/tree.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

enum RomeCustomTabAction {
	ROME_TAB_RENAME,
	ROME_TAB_DUPLICATE,
	ROME_TAB_MOVE_LEFT,
	ROME_TAB_MOVE_RIGHT,
	ROME_TAB_DELETE,
};

enum RomeCustomToolAction {
	ROME_TOOL_MOVE_LEFT,
	ROME_TOOL_MOVE_RIGHT,
	ROME_TOOL_REMOVE,
	ROME_TOOL_ADD,
	ROME_TOOL_DIVIDER,
};

// A separator is stored alongside command IDs, so its position survives reloads.
static const char *ROME_TOOL_DIVIDER_ID = "@divider";

static void rome_tab_context(const Ref<InputEvent> &p_event, PopupMenu *p_menu) {
	Ref<InputEventMouseButton> mouse = p_event;
	if (mouse.is_valid() && mouse->is_pressed() && mouse->get_button_index() == MouseButton::RIGHT) {
		p_menu->set_position(DisplayServer::get_singleton()->mouse_get_position());
		p_menu->popup();
	}
}

Button *EditorNode::_rome_make_tool(Control *p_row, const String &p_id) {
	if (!p_row) { return nullptr; }
	for (const RomeRibbonCommand &command : rome_ribbon_commands) {
		if (command.id != p_id) { continue; }
		const bool has_menu = rome_tool_menus.has(p_id);
		Button *button = memnew(Button);
		button->set_name("Tool" + p_id.validate_node_name());
		button->set_meta("rome_tool_id", p_id);
		if (rome_selection_tools.has(p_id)) {
			button->set_meta("rome_needs_selection", true);
		}
		button->set_custom_minimum_size(Size2(MAX(48, MIN(76, command.label.length() * 6 + 8)) + (has_menu ? 12 : 0), 54) * EDSCALE);
		button->set_flat(true);
		button->set_focus_mode(Control::FOCUS_NONE);
		button->set_tooltip_text(command.enabled ? command.label : command.label + "\nEsta herramienta no esta disponible en ROME todavia.");
		button->set_disabled(!command.enabled);
		if (!command.enabled) { button->set_modulate(Color(1, 1, 1, 0.35)); }
		button->connect("pressed", command.action);
		p_row->add_child(button);
		VBoxContainer *inside = memnew(VBoxContainer);
		inside->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		inside->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		inside->set_alignment(BoxContainer::ALIGNMENT_CENTER);
		inside->add_theme_constant_override("separation", 3 * EDSCALE);
		button->add_child(inside);
		TextureRect *texture = memnew(TextureRect);
		texture->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		texture->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		texture->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		texture->set_custom_minimum_size(Size2(24, 24) * EDSCALE);
		texture->set_texture(theme->get_icon(command.icon, EditorStringName(EditorIcons)));
		inside->add_child(texture);
		RomeRibbonIcon entry;
		entry.texture = texture;
		entry.icon = command.icon;
		rome_ribbon_icons.push_back(entry);
		Label *caption = memnew(Label(command.label));
		caption->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
		caption->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		caption->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
		caption->add_theme_font_size_override("font_size", 11 * EDSCALE);
		inside->add_child(caption);
		if (has_menu && command.enabled) {
			PopupMenu *menu = memnew(PopupMenu);
			const Vector<RomeToolMenuItem> &items = rome_tool_menus[p_id];
			for (int i = 0; i < items.size(); i++) {
				if (items[i].has_swatch) {
					Ref<Image> swatch = Image::create_empty(14 * EDSCALE, 14 * EDSCALE, false, Image::FORMAT_RGBA8);
					swatch->fill(items[i].swatch);
					menu->add_icon_item(ImageTexture::create_from_image(swatch), items[i].label, i);
				} else {
					menu->add_item(items[i].label, i);
				}
			}
			menu->connect(SceneStringName(id_pressed), callable_mp(this, &EditorNode::_rome_tool_menu_selected).bind(p_id));
			button->add_child(menu);
			Button *arrow = memnew(Button);
			arrow->set_name("Arrow");
			arrow->set_flat(true);
			arrow->set_focus_mode(Control::FOCUS_NONE);
			arrow->set_tooltip_text(String::utf8("Más opciones de ") + command.label);
			arrow->set_anchors_preset(Control::PRESET_TOP_RIGHT);
			arrow->set_offset(SIDE_LEFT, -15 * EDSCALE);
			arrow->set_offset(SIDE_TOP, 6 * EDSCALE);
			arrow->set_offset(SIDE_RIGHT, -1 * EDSCALE);
			arrow->set_offset(SIDE_BOTTOM, 30 * EDSCALE);
			arrow->connect(SceneStringName(pressed), callable_mp(this, &EditorNode::_rome_show_tool_menu).bind(button, menu));
			button->add_child(arrow);
			TextureRect *arrow_icon = memnew(TextureRect);
			arrow_icon->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
			arrow_icon->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
			arrow_icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
			arrow_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
			arrow_icon->set_texture(theme->get_icon(SNAME("GuiDropdown"), EditorStringName(EditorIcons)));
			arrow->add_child(arrow_icon);
			RomeRibbonIcon arrow_entry;
			arrow_entry.texture = arrow_icon;
			arrow_entry.icon = SNAME("GuiDropdown");
			rome_ribbon_icons.push_back(arrow_entry);
		}
		return button;
	}
	return nullptr;
}

Array EditorNode::_rome_get_custom_tabs() const { return rome_custom_tabs.duplicate(true); }

void EditorNode::_rome_prompt_new_tab() {
	if (!rome_new_tab_dialog) {
		rome_new_tab_dialog = memnew(ConfirmationDialog);
		rome_new_tab_dialog->set_name("ROMENewTabDialog");
		rome_new_tab_dialog->set_title("Nueva pesta" + String::chr(0x00f1) + "a");
		rome_new_tab_dialog->set_ok_button_text("Crear");
		gui_base->add_child(rome_new_tab_dialog);
		VBoxContainer *layout = memnew(VBoxContainer);
		rome_new_tab_dialog->add_child(layout);
		rome_new_tab_name = memnew(LineEdit);
		rome_new_tab_name->set_name("ROMENewTabName");
		rome_new_tab_name->set_placeholder("Nombre de la pesta" + String::chr(0x00f1) + "a");
		rome_new_tab_name->set_max_length(32);
		layout->add_child(rome_new_tab_name);
		rome_new_tab_error = memnew(Label);
		rome_new_tab_error->add_theme_color_override("font_color", Color::html("ed8188"));
		layout->add_child(rome_new_tab_error);
		rome_new_tab_dialog->set_hide_on_ok(false);
		rome_new_tab_dialog->connect("confirmed", callable_mp(this, &EditorNode::_rome_confirm_new_tab));
		rome_new_tab_name->connect("text_submitted", callable_mp(this, &EditorNode::_rome_confirm_new_tab).unbind(1));
	}
	rome_new_tab_name->clear();
	rome_new_tab_dialog->set_meta("rename_index", -1);
	rome_new_tab_dialog->set_title("Nueva pesta" + String::chr(0x00f1) + "a");
	rome_new_tab_dialog->set_ok_button_text("Crear");
	rome_new_tab_error->set_text("");
	rome_new_tab_dialog->popup_centered(Size2i(360, 115) * EDSCALE);
	rome_new_tab_name->grab_focus();
}

void EditorNode::_rome_confirm_new_tab() {
	const int rename_index = rome_new_tab_dialog->get_meta("rename_index", -1);
	if (rename_index >= 0) {
		ERR_FAIL_INDEX(rename_index, rome_custom_tabs.size());
		const String name = rome_new_tab_name->get_text().strip_edges();
		bool valid = !name.is_empty() && name.length() <= 32 && !name.contains_char('/') && !name.contains_char('\\');
		for (int i = 0; i < rome_ribbon_tabs.size(); i++) {
			if (i != rename_index + 6 && rome_ribbon_tabs[i]->get_text().nocasecmp_to(name) == 0) {
				valid = false;
			}
		}
		if (!valid) {
			rome_new_tab_error->set_text("Escribe un nombre nuevo para la pesta" + String::chr(0x00f1) + "a.");
			return;
		}
		Dictionary tab = rome_custom_tabs[rename_index];
		tab["name"] = name;
		rome_custom_tabs[rename_index] = tab;
		_rome_rebuild_custom_tabs();
		rome_ribbon_tabs[rename_index + 6]->set_pressed(true);
		_rome_ribbon_tab_changed(rename_index + 6);
		EditorSettings::get_singleton()->set("rome/studio/custom_tabs", rome_custom_tabs.duplicate(true));
		EditorSettings::save();
		rome_new_tab_dialog->hide();
		return;
	}
	if (_rome_create_custom_tab(rome_new_tab_name->get_text()) < 0) {
		rome_new_tab_error->set_text("Escribe un nombre nuevo para la pesta" + String::chr(0x00f1) + "a.");
		return;
	}
	rome_new_tab_dialog->hide();
}

int EditorNode::_rome_create_custom_tab(const String &p_name) {
	const String name = p_name.strip_edges();
	if (name.is_empty() || name.length() > 32 || name.contains_char('/') || name.contains_char('\\')) { return -1; }
	for (Button *tab : rome_ribbon_tabs) {
		if (tab->get_text().nocasecmp_to(name) == 0) { return -1; }
	}
	Dictionary tab;
	tab["name"] = name;
	tab["tools"] = PackedStringArray();
	rome_custom_tabs.push_back(tab);
	_rome_rebuild_custom_tabs();
	EditorSettings::get_singleton()->set("rome/studio/custom_tabs", rome_custom_tabs.duplicate(true));
	EditorSettings::save();
	const int page = 6 + rome_custom_tabs.size() - 1;
	rome_ribbon_tabs[page]->set_pressed(true);
	_rome_ribbon_tab_changed(page);
	return page;
}

void EditorNode::_rome_set_custom_tools(int p_tab, const PackedStringArray &p_tools) {
	ERR_FAIL_INDEX(p_tab, rome_custom_tabs.size());
	PackedStringArray valid;
	for (const String &id : p_tools) {
		if (id == ROME_TOOL_DIVIDER_ID) {
			valid.push_back(id);
			continue;
		}
		for (const RomeRibbonCommand &command : rome_ribbon_commands) {
			if (command.id == id && !valid.has(id)) { valid.push_back(id); break; }
		}
	}
	Dictionary tab = rome_custom_tabs[p_tab];
	tab["tools"] = valid;
	rome_custom_tabs[p_tab] = tab;
	_rome_rebuild_custom_page(p_tab);
	EditorSettings::get_singleton()->set("rome/studio/custom_tabs", rome_custom_tabs.duplicate(true));
	EditorSettings::save();
}

void EditorNode::_rome_remove_custom_tab(int p_tab) {
	ERR_FAIL_INDEX(p_tab, rome_custom_tabs.size());
	rome_custom_tabs.remove_at(p_tab);
	_rome_rebuild_custom_tabs();
	rome_ribbon_tabs[0]->set_pressed(true);
	_rome_ribbon_tab_changed(0);
	EditorSettings::get_singleton()->set("rome/studio/custom_tabs", rome_custom_tabs.duplicate(true));
	EditorSettings::save();
}

void EditorNode::_rome_custom_tab_action(int p_action, int p_tab) {
	ERR_FAIL_INDEX(p_tab, rome_custom_tabs.size());
	Dictionary tab = rome_custom_tabs[p_tab];
	if (p_action == ROME_TAB_RENAME) {
		_rome_prompt_new_tab();
		rome_new_tab_dialog->set_meta("rename_index", p_tab);
		rome_new_tab_dialog->set_title("Renombrar pesta" + String::chr(0x00f1) + "a");
		rome_new_tab_dialog->set_ok_button_text("Guardar");
		rome_new_tab_name->set_text(tab.get("name", ""));
		rome_new_tab_name->select_all();
		return;
	}
	if (p_action == ROME_TAB_DUPLICATE) {
		const String stem = String(tab.get("name", "Personalizada")).left(18) + " copia";
		String name = stem;
		int suffix = 2;
		while (true) {
			bool exists = false;
			for (Button *button : rome_ribbon_tabs) {
				if (button->get_text().nocasecmp_to(name) == 0) { exists = true; break; }
			}
			if (!exists) { break; }
			name = stem + " " + itos(suffix++);
		}
		const int page = _rome_create_custom_tab(name);
		if (page >= 6) { _rome_set_custom_tools(page - 6, tab.get("tools", PackedStringArray())); }
		return;
	}
	if (p_action == ROME_TAB_DELETE) {
		_rome_remove_custom_tab(p_tab);
		return;
	}
	if (p_action == ROME_TAB_MOVE_LEFT || p_action == ROME_TAB_MOVE_RIGHT) {
		const int target = p_tab + (p_action == ROME_TAB_MOVE_LEFT ? -1 : 1);
		if (target < 0 || target >= rome_custom_tabs.size()) { return; }
		rome_custom_tabs[p_tab] = rome_custom_tabs[target];
		rome_custom_tabs[target] = tab;
		_rome_rebuild_custom_tabs();
		rome_ribbon_tabs[target + 6]->set_pressed(true);
		_rome_ribbon_tab_changed(target + 6);
		EditorSettings::get_singleton()->set("rome/studio/custom_tabs", rome_custom_tabs.duplicate(true));
		EditorSettings::save();
	}
}

void EditorNode::_rome_custom_tool_action(int p_action, int p_tab, int p_tool) {
	ERR_FAIL_INDEX(p_tab, rome_custom_tabs.size());
	if (p_action == ROME_TOOL_ADD) {
		_rome_show_tool_picker(p_tab);
		return;
	}
	Dictionary tab = rome_custom_tabs[p_tab];
	PackedStringArray tools = tab.get("tools", PackedStringArray());
	if (p_action == ROME_TOOL_DIVIDER) {
		tools.insert(p_tool < 0 ? tools.size() : p_tool + 1, ROME_TOOL_DIVIDER_ID);
	} else {
		ERR_FAIL_INDEX(p_tool, tools.size());
		if (p_action == ROME_TOOL_REMOVE) {
			tools.remove_at(p_tool);
		} else if (p_action == ROME_TOOL_MOVE_LEFT || p_action == ROME_TOOL_MOVE_RIGHT) {
			const int target = p_tool + (p_action == ROME_TOOL_MOVE_LEFT ? -1 : 1);
			if (target < 0 || target >= tools.size()) { return; }
			const String command = tools[p_tool];
			tools.set(p_tool, tools[target]);
			tools.set(target, command);
		}
	}
	_rome_set_custom_tools(p_tab, tools);
}

void EditorNode::_rome_rebuild_custom_tabs() {
	for (int i = rome_ribbon_tabs.size() - 1; i >= 6; i--) {
		Button *tab = rome_ribbon_tabs[i];
		tab->get_parent()->remove_child(tab);
		tab->queue_free();
		rome_ribbon_tabs.remove_at(i);
	}
	while (rome_ribbon_pages.size() < 6 + rome_custom_tabs.size()) {
		HBoxContainer *page = memnew(HBoxContainer);
		page->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		page->add_theme_constant_override("separation", 1 * EDSCALE);
		page->hide();
		rome_pages_row->add_child(page);
		rome_ribbon_pages.push_back(page);
	}
	for (int i = 0; i < rome_custom_tabs.size(); i++) {
		Dictionary info = rome_custom_tabs[i];
		Button *tab = memnew(Button);
		tab->set_name("CustomTab" + itos(i));
		tab->set_text(info.get("name", "Personalizada"));
		tab->set_flat(true);
		tab->set_toggle_mode(true);
		tab->set_button_group(rome_ribbon_group);
		tab->set_custom_minimum_size(Size2(80, 24) * EDSCALE);
		Ref<StyleBoxFlat> selected = memnew(StyleBoxFlat);
		selected->set_bg_color(Color::html("303139"));
		selected->set_corner_radius_all(3 * EDSCALE);
		selected->set_content_margin_individual(14 * EDSCALE, 2 * EDSCALE, 14 * EDSCALE, 2 * EDSCALE);
		tab->add_theme_style_override("pressed", selected);
		PopupMenu *context = memnew(PopupMenu);
		context->set_name("ROMECustomTabMenu" + itos(i));
		context->add_item("Renombrar pesta" + String::chr(0x00f1) + "a", ROME_TAB_RENAME);
		context->add_item("Duplicar pesta" + String::chr(0x00f1) + "a", ROME_TAB_DUPLICATE);
		context->add_separator();
		context->add_item("Mover a la izquierda", ROME_TAB_MOVE_LEFT);
		context->set_item_disabled(context->get_item_index(ROME_TAB_MOVE_LEFT), i == 0);
		context->add_item("Mover a la derecha", ROME_TAB_MOVE_RIGHT);
		context->set_item_disabled(context->get_item_index(ROME_TAB_MOVE_RIGHT), i + 1 == rome_custom_tabs.size());
		context->add_separator();
		context->add_item("Eliminar pesta" + String::chr(0x00f1) + "a", ROME_TAB_DELETE);
		context->connect("id_pressed", callable_mp(this, &EditorNode::_rome_custom_tab_action).bind(i));
		tab->add_child(context);
		tab->connect("gui_input", callable_mp_static(&rome_tab_context).bind(context));
		tab->connect("pressed", callable_mp(this, &EditorNode::_rome_ribbon_tab_changed).bind(i + 6));
		rome_tabs_row->add_child(tab);
		rome_tabs_row->move_child(tab, 6 + i);
		rome_ribbon_tabs.push_back(tab);
		_rome_rebuild_custom_page(i);
	}
	for (int i = 6 + rome_custom_tabs.size(); i < rome_ribbon_pages.size(); i++) { rome_ribbon_pages[i]->hide(); }
}

void EditorNode::_rome_rebuild_custom_page(int p_tab) {
	Control *page = rome_ribbon_pages[6 + p_tab];
	for (int i = rome_ribbon_icons.size() - 1; i >= 0; i--) {
		if (page->is_ancestor_of(rome_ribbon_icons[i].texture)) { rome_ribbon_icons.remove_at(i); }
	}
	while (page->get_child_count()) {
		Node *child = page->get_child(0);
		page->remove_child(child);
		child->queue_free();
	}
	Dictionary tab = rome_custom_tabs[p_tab];
	PackedStringArray tools = tab.get("tools", PackedStringArray());
	for (int i = 0; i < tools.size(); i++) {
		Control *tool = nullptr;
		if (tools[i] == ROME_TOOL_DIVIDER_ID) {
			VSeparator *divider = memnew(VSeparator);
			divider->set_custom_minimum_size(Size2(12, 44) * EDSCALE);
			divider->set_tooltip_text("Separador");
			page->add_child(divider);
			tool = divider;
		} else {
			tool = _rome_make_tool(page, tools[i]);
		}
		if (!tool) { continue; }
		PopupMenu *context = memnew(PopupMenu);
		context->set_name("ROMECustomToolMenu" + itos(i));
		context->add_item("Mover a la izquierda", ROME_TOOL_MOVE_LEFT);
		context->set_item_disabled(context->get_item_index(ROME_TOOL_MOVE_LEFT), i == 0);
		context->add_item("Mover a la derecha", ROME_TOOL_MOVE_RIGHT);
		context->set_item_disabled(context->get_item_index(ROME_TOOL_MOVE_RIGHT), i + 1 == tools.size());
		context->add_separator();
		context->add_item(tools[i] == ROME_TOOL_DIVIDER_ID ? "Quitar separador" : "Quitar herramienta", ROME_TOOL_REMOVE);
		context->add_separator();
		context->add_item("Agregar herramientas...", ROME_TOOL_ADD);
		context->add_item("Agregar separador", ROME_TOOL_DIVIDER);
		context->connect("id_pressed", callable_mp(this, &EditorNode::_rome_custom_tool_action).bind(p_tab, i));
		tool->add_child(context);
		tool->connect("gui_input", callable_mp_static(&rome_tab_context).bind(context));
	}
	Button *add = memnew(Button);
	add->set_text("+\nAgregar herramientas");
	add->set_name("AddTools" + itos(p_tab));
	add->set_flat(true);
	add->set_custom_minimum_size(Size2(112, 54) * EDSCALE);
	add->add_theme_font_size_override("font_size", 11 * EDSCALE);
	add->connect("pressed", callable_mp(this, &EditorNode::_rome_show_tool_picker).bind(p_tab));
	page->add_child(add);
	Control *empty = memnew(Control);
	empty->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	empty->set_custom_minimum_size(Size2(24, 54) * EDSCALE);
	page->add_child(empty);
	PopupMenu *context = memnew(PopupMenu);
	context->set_name("ROMECustomPageMenu" + itos(p_tab));
	context->add_item("Agregar herramientas...", ROME_TOOL_ADD);
	context->add_item("Agregar separador", ROME_TOOL_DIVIDER);
	context->connect("id_pressed", callable_mp(this, &EditorNode::_rome_custom_tool_action).bind(p_tab, -1));
	empty->add_child(context);
	empty->connect("gui_input", callable_mp_static(&rome_tab_context).bind(context));
	add->connect("gui_input", callable_mp_static(&rome_tab_context).bind(context));
	_rome_selection_changed();
}

void EditorNode::_rome_show_tool_picker(int p_tab) {
	rome_editing_custom_tab = p_tab;
	if (!rome_tool_picker) {
		rome_tool_picker = memnew(ConfirmationDialog);
		rome_tool_picker->set_name("ROMEAddToolsDialog");
		rome_tool_picker->set_title("Agregar herramientas");
		rome_tool_picker->set_ok_button_text("Listo");
		gui_base->add_child(rome_tool_picker);
		VBoxContainer *layout = memnew(VBoxContainer);
		rome_tool_picker->add_child(layout);
		rome_tool_search = memnew(LineEdit);
		rome_tool_search->set_placeholder("Buscar una herramienta");
		rome_tool_search->connect("text_changed", callable_mp(this, &EditorNode::_rome_filter_tools));
		layout->add_child(rome_tool_search);
		rome_tool_list = memnew(Tree);
		rome_tool_list->set_hide_root(true);
		rome_tool_list->set_custom_minimum_size(Size2(390, 360) * EDSCALE);
		rome_tool_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		rome_tool_list->connect("item_edited", callable_mp(this, &EditorNode::_rome_custom_tool_edited));
		layout->add_child(rome_tool_list);
	}
	rome_tool_search->set_text("");
	_rome_filter_tools("");
	rome_tool_picker->popup_centered(Size2(430, 470) * EDSCALE);
	rome_tool_search->grab_focus();
}

void EditorNode::_rome_filter_tools(const String &p_search) {
	rome_tool_list->clear();
	TreeItem *root = rome_tool_list->create_item();
	Dictionary tab = rome_custom_tabs[rome_editing_custom_tab];
	PackedStringArray tools = tab.get("tools", PackedStringArray());
	for (const RomeRibbonCommand &command : rome_ribbon_commands) {
		if (!command.enabled || !command.label.containsn(p_search)) { continue; }
		TreeItem *item = rome_tool_list->create_item(root);
		item->set_cell_mode(0, TreeItem::CELL_MODE_CHECK);
		item->set_editable(0, true);
		item->set_text(0, command.label);
		item->set_metadata(0, command.id);
		item->set_checked(0, tools.has(command.id));
		item->set_icon(0, theme->get_icon(command.icon, EditorStringName(EditorIcons)));
	}
}

void EditorNode::_rome_custom_tool_edited() {
	TreeItem *item = rome_tool_list->get_edited();
	if (!item) { return; }
	Dictionary tab = rome_custom_tabs[rome_editing_custom_tab];
	PackedStringArray tools = tab.get("tools", PackedStringArray());
	const String id = item->get_metadata(0);
	if (item->is_checked(0) && !tools.has(id)) { tools.push_back(id); }
	if (!item->is_checked(0)) { const int index = tools.find(id); if (index >= 0) { tools.remove_at(index); } }
	_rome_set_custom_tools(rome_editing_custom_tab, tools);
}

void EditorNode::_rome_open_color() { rome_color_picker->get_popup()->popup_centered(); }
void EditorNode::_rome_open_material() { rome_material_popup->set_position(DisplayServer::get_singleton()->mouse_get_position()); rome_material_popup->popup(); }
