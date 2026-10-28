#include "editor_node.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_main_screen.h"
#include "editor/run/editor_run_bar.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_log.h"
#include "editor/editor_string_names.h"
#include "editor/gui/editor_title_bar.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "editor/settings/editor_settings.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/project_settings_editor.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/gui/code_editor.h"
#include "core/object/script_language.h"
#include "core/object/editor_language.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/popup_menu.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/color_picker.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/style_box_flat.h"

void rome_main_plugins_menu(int p_id, EditorNode *p_editor) {
	if (p_id == 0) {
		ProjectSettingsEditor::get_singleton()->popup_project_settings();
	} else if (p_id == 1) {
		EditorDock *dock = EditorDockManager::get_singleton()->get_dock_by_name("Luau");
		if (dock) { EditorDockManager::get_singleton()->focus_dock(dock); }
	} else {
		p_editor->emit_signal("rome_editor_action", "import", Variant());
	}
}

void EditorNode::_rome_edit_menu(int p_id) {
	if (p_id == 0) { undo(); } else { redo(); }
}

void EditorNode::_rome_view_menu(int p_id) {
	const char *actions[] = { "explorer", "properties", "assets", "output" };
	if (p_id < 4) { _rome_editor_action(actions[p_id]); }
	else if (EditorInterface::get_singleton()) {
		EditorInterface::get_singleton()->set_main_screen_editor(p_id == 4 ? "3D" : p_id == 5 ? "2D" : "Script");
	}
}

void EditorNode::_rome_ribbon_tab_changed(int p_tab) {
	for (int i = 0; i < rome_ribbon_pages.size(); i++) {
		rome_ribbon_pages[i]->set_visible(i == p_tab);
	}

}

void EditorNode::_rome_editor_action(const String &p_action) {
	if (p_action == "explorer") {
		EditorDockManager::get_singleton()->focus_dock(SceneTreeDock::get_singleton());
	} else if (p_action == "properties") {
		EditorDockManager::get_singleton()->focus_dock(InspectorDock::get_singleton());
	} else if (p_action == "assets") {
		emit_signal("rome_editor_action", "resources", Variant());
	} else if (p_action == "output") {
		EditorDockManager::get_singleton()->focus_dock(log);
	} else if (p_action == "coordinate_space") {
		Dictionary state;
		state["local_coords"] = !Node3DEditor::get_singleton()->are_local_coords_enabled();
		Node3DEditor::get_singleton()->set_state(state);
	} else if (p_action == "ui_editor") {
		EditorInterface::get_singleton()->set_main_screen_editor("2D");
	} else if (p_action == "commands") {
		EditorCommandPalette::get_singleton()->open_popup();
	} else if (p_action == "debugger") {
		EditorDockManager::get_singleton()->focus_dock(EditorDebuggerNode::get_singleton());
	} else if (p_action == "animation" || p_action == "luau_settings") {
		EditorDock *dock = EditorDockManager::get_singleton()->get_dock_by_name(p_action == "animation" ? TTR("Animation") : "Luau");
		if (dock) { EditorDockManager::get_singleton()->focus_dock(dock); }
	} else if (p_action.begins_with("script_")) {
		TextEditorBase *editor = Object::cast_to<TextEditorBase>(ScriptEditor::get_singleton()->get_current_editor());
		if (!editor) { show_warning("Abre un script para usar esta herramienta."); return; }
		EditorInterface::get_singleton()->set_main_screen_editor("Script");
		if (p_action == "script_find") {
			FindReplaceBar *find = editor->get_code_editor()->get_find_replace_bar();
			if (find) { find->popup_search(); }
		} else if (p_action == "script_goto") {
			GotoLinePopup *popup = memnew(GotoLinePopup);
			gui_base->add_child(popup);
			popup->popup_find_line(editor->get_code_editor());
		} else if (p_action == "script_format") {
			CodeEdit *text = Object::cast_to<CodeEdit>(editor->get_base_editor());
			Ref<Script> script = editor->get_edited_resource();
			if (text && script.is_valid()) {
				String source = text->get_text();
				if (script->get_language()->get_editor_language()) {
					script->get_language()->get_editor_language()->format_code(source, 0, text->get_line_count() - 1);
				}
				text->begin_complex_operation();
				text->select_all();
				text->insert_text_at_caret(source);
				text->end_complex_operation();
			}
		}
	} else {
		emit_signal("rome_editor_action", p_action, Variant());
	}
}

