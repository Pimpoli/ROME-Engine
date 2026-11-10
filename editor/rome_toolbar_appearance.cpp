#include "editor_node.h"

#include "core/input/input_event.h"
#include "core/object/callable_mp.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/separator.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/timer.h"
#include "scene/resources/font.h"
#include "scene/resources/style_box_flat.h"
#include "servers/display/display_server.h"

namespace {
enum ToolbarOption { TOOLBAR_COMPACT, TOOLBAR_LABELS, TOOLBAR_COLLAPSED, TOOLBAR_LEFT_ALIGNED, TOOLBAR_OPTION_COUNT };
const char *const TOOLBAR_KEYS[] = { "compact", "labels", "collapsed", "left_aligned" };
const char *const TOOLBAR_SETTINGS[] = {
	"rome/studio/toolbar_compact",
	"rome/studio/toolbar_labels",
	"rome/studio/toolbar_collapsed",
	"rome/studio/toolbar_left_aligned",
};
}

Dictionary EditorNode::_rome_get_toolbar_options() const {
	Dictionary options;
	options["compact"] = rome_toolbar_compact;
	options["labels"] = rome_toolbar_labels;
	options["collapsed"] = rome_toolbar_collapsed;
	options["left_aligned"] = rome_tabs_left_aligned;
	return options;
}

void EditorNode::_rome_set_toolbar_options(const Dictionary &p_options) {
	Dictionary options = _rome_get_toolbar_options();
	for (int i = 0; i < TOOLBAR_OPTION_COUNT; i++) {
		if (p_options.has(TOOLBAR_KEYS[i]) && p_options[TOOLBAR_KEYS[i]].get_type() == Variant::BOOL) {
			options[TOOLBAR_KEYS[i]] = p_options[TOOLBAR_KEYS[i]];
			EditorSettings::get_singleton()->set(TOOLBAR_SETTINGS[i], p_options[TOOLBAR_KEYS[i]]);
		}
	}
	rome_toolbar_compact = options["compact"];
	rome_toolbar_labels = options["labels"];
	rome_toolbar_collapsed = options["collapsed"];
	rome_tabs_left_aligned = options["left_aligned"];
	rome_toolbar_revealed = false;
	_rome_apply_toolbar_options();
	EditorSettings::save();
}

void EditorNode::_rome_style_ribbon_button(Button *p_button, bool p_tab) {
	ERR_FAIL_NULL(p_button);
	// Flat Button skips its state styleboxes entirely. Transparent normal boxes
	// preserve the quiet toolbar while allowing a visible active tab/tool.
	p_button->set_flat(false);
	auto state = [p_tab](const Color &p_color) {
		Ref<StyleBoxFlat> box = memnew(StyleBoxFlat);
		box->set_bg_color(p_color);
		box->set_corner_radius_all(3 * EDSCALE);
		const int margin = (p_tab ? 10 : 2) * EDSCALE;
		box->set_content_margin_individual(margin, 2 * EDSCALE, margin, 2 * EDSCALE);
		return box;
	};
	auto apply = [p_button, p_tab, &state](const String &p_name, const Color &p_color, bool p_selected = false) {
		const Ref<StyleBoxFlat> box = state(p_color);
		if (p_tab && p_selected) {
			box->set_border_width(SIDE_BOTTOM, MAX(1, Math::round(2 * EDSCALE)));
			box->set_border_color(Color::html("6687ff"));
		}
		p_button->add_theme_style_override(p_name, box);
		p_button->add_theme_style_override(p_name + "_mirrored", box);
	};
	apply("normal", Color(0, 0, 0, 0));
	apply("disabled", Color(0, 0, 0, 0));
	apply("hover", Color::html("282a31"));
	apply("pressed", Color::html("303139"), true);
	apply("hover_pressed", Color::html("393b43"), true);
	for (const char *key : { "font_color", "font_pressed_color", "font_hover_color", "font_hover_pressed_color" }) {
		p_button->add_theme_color_override(key, Color::html("e2e3e8"));
	}
	p_button->add_theme_color_override("icon_pressed_color", Color(1, 1, 1));
	p_button->add_theme_color_override("icon_hover_pressed_color", Color(1, 1, 1));
	if (p_tab) {
		p_button->add_theme_font_size_override("font_size", 12 * EDSCALE);
	}
}