void EditorNode::_rome_color_changed(const Color &p_color) {
	emit_signal("rome_editor_action", "color", p_color);
}

void EditorNode::_rome_material_changed(int p_material) {
	emit_signal("rome_editor_action", "material", p_material);
}

void EditorNode::_rome_snap_changed(double p_value, bool p_rotation) {
	rome_snap_step[p_rotation ? 1 : 0] = p_value;
	rome_snap_enabled[p_rotation ? 1 : 0] = true;
	_rome_sync_snap_controls();
	_rome_apply_snap();
}

void EditorNode::_rome_snap_toggled(bool p_enabled, bool p_rotation) {
	rome_snap_enabled[p_rotation ? 1 : 0] = p_enabled;
	_rome_sync_snap_controls();
	_rome_apply_snap();
}

void EditorNode::_rome_sync_snap_controls() {
	if (!rome_pages_row) {
		return;
	}
	TypedArray<Node> controls = rome_pages_row->find_children("ROMESnap*", "", true, false);
	for (int i = 0; i < controls.size(); i++) {
		Node *control = Object::cast_to<Node>(controls[i]);
		const int axis = String(control->get_name()).ends_with("1") ? 1 : 0;
		if (CheckBox *toggle = Object::cast_to<CheckBox>(control)) {
			toggle->set_pressed_no_signal(rome_snap_enabled[axis]);
		} else if (SpinBox *value = Object::cast_to<SpinBox>(control)) {
			value->set_value_no_signal(rome_snap_step[axis]);
		}
	}
}

void EditorNode::_rome_apply_snap() {
	Node3DEditor *editor = Node3DEditor::get_singleton();
	if (!editor) {
		return;
	}
	// Node3DEditor has one snap switch; a zero step leaves that axis free.
	Dictionary state;
	state["snap_enabled"] = rome_snap_enabled[0] || rome_snap_enabled[1];
	state["translate_snap"] = rome_snap_enabled[0] ? rome_snap_step[0] : 0.0;
	state["rotate_snap"] = rome_snap_enabled[1] ? rome_snap_step[1] : 0.0;
	editor->set_state(state);
	EditorSettings *settings = EditorSettings::get_singleton();
	settings->set_project_metadata("rome", "snap_move", rome_snap_enabled[0]);
	settings->set_project_metadata("rome", "snap_rotate", rome_snap_enabled[1]);
	settings->set_project_metadata("rome", "snap_move_value", rome_snap_step[0]);
	settings->set_project_metadata("rome", "snap_rotate_value", rome_snap_step[1]);
}

void EditorNode::_rome_emit_action(const String &p_action, const Variant &p_payload) {
	emit_signal("rome_editor_action", p_action, p_payload);
}

void EditorNode::_rome_show_tool_menu(Button *p_tool, PopupMenu *p_menu) {
	p_menu->reset_size();
	p_menu->set_position(p_tool->get_screen_position() + Vector2(0, p_tool->get_size().y));
	p_menu->popup();
}

void EditorNode::_rome_tool_menu_selected(int p_index, const String &p_tool) {
	if (!rome_tool_menus.has(p_tool)) {
		return;
	}
	const Vector<RomeToolMenuItem> &items = rome_tool_menus[p_tool];
	ERR_FAIL_INDEX(p_index, items.size());
	items[p_index].action.call();
}

void EditorNode::_rome_selection_changed() {
	if (!rome_pages_row || !editor_selection) {
		return;
	}
	const bool has_selection = !editor_selection->get_selected_nodes().is_empty();
	TypedArray<Node> tools = rome_pages_row->find_children("Tool*", "Button", true, false);
	for (int i = 0; i < tools.size(); i++) {
		Button *tool = Object::cast_to<Button>(tools[i]);
		if (!tool || !tool->has_meta("rome_needs_selection")) {
			continue;
		}
		tool->set_disabled(!has_selection);
		tool->set_modulate(Color(1, 1, 1, has_selection ? 1.0 : 0.4));
		for (int j = 0; j < tool->get_child_count(); j++) {
			if (Button *arrow = Object::cast_to<Button>(tool->get_child(j))) {
				arrow->set_disabled(!has_selection);
			}
		}
	}
}