void EditorNode::_rome_style_toolbar_controls(Control *p_root) {
	if (!p_root) {
		return;
	}
	Button *button = Object::cast_to<Button>(p_root);
	if (button && button->has_meta("rome_tool_id")) {
		_rome_style_ribbon_button(button);
		Label *caption = Object::cast_to<Label>(button->get_node_or_null(NodePath("ToolContent/ToolCaption")));
		TextureRect *icon = Object::cast_to<TextureRect>(button->get_node_or_null(NodePath("ToolContent/ToolIcon")));
		ERR_FAIL_NULL(caption);
		ERR_FAIL_NULL(icon);
		const bool menu = rome_tool_menus.has(button->get_meta("rome_tool_id"));
		const int caption_size = (rome_toolbar_compact ? 10 : 11) * EDSCALE;
		caption->add_theme_font_size_override("font_size", caption_size);
		caption->set_visible(rome_toolbar_labels);

		real_t width = (rome_toolbar_compact ? 30 : 36) * EDSCALE;
		if (rome_toolbar_labels) {
			const Ref<Font> font = caption->get_theme_font(SNAME("font"));
			const real_t measured = font->get_string_size(caption->get_text(), HORIZONTAL_ALIGNMENT_LEFT, -1, caption_size).x;
			width = CLAMP(Math::ceil(measured) + 12 * EDSCALE, (rome_toolbar_compact ? 40 : 48) * EDSCALE, (rome_toolbar_compact ? 84 : 112) * EDSCALE);
		}
		const int height = rome_toolbar_labels ? (rome_toolbar_compact ? 44 : 54) : (rome_toolbar_compact ? 30 : 38);
		button->set_custom_minimum_size(Size2(width + (menu ? 12 * EDSCALE : 0), height * EDSCALE));
		icon->set_custom_minimum_size(Size2(rome_toolbar_compact ? 18 : 24, rome_toolbar_compact ? 18 : 24) * EDSCALE);
		Button *arrow = Object::cast_to<Button>(button->get_node_or_null(NodePath("Arrow")));
		if (arrow) {
			_rome_style_ribbon_button(arrow);
			arrow->set_offset(SIDE_TOP, (rome_toolbar_compact ? 3 : 6) * EDSCALE);
			arrow->set_offset(SIDE_BOTTOM, (rome_toolbar_compact ? 23 : 30) * EDSCALE);
		}
	} else if (button && button->has_meta("rome_add_tools")) {
		_rome_style_ribbon_button(button);
		button->set_text(rome_toolbar_labels ? "+\nAgregar herramientas" : "+");
		button->set_custom_minimum_size(Size2(rome_toolbar_labels ? (rome_toolbar_compact ? 96 : 112) : 32, rome_toolbar_compact ? 38 : (rome_toolbar_labels ? 54 : 38)) * EDSCALE);
	} else if (p_root->has_meta("rome_tool_spacer")) {
		p_root->set_custom_minimum_size(Size2(24, rome_toolbar_compact ? 30 : (rome_toolbar_labels ? 54 : 38)) * EDSCALE);
	} else if (Object::cast_to<VSeparator>(p_root)) {
		p_root->set_custom_minimum_size(Size2(p_root->get_custom_minimum_size().x / EDSCALE, rome_toolbar_compact ? 28 : 44) * EDSCALE);
	}
	for (int i = 0; i < p_root->get_child_count(); i++) {
		Control *child = Object::cast_to<Control>(p_root->get_child(i));
		if (child) {
			_rome_style_toolbar_controls(child);
		}
	}
}

void EditorNode::_rome_apply_toolbar_options() {
	if (!rome_toolbar_menu) {
		return;
	}
	rome_toolbar_menu->set_item_checked(TOOLBAR_COMPACT, rome_toolbar_compact);
	rome_toolbar_menu->set_item_checked(TOOLBAR_LABELS, rome_toolbar_labels);
	rome_toolbar_menu->set_item_checked(TOOLBAR_COLLAPSED, rome_toolbar_collapsed);
	_rome_style_toolbar_controls(rome_pages_row);
	rome_toolbar_menu->set_item_checked(rome_toolbar_menu->get_item_index(TOOLBAR_LEFT_ALIGNED), rome_tabs_left_aligned);
	rome_fit_tabs.call_deferred();
	rome_toolbar_scroll->set_custom_minimum_size(Size2(0, rome_toolbar_labels ? (rome_toolbar_compact ? 44 : 54) : (rome_toolbar_compact ? 38 : 42)) * EDSCALE);
	const bool show_tools = !rome_toolbar_collapsed || rome_toolbar_revealed;
	rome_toolbar_scroll->set_visible(show_tools);
	rome_toolbar_tools_row->set_visible(show_tools);
	rome_toolbar_tools_panel->set_visible(show_tools);
	_rome_queue_toolbar_overflow_update();
	if (!rome_toolbar_revealed) {
		rome_toolbar_hover_timer->stop();
	}
}

void EditorNode::_rome_queue_toolbar_overflow_update() {
	if (!rome_toolbar_tools_row || rome_toolbar_overflow_update_pending) {
		return;
	}
	rome_toolbar_overflow_update_pending = true;
	callable_mp(this, &EditorNode::_rome_update_toolbar_overflow).call_deferred();
}

void EditorNode::_rome_update_toolbar_overflow() {
	rome_toolbar_overflow_update_pending = false;
	if (!rome_toolbar_tools_row || !rome_toolbar_scroll || !rome_pages_row) {
		return;
	}
	// Compare against the entire row, not the narrower viewport left after
	// revealing the chevrons, so crossing the threshold cannot oscillate.
	const bool visible = (!rome_toolbar_collapsed || rome_toolbar_revealed) &&
			rome_pages_row->get_combined_minimum_size().x > rome_toolbar_tools_row->get_size().x + EDSCALE;
	rome_toolbar_back->set_visible(visible);
	rome_toolbar_next->set_visible(visible);
	ScrollBar *bar = rome_toolbar_scroll->get_h_scroll_bar();
	const double end = MAX(0.0, bar->get_max() - bar->get_page());
	rome_toolbar_back->set_disabled(bar->get_value() <= 1.0);
	rome_toolbar_next->set_disabled(bar->get_value() >= end - 1.0);
}

void EditorNode::_rome_scroll_toolbar(int p_direction) {
	if (!rome_toolbar_scroll) {
		return;
	}
	ScrollBar *bar = rome_toolbar_scroll->get_h_scroll_bar();
	const double end = MAX(0.0, bar->get_max() - bar->get_page());
	const double step = MAX(80.0 * EDSCALE, rome_toolbar_scroll->get_size().x * 0.7);
	rome_toolbar_scroll->set_h_scroll(CLAMP(bar->get_value() + p_direction * step, 0.0, end));
	_rome_queue_toolbar_overflow_update();
}

void EditorNode::_rome_toolbar_menu_selected(int p_id) {
	ERR_FAIL_INDEX(p_id, TOOLBAR_OPTION_COUNT);
	Dictionary options;
	options[TOOLBAR_KEYS[p_id]] = !bool(_rome_get_toolbar_options()[TOOLBAR_KEYS[p_id]]);
	_rome_set_toolbar_options(options);
}

void EditorNode::_rome_toolbar_context(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mouse = p_event;
	if (mouse.is_valid() && mouse->is_pressed() && mouse->get_button_index() == MouseButton::RIGHT) {
		rome_toolbar_menu->set_position(DisplayServer::get_singleton()->mouse_get_position());
		rome_toolbar_menu->popup();
	}
}

void EditorNode::_rome_connect_toolbar_tabs() {
	for (int i = 0; i < rome_ribbon_tabs.size(); i++) {
		_rome_style_ribbon_button(rome_ribbon_tabs[i], true);
		const Callable hover = callable_mp(this, &EditorNode::_rome_toolbar_tab_hovered).bind(i);
		if (!rome_ribbon_tabs[i]->is_connected("mouse_entered", hover)) {
			rome_ribbon_tabs[i]->connect("mouse_entered", hover);
		}
		if (i < 6 && !rome_ribbon_tabs[i]->is_connected("gui_input", callable_mp(this, &EditorNode::_rome_toolbar_context))) {
			rome_ribbon_tabs[i]->connect("gui_input", callable_mp(this, &EditorNode::_rome_toolbar_context));
		}
	}
}