bool EditorNode::_rome_shortcut_input(const Ref<InputEvent> &p_event) {
	if (!bool(ProjectSettings::get_singleton()->get_setting("rome/studio/enabled", false))) {
		return false;
	}
	Control *focus = get_viewport()->gui_get_focus_owner();
	if (Object::cast_to<LineEdit>(focus) || Object::cast_to<TextEdit>(focus)) {
		return false;
	}
	if (ED_IS_SHORTCUT("rome/stop_test", p_event)) {
		project_run_bar->stop_playing();
		return true;
	}
	if (ED_IS_SHORTCUT("editor/run_current_scene", p_event)) {
		project_run_bar->play_current_scene();
		return true;
	}
	if (editor_main_screen->get_current_tab_control() == ScriptEditor::get_singleton()) {
		return false;
	}
	const char *tools[] = { "rome/tool_select", "rome/tool_move", "rome/tool_scale", "rome/tool_rotate", "rome/tool_transform" };
	const int modes[] = { Node3DEditor::TOOL_MODE_SELECT, Node3DEditor::TOOL_MODE_MOVE, Node3DEditor::TOOL_MODE_SCALE, Node3DEditor::TOOL_MODE_ROTATE, Node3DEditor::TOOL_MODE_TRANSFORM };
	for (int i = 0; i < 5; i++) {
		if (ED_IS_SHORTCUT(tools[i], p_event)) {
			_rome_set_3d_tool(modes[i]);
			return true;
		}
	}
	if (ED_IS_SHORTCUT("rome/toggle_local_space", p_event)) {
		_rome_editor_action("coordinate_space");
	} else if (ED_IS_SHORTCUT("rome/group", p_event)) {
		_rome_emit_action("group", Variant());
	} else if (ED_IS_SHORTCUT("rome/group_folder", p_event)) {
		_rome_emit_action("group_folder", Variant());
	} else if (ED_IS_SHORTCUT("rome/ungroup", p_event)) {
		_rome_emit_action("ungroup", Variant());
	} else {
		return false;
	}
	return true;
}

void EditorNode::_rome_prefer_world_view() {
	// Studio opens a place on its world; an empty script screen is never the first view.
	ScriptEditor *scripts = ScriptEditor::get_singleton();
	Node *root = editor_data.get_edited_scene_root();
	if (scripts && editor_main_screen->get_current_tab_control() == scripts && scripts->get_open_scripts().is_empty() && Object::cast_to<Node3D>(root)) {
		EditorInterface::get_singleton()->set_main_screen_editor("3D");
	}
}

void EditorNode::_rome_play_menu(int p_id) {
	if (p_id == 0) {
		project_run_bar->play_main_scene();
	} else if (p_id == 1) {
		project_run_bar->play_current_scene();
	} else {
		project_run_bar->stop_playing();
	}
}

void EditorNode::_rome_create_ribbon() {
	PanelContainer *ribbon = memnew(PanelContainer);
	ribbon->set_name("ROMERibbon");
	Ref<StyleBoxFlat> background;
	background.instantiate();
	background->set_bg_color(Color(0.095, 0.10, 0.116));
	background->set_content_margin_individual(6 * EDSCALE, 2 * EDSCALE, 6 * EDSCALE, 3 * EDSCALE);
	background->set_border_width(SIDE_BOTTOM, EDSCALE);
	background->set_border_color(Color::html("34353d"));
	ribbon->add_theme_style_override("panel", background);
	main_vbox->add_child(ribbon);
	main_vbox->move_child(ribbon, title_bar->get_index() + 1);
	VBoxContainer *layout = memnew(VBoxContainer);
	layout->add_theme_constant_override("separation", 2 * EDSCALE);
	ribbon->add_child(layout);
	HBoxContainer *mezzanine = memnew(HBoxContainer);
	mezzanine->set_custom_minimum_size(Size2(0, 34) * EDSCALE);
	layout->add_child(mezzanine);
	rome_run_slot = memnew(HBoxContainer);
	mezzanine->add_child(rome_run_slot);
	MenuButton *test_menu = memnew(MenuButton);
	test_menu->set_name("ROMETestMenu");
	test_menu->set_text("Probar");
	test_menu->set_flat(false);
	test_menu->set_button_icon(theme->get_icon(SNAME("GuiDropdown"), EditorStringName(EditorIcons)));
	test_menu->set_icon_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	test_menu->set_custom_minimum_size(Size2(86, 24) * EDSCALE);
	test_menu->set_tooltip_text("Opciones de prueba");
	PopupMenu *test_popup = test_menu->get_popup();
	test_popup->add_item("Probar (F5)", 0);
	test_popup->add_item("Probar escena actual (F6)", 1);
	test_popup->add_separator();
	test_popup->add_item(String::utf8("Detener (Mayús+F5)"), 2);
	test_popup->connect(SceneStringName(id_pressed), callable_mp(this, &EditorNode::_rome_play_menu));
	rome_run_slot->add_child(test_menu);
	ED_SHORTCUT("rome/stop_test", "Detener prueba", KeyModifierMask::SHIFT | Key::F5);
	ED_SHORTCUT("rome/tool_select", "Herramienta Seleccionar", KeyModifierMask::CMD_OR_CTRL | Key::KEY_1);
	ED_SHORTCUT("rome/tool_move", "Herramienta Mover", KeyModifierMask::CMD_OR_CTRL | Key::KEY_2);
	ED_SHORTCUT("rome/tool_scale", "Herramienta Escalar", KeyModifierMask::CMD_OR_CTRL | Key::KEY_3);
	ED_SHORTCUT("rome/tool_rotate", "Herramienta Girar", KeyModifierMask::CMD_OR_CTRL | Key::KEY_4);
	ED_SHORTCUT("rome/tool_transform", "Herramienta Transformar", KeyModifierMask::CMD_OR_CTRL | Key::KEY_5);
	ED_SHORTCUT("rome/toggle_local_space", "Alternar espacio local o global", KeyModifierMask::CMD_OR_CTRL | Key::L);
	ED_SHORTCUT("rome/group", "Agrupar como modelo", KeyModifierMask::CMD_OR_CTRL | Key::G);
	ED_SHORTCUT("rome/group_folder", "Agrupar como carpeta", KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::ALT | Key::G);
	ED_SHORTCUT("rome/ungroup", "Desagrupar", KeyModifierMask::CMD_OR_CTRL | Key::U);
	Control *left_gap = memnew(Control);
	left_gap->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	mezzanine->add_child(left_gap);
	rome_ribbon_group.instantiate();
	rome_tabs_row = memnew(HBoxContainer);
	rome_tabs_row->add_theme_constant_override("separation", 4 * EDSCALE);
	mezzanine->add_child(rome_tabs_row);
	const char *tab_names[] = { "Inicio", "Avatar", "UI", "Script", "Modelo", "Plugins" };
	for (int i = 0; i < 6; i++) {
		Button *tab = memnew(Button);
		tab->set_text(tab_names[i]);
		tab->set_custom_minimum_size(Size2(80, 24) * EDSCALE);
		tab->set_flat(true);
		tab->set_toggle_mode(true);
		tab->set_button_group(rome_ribbon_group);
		tab->set_pressed(i == 0);
		Ref<StyleBoxFlat> active = memnew(StyleBoxFlat);
		active->set_bg_color(Color::html("303139"));
		active->set_corner_radius_all(3 * EDSCALE);
		active->set_content_margin_individual(14 * EDSCALE, 2 * EDSCALE, 14 * EDSCALE, 2 * EDSCALE);
		tab->add_theme_style_override("pressed", active);
		tab->connect("pressed", callable_mp(this, &EditorNode::_rome_ribbon_tab_changed).bind(i));
		rome_tabs_row->add_child(tab);
		rome_ribbon_tabs.push_back(tab);
	}
	Control *right_gap = memnew(Control);
	right_gap->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	right_gap->set_stretch_ratio(3.0);
	mezzanine->add_child(right_gap);
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_vertical_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_AUTO);
	scroll->set_custom_minimum_size(Size2(0, 60) * EDSCALE);
	layout->add_child(scroll);
	HBoxContainer *pages = memnew(HBoxContainer);
	rome_pages_row = pages;
	pages->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(pages);
	for (int i = 0; i < 6; i++) {
		HBoxContainer *page = memnew(HBoxContainer);
		page->add_theme_constant_override("separation", 1 * EDSCALE);
		page->set_visible(i == 0);
		pages->add_child(page);
		rome_ribbon_pages.push_back(page);
	}
	auto tool = [this](Control *p_row, const String &p_label, const StringName &p_icon, const Callable &p_action, bool p_enabled = true) {
		bool exists = false;
		for (const RomeRibbonCommand &entry : rome_ribbon_commands) { if (entry.id == p_label) { exists = true; break; } }
		if (!exists) {
			RomeRibbonCommand command;
			command.id = p_label;
			command.label = p_label;
			command.icon = p_icon;
			command.action = p_action;
			command.enabled = p_enabled;
			rome_ribbon_commands.push_back(command);
		}
		return _rome_make_tool(p_row, p_label);
	};
	auto insert = [this, &tool](Control *p_row, const String &p_label, const StringName &p_icon, const String &p_type) {
		tool(p_row, p_label, p_icon, callable_mp(this, &EditorNode::_rome_create_node).bind(p_type));
	};
	auto action = [this, &tool](Control *p_row, const String &p_label, const StringName &p_icon, const String &p_action) {
		tool(p_row, p_label, p_icon, callable_mp(this, &EditorNode::_rome_editor_action).bind(p_action));
	};
	auto transforms = [this, &tool](Control *p_row) {
		Ref<ButtonGroup> modes;
		modes.instantiate();
		const char *names[] = { "Seleccionar", "Mover", "Girar", "Escalar", "Transformar" };
		const char *icons[] = { "StudioSelect", "StudioMove", "StudioRotate", "StudioScale", "StudioTransform" };
		const int tools[] = { Node3DEditor::TOOL_MODE_SELECT, Node3DEditor::TOOL_MODE_MOVE, Node3DEditor::TOOL_MODE_ROTATE, Node3DEditor::TOOL_MODE_SCALE, Node3DEditor::TOOL_MODE_TRANSFORM };
		for (int i = 0; i < 5; i++) {
			Button *button = tool(p_row, names[i], icons[i], callable_mp(this, &EditorNode::_rome_set_3d_tool).bind(tools[i]));
			button->set_toggle_mode(true);
			button->set_button_group(modes);
			button->set_pressed(i == 0);
			button->set_meta("rome_tool_mode", tools[i]);
		}
	};
	EditorSettings *settings = EditorSettings::get_singleton();
	rome_snap_enabled[0] = settings->get_project_metadata("rome", "snap_move", false);
	rome_snap_enabled[1] = settings->get_project_metadata("rome", "snap_rotate", false);
	rome_snap_step[0] = settings->get_project_metadata("rome", "snap_move_value", 1.0);
	rome_snap_step[1] = settings->get_project_metadata("rome", "snap_rotate_value", 40.0);
	auto snaps = [this](Control *row) {
		VBoxContainer *snap = memnew(VBoxContainer);
		snap->add_theme_constant_override("separation", 1 * EDSCALE);
		row->add_child(snap);
		// Every page shows the same snap state, like Studio's single snapping setting.
		for (int i = 0; i < 2; i++) {
			HBoxContainer *line = memnew(HBoxContainer);
			line->add_theme_constant_override("separation", 2 * EDSCALE);
			snap->add_child(line);
			CheckBox *enabled = memnew(CheckBox);
			enabled->set_name("ROMESnapToggle" + itos(i));
			enabled->set_focus_mode(Control::FOCUS_NONE);
			enabled->set_tooltip_text(i ? String::utf8("Ajustar el giro a este \u00e1ngulo") : "Ajustar el movimiento a esta distancia");
			enabled->set_pressed_no_signal(rome_snap_enabled[i]);
			enabled->connect(SceneStringName(toggled), callable_mp(this, &EditorNode::_rome_snap_toggled).bind(i == 1));
			line->add_child(enabled);
			SpinBox *value = memnew(SpinBox);
			value->set_name("ROMESnapValue" + itos(i));
			// Range snaps to min + k * step, so the minimum must sit on the step grid.
			value->set_min(i ? 1 : 0.1); value->set_max(i ? 180 : 100);
			value->set_step(i ? 1 : 0.1);
			value->set_custom_arrow_step(i ? 5 : 0.5);
			value->set_value_no_signal(rome_snap_step[i]);
			value->set_suffix(i ? String::utf8(u8"\u00b0") : "studs");
			value->set_custom_minimum_size(Size2(88, 0) * EDSCALE);
			value->connect("value_changed", callable_mp(this, &EditorNode::_rome_snap_changed).bind(i == 1));
			line->add_child(value);
		}
	};
	auto geometry = [this, &tool](Control *row) {
		tool(row, String::utf8(u8"Geom\u00e9trico"), "StudioGeometry", callable_mp(this, &EditorNode::_rome_editor_action).bind("coordinate_space"));
	};
	auto unavailable = [this, &tool](Control *row, const String &label, const StringName &icon) {
		tool(row, label, icon, callable_mp(this, &EditorNode::_rome_editor_action).bind("unavailable"), false);
	};
	rome_material_popup = memnew(PopupMenu);
	ribbon->add_child(rome_material_popup);
	const char *materials[] = { "Plastic", "Wood", "Metal", "Concrete", "Glass", "Neon", "Grass" };
	for (int i = 0; i < 7; i++) { rome_material_popup->add_item(materials[i], i); }
	rome_material_popup->connect("id_pressed", callable_mp(this, &EditorNode::_rome_material_changed));
	rome_color_picker = memnew(ColorPickerButton);
	rome_color_picker->set_pick_color(Color(0.64, 0.64, 0.66));
	rome_color_picker->hide();
	ribbon->add_child(rome_color_picker);
	rome_color_picker->connect("color_changed", callable_mp(this, &EditorNode::_rome_color_changed));
	// Studio's split tools: the icon runs the default action, the arrow offers the variants.
	auto entry = [](const String &p_label, const Callable &p_action) {
		RomeToolMenuItem item;
		item.label = p_label;
		item.action = p_action;
		return item;
	};
	const Callable emit = callable_mp(this, &EditorNode::_rome_emit_action);
	const Callable create = callable_mp(this, &EditorNode::_rome_create_node);
	rome_tool_menus["Pieza"] = { entry("Bloque", emit.bind("insert_part", 0)), entry("Esfera", emit.bind("insert_part", 1)), entry("Cilindro", emit.bind("insert_part", 2)), entry(String::utf8("Cuña"), emit.bind("insert_part", 3)), entry(String::utf8("Cuña de esquina"), emit.bind("insert_part", 4)) };
	rome_tool_menus["GUI"] = { entry("ScreenGui", create.bind("ScreenGui")), entry("SurfaceGui", create.bind("SurfaceGui")), entry("BillboardGui", create.bind("BillboardGui")) };
	rome_tool_menus["Script"] = { entry("Script", create.bind("ServerScript")), entry("LocalScript", create.bind("LocalScript")), entry("ModuleScript", create.bind("ModuleScript")) };
	Vector<RomeToolMenuItem> material_items;
	for (int i = 0; i < 7; i++) {
		material_items.push_back(entry(materials[i], callable_mp(this, &EditorNode::_rome_material_changed).bind(i)));
	}
	rome_tool_menus["Material"] = material_items;
	const char *color_names[] = { "Blanco", "Gris piedra", "Gris oscuro", "Negro", "Rojo", "Naranja", "Amarillo", "Verde lima", "Verde", "Azul", "Celeste", "Morado", "Rosa", "Marrón" };
	const char *color_values[] = { "f2f3f3", "a3a2a5", "635f62", "1b2a35", "c4281c", "da8541", "f5cd30", "a4bd47", "287f47", "0d69ac", "6e99ca", "6b327c", "e8bac8", "7c5c46" };
	Vector<RomeToolMenuItem> color_items;
	for (int i = 0; i < 14; i++) {
		RomeToolMenuItem item = entry(String::utf8(color_names[i]), callable_mp(this, &EditorNode::_rome_color_changed).bind(Color::html(color_values[i])));
		item.swatch = Color::html(color_values[i]);
		item.has_swatch = true;
		color_items.push_back(item);
	}
	color_items.push_back(entry(String::utf8("Más colores..."), callable_mp(this, &EditorNode::_rome_open_color)));
	rome_tool_menus["Color"] = color_items;
	rome_tool_menus["Grupo"] = { entry("Agrupar como modelo (Ctrl+G)", emit.bind("group", Variant())), entry("Agrupar como carpeta (Ctrl+Alt+G)", emit.bind("group_folder", Variant())), entry("Desagrupar (Ctrl+U)", emit.bind("ungroup", Variant())) };
	rome_tool_menus["Bloquear"] = { entry("Bloquear", emit.bind("lock_set", true)), entry("Desbloquear", emit.bind("lock_set", false)) };
	rome_tool_menus["Anclar"] = { entry("Anclar", emit.bind("anchor_set", true)), entry("Desanclar", emit.bind("anchor_set", false)) };
	for (const char *id : { "Material", "Color", "Grupo", "Bloquear", "Anclar" }) {
		rome_selection_tools.insert(id);
	}
	auto appearance = [this, &tool](Control *row) {
		tool(row, "Material", "StudioMaterial", callable_mp(this, &EditorNode::_rome_open_material));
		tool(row, "Color", "StudioColor", callable_mp(this, &EditorNode::_rome_open_color));
	};
	Control *home = rome_ribbon_pages[0];
	transforms(home); geometry(home); snaps(home); home->add_child(memnew(VSeparator));
	insert(home, "Pieza", "StudioPart", "Part");
	action(home, "Terreno", "StudioTerrain", "terrain");
	insert(home, "Personaje", "StudioCharacter", "Character");
	insert(home, "GUI", "StudioGui", "ScreenGui");
	insert(home, "Script", "StudioScript", "ServerScript");
	action(home, "Importar", "StudioImport", "import");
	home->add_child(memnew(VSeparator)); appearance(home);
	action(home, "Grupo", "StudioGroup", "group"); action(home, "Bloquear", "StudioLock", "lock"); action(home, "Anclar", "StudioAnchor", "anchor");
	home->add_child(memnew(VSeparator));
	action(home, "Explorador", "StudioExplorer", "explorer"); action(home, "Propiedades", "StudioProperties", "properties");
	action(home, "Caja de herramientas", "StudioToolbox", "insert"); action(home, "Recursos", "StudioAssets", "assets");
	Control *avatar = rome_ribbon_pages[1];
	transforms(avatar); geometry(avatar); snaps(avatar); avatar->add_child(memnew(VSeparator));
	action(avatar, String::utf8(u8"Configuraci\u00f3n"), "StudioAvatarSettings", "avatar_settings");
	insert(avatar, "Personaje", "StudioCharacter", "Character");
	insert(avatar, "Configurar", "StudioRig", "Humanoid"); insert(avatar, "Accesorio", "StudioAccessory", "Accessory");
	action(avatar, String::utf8(u8"Animaci\u00f3n"), "StudioAnimation", "animation");
	unavailable(avatar, String::utf8(u8"Validaci\u00f3n"), "StudioValidate"); avatar->add_child(memnew(VSeparator));
	action(avatar, "Editor de clips", "StudioClips", "animation"); unavailable(avatar, "Editor de avatar", "StudioAvatarEditor");
	Control *ui = rome_ribbon_pages[2];
	insert(ui, "GUI", "StudioGui", "ScreenGui"); insert(ui, "Cuadro", "StudioFrame", "Frame");
	insert(ui, "Etiqueta", "StudioLabel", "TextLabel"); insert(ui, "Entrada", "StudioInput", "TextBox");
	insert(ui, "Apariencia", "StudioAppearance", "UICorner"); insert(ui, String::utf8(u8"Dise\u00f1o"), "StudioLayout", "UIListLayout");
	insert(ui, String::utf8(u8"Restricci\u00f3n"), "StudioConstraint", "UISizeConstraint"); ui->add_child(memnew(VSeparator));
	action(ui, "Editor de UI", "StudioUiEditor", "ui_editor");
	Control *script = rome_ribbon_pages[3];
	unavailable(script, "Volver", "StudioBack"); unavailable(script, "Avanzar", "StudioForward"); script->add_child(memnew(VSeparator));
	insert(script, "Script", "StudioScript", "ServerScript");
	action(script, "Formato", "StudioFormat", "script_format"); action(script, "Buscar", "StudioSearch", "script_find"); action(script, "Ir a la linea", "StudioGoto", "script_goto");
	script->add_child(memnew(VSeparator)); action(script, "Comando", "StudioCommand", "commands"); action(script, "Salida", "StudioOutput", "output");
	script->add_child(memnew(VSeparator)); action(script, "Puntos de interrupcion", "StudioBreakpoint", "debugger"); action(script, "Pila de llamadas", "StudioStack", "debugger"); action(script, "Inspeccionar", "StudioWatch", "debugger");
	script->add_child(memnew(VSeparator)); action(script, "Analisis", "StudioAnalysis", "debugger"); action(script, "Actividad", "StudioActivity", "debugger");
	Control *model = rome_ribbon_pages[4];
	transforms(model); geometry(model); snaps(model); model->add_child(memnew(VSeparator));
	unavailable(model, "Pivote", "StudioPivot"); unavailable(model, "Restablecer", "StudioReset"); model->add_child(memnew(VSeparator));
	insert(model, "Pieza", "StudioPart", "Part"); insert(model, "Efecto", "StudioEffect", "ParticleEmitter");
	insert(model, "Conexion", "StudioConnection", "Attachment"); insert(model, "Restricciones", "StudioConstraint3D", "WeldConstraint"); insert(model, "Soldadura", "StudioWeld", "Weld");
	model->add_child(memnew(VSeparator)); appearance(model); unavailable(model, "Texture", "StudioTexture");
	action(model, "Grupo", "StudioGroup", "group"); action(model, "Bloquear", "StudioLock", "lock"); action(model, "Anclar", "StudioAnchor", "anchor");
	model->add_child(memnew(VSeparator)); unavailable(model, "Alinear", "StudioAlign");
	model->add_child(memnew(VSeparator)); unavailable(model, "Unir", "StudioUnion"); unavailable(model, "Intersecar", "StudioIntersect"); unavailable(model, "Separar", "StudioSeparate"); unavailable(model, "Sustraer", "StudioSubtract");
	Control *plugins = rome_ribbon_pages[5];
	tool(plugins, "Administrar plugins", "StudioPlugins", callable_mp(this, &EditorNode::_rome_ribbon_action).bind(ROME_PROJECT_SETTINGS));
	action(plugins, "ROME Luau", "StudioLuau", "luau_settings"); action(plugins, "Importar .rbxl", "StudioImport", "import");
	// Additional insertion shortcuts are available to custom tabs.
	insert(nullptr, "LocalScript", "StudioScript", "LocalScript"); insert(nullptr, "ModuleScript", "StudioScript", "ModuleScript");
	insert(nullptr, "Modelo", "StudioGroup", "RBXModel"); insert(nullptr, "Carpeta", "StudioAssets", "Folder");
	insert(nullptr, "Imagen", "StudioLabel", "ImageLabel"); insert(nullptr, "Boton", "StudioInput", "TextButton");
	tool(nullptr, "Guardar", "StudioSave", callable_mp(this, &EditorNode::_rome_ribbon_action).bind(ROME_SAVE_SCENE));
	tool(nullptr, "Nueva escena", "StudioAssets", callable_mp(this, &EditorNode::_rome_ribbon_action).bind(ROME_NEW_SCENE));
	Button *add_tab = memnew(Button);
	add_tab->set_name("ROMEAddTab");
	add_tab->set_text("+");
	add_tab->set_flat(true);
	add_tab->set_tooltip_text("Crear una nueva pesta" + String::chr(0x00f1) + "a personalizada");
	add_tab->set_custom_minimum_size(Size2(36, 24) * EDSCALE);
	add_tab->connect("pressed", callable_mp(this, &EditorNode::_rome_prompt_new_tab));
	rome_tabs_row->add_child(add_tab);
	if (EditorSettings::get_singleton()->has_setting("rome/studio/custom_tabs")) {
		rome_custom_tabs = EditorSettings::get_singleton()->get("rome/studio/custom_tabs");
	}
	_rome_rebuild_custom_tabs();
	_rome_ribbon_tab_changed(0);
	editor_selection->connect("selection_changed", callable_mp(this, &EditorNode::_rome_selection_changed));
	_rome_selection_changed();
}