void EditorNode::_rome_toolbar_tab_hovered(int p_tab) {
	if (rome_toolbar_collapsed) {
		rome_ribbon_tabs[p_tab]->set_pressed(true);
		_rome_ribbon_tab_changed(p_tab);
	}
}

void EditorNode::_rome_poll_toolbar_hover() {
	if (rome_toolbar->get_global_rect().has_point(rome_toolbar->get_global_mouse_position()) || rome_toolbar_menu->is_visible() || (rome_tool_picker && rome_tool_picker->is_visible()) || (rome_new_tab_dialog && rome_new_tab_dialog->is_visible())) {
		return;
	}
	const TypedArray<Node> popups = rome_toolbar->find_children("*", "Window", true, false);
	for (int i = 0; i < popups.size(); i++) {
		Window *popup = Object::cast_to<Window>(popups[i]);
		if (popup && popup->is_visible()) {
			return;
		}
	}
	rome_toolbar_revealed = false;
	_rome_apply_toolbar_options();
}

void EditorNode::_rome_init_toolbar_appearance(Control *p_header) {
	rome_toolbar_hover_timer = memnew(Timer);
	rome_toolbar_hover_timer->set_wait_time(0.2);
	rome_toolbar_hover_timer->connect("timeout", callable_mp(this, &EditorNode::_rome_poll_toolbar_hover));
	rome_toolbar->add_child(rome_toolbar_hover_timer);
	MenuButton *options = memnew(MenuButton);
	options->set_name("ROMEToolbarOptions");
	options->set_button_icon(theme->get_icon("GuiTabMenu", EditorStringName(EditorIcons)));
	options->set_tooltip_text("Opciones de la barra de herramientas");
	options->set_accessibility_name("Opciones de la barra de herramientas");
	options->set_custom_minimum_size(Size2(28, 26) * EDSCALE);
	_rome_style_ribbon_button(options);
	p_header->add_child(options);
	rome_toolbar_menu = options->get_popup();
	rome_toolbar_menu->set_name("ROMEToolbarAppearanceMenu");
	rome_toolbar_menu->add_check_item("Barra compacta", TOOLBAR_COMPACT);
	rome_toolbar_menu->add_check_item("Mostrar etiquetas", TOOLBAR_LABELS);
	rome_toolbar_menu->add_check_item("Plegar barra de herramientas", TOOLBAR_COLLAPSED);
	rome_toolbar_menu->add_separator();
	rome_toolbar_menu->add_check_item(String::utf8(u8"Alinear pestañas a la izquierda"), TOOLBAR_LEFT_ALIGNED);
	rome_toolbar_menu->set_hide_on_checkable_item_selection(false);
	rome_toolbar_menu->connect("id_pressed", callable_mp(this, &EditorNode::_rome_toolbar_menu_selected));
	rome_toolbar->connect("gui_input", callable_mp(this, &EditorNode::_rome_toolbar_context));
	_rome_connect_toolbar_tabs();
	for (Button *button : { rome_workspace_2d, rome_workspace_3d }) {
		if (button) {
			_rome_style_ribbon_button(button);
		}
	}
	EditorSettings *settings = EditorSettings::get_singleton();
	rome_toolbar_compact = settings->has_setting(TOOLBAR_SETTINGS[0]) && bool(settings->get(TOOLBAR_SETTINGS[0]));
	rome_toolbar_labels = !settings->has_setting(TOOLBAR_SETTINGS[1]) || bool(settings->get(TOOLBAR_SETTINGS[1]));
	rome_toolbar_collapsed = settings->has_setting(TOOLBAR_SETTINGS[TOOLBAR_COLLAPSED]) && bool(settings->get(TOOLBAR_SETTINGS[TOOLBAR_COLLAPSED]));
	rome_tabs_left_aligned = settings->has_setting(TOOLBAR_SETTINGS[TOOLBAR_LEFT_ALIGNED]) && bool(settings->get(TOOLBAR_SETTINGS[TOOLBAR_LEFT_ALIGNED]));
	_rome_apply_toolbar_options();
}
