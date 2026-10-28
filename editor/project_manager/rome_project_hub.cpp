#include "project_manager.h"

#include "core/io/image.h"
#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "core/object/callable_mp.h"
#include "editor/project_manager/project_dialog.h"
#include "editor/project_manager/project_list.h"
#include "editor/project_manager/rome_project_template.h"
#include "editor/themes/editor_scale.h"
#include "editor/themes/editor_icons.h"
#include "editor/settings/editor_settings.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/margin_container.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/style_box.h"
#include "servers/display/display_server.h"

static Ref<StyleBoxFlat> rome_hub_style(const Color &p_color, int p_margin = 12) {
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(p_color);
	style->set_corner_radius_all(7 * EDSCALE);
	style->set_content_margin_all(p_margin * EDSCALE);
	return style;
}

static Ref<Texture2D> rome_hub_image(const String &p_path) {
	if (!FileAccess::exists(p_path)) {
		return Ref<Texture2D>();
	}
	Ref<Image> image = Image::load_from_file(p_path);
	return image.is_valid() && !image->is_empty() ? ImageTexture::create_from_image(image) : Ref<ImageTexture>();
}

static Button *rome_hub_card(const String &p_title, const String &p_detail, const Ref<Texture2D> &p_texture, const Callable &p_action, bool p_template = false) {
	Button *card = memnew(Button);
	card->set_custom_minimum_size((p_template ? Size2(280, 348) : Size2(226, p_detail.is_empty() ? 258 : 286)) * EDSCALE);
	card->add_theme_style_override("normal", rome_hub_style(Color(0.095, 0.10, 0.117)));
	card->add_theme_style_override("hover", rome_hub_style(Color(0.14, 0.15, 0.18)));
	card->add_theme_style_override("pressed", rome_hub_style(Color(0.19, 0.24, 0.37)));
	card->connect("pressed", p_action);
	VBoxContainer *body = memnew(VBoxContainer);
	body->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	body->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT, Control::PRESET_MODE_MINSIZE, 12 * EDSCALE);
	body->add_theme_constant_override("separation", 10 * EDSCALE);
	card->add_child(body);
	TextureRect *image = memnew(TextureRect);
	image->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	image->set_custom_minimum_size((p_template ? Size2(256, 256) : Size2(202, 200)) * EDSCALE);
	image->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	image->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_COVERED);
	image->set_texture(p_texture);
	body->add_child(image);
	Label *title = memnew(Label(p_title));
	if (p_template) { title->add_theme_font_size_override("font_size", 14 * EDSCALE); }
	title->set_clip_text(true);
	title->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	body->add_child(title);
	Label *detail = memnew(Label(p_detail));
	detail->set_visible(!p_detail.is_empty());
	detail->set_clip_text(true);
	if (p_template) { detail->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART); }
	detail->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	detail->add_theme_font_size_override("font_size", 11 * EDSCALE);
	detail->add_theme_color_override("font_color", Color(0.65, 0.67, 0.72));
	body->add_child(detail);
	return card;
}

void ProjectManager::_rome_new_template(const String &p_template) {
	String base = OS::get_singleton()->get_system_dir(OS::SYSTEM_DIR_DOCUMENTS).path_join("ROME/Experiences");
	Ref<DirAccess> directory = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (directory->make_dir_recursive(base) != OK) {
		base = OS::get_singleton()->get_data_path().path_join("ROME/Experiences");
		if (directory->make_dir_recursive(base) != OK) {
			_show_error("No se pudo crear la carpeta de experiencias de ROME.");
			return;
		}
	}
	int number = 1;
	while (directory->dir_exists(base.path_join("Place" + itos(number)))) { number++; }
	const String name = "Place" + itos(number);
	const String path = base.path_join(name);
	Error err = directory->make_dir(path);
	if (err == OK) { err = RomeProjectTemplate::install(path, p_template); }
	if (err != OK) {
		_show_error("No se pudo crear la experiencia. Comprueba el paquete rome_runtime.");
		return;
	}
	ProjectSettings::CustomMap initial;
	RomeProjectTemplate::configure(initial);
	initial["application/config/name"] = name;
	initial["application/config/icon"] = "res://icon.svg";
	initial["application/config/features"] = PackedStringArray({ "4.8", "Forward Plus" });
	initial["rendering/renderer/rendering_method"] = "forward_plus";
	ConfigFile configuration;
	configuration.set_value("", "config_version", 5);
	for (const KeyValue<String, Variant> &setting : initial) {
		const int slash = setting.key.find_char('/');
		configuration.set_value(setting.key.substr(0, slash), setting.key.substr(slash + 1), setting.value);
	}
	err = configuration.save(path.path_join("project.godot"));
	if (err != OK) { _show_error("No se pudo guardar la experiencia."); return; }
	Ref<FileAccess> icon = FileAccess::open(path.path_join("icon.svg"), FileAccess::WRITE);
	if (icon.is_valid()) { icon->store_string(get_default_project_icon()); icon->close(); }
	_on_project_created(path, true);
}

void ProjectManager::_rome_open_project(const String &p_path) {
	for (int i = 0; i < project_list->_projects.size(); i++) {
		if (project_list->_projects[i].path == p_path) {
			project_list->_clear_project_selection();
			project_list->select_project(i);
			_open_selected_projects_check_recovery_mode();
			return;
		}
	}
}

void ProjectManager::_rome_filter(const String &p_text) {
	_rome_refresh_cards();
}

void ProjectManager::_rome_menu_action(int p_action) {
	if (p_action == 0) {
		_rome_show_page(2);
	} else if (p_action == 1) {
		_import_project();
	} else if (p_action == 2) {
		_show_quick_settings();
	} else if (p_action == 3) {
		_show_about();
	}
}

void ProjectManager::_rome_refresh_cards() {
	if (!rome_project_cards) {
		return;
	}
	for (int i = rome_project_cards->get_child_count() - 1; i >= 0; i--) {
		Node *child = rome_project_cards->get_child(i);
		rome_project_cards->remove_child(child);
		child->queue_free();
	}
	const String query = rome_search->get_text().to_lower();
	int visible_count = 0;
	for (const ProjectList::Item &project : project_list->_projects) {
		if (project.missing || (!query.is_empty() && !project.project_name.to_lower().contains(query))) {
			continue;
		}
		Ref<Texture2D> texture = rome_hub_image(project.path.path_join(".rome/thumbnail.png"));
		if (texture.is_null()) {
			String icon_path = project.icon.replace("res://", project.path + "/");
			texture = rome_hub_image(icon_path);
		}
		if (texture.is_null()) {
			texture = rome_hub_image(RomeProjectTemplate::get_root().path_join("hub/baseplate.svg"));
		}
		Button *card = rome_hub_card(project.project_name, project.get_last_edited_string(), texture, callable_mp(this, &ProjectManager::_rome_open_project).bind(project.path));
		card->set_tooltip_text(project.path);
		rome_project_cards->add_child(card);
		visible_count++;
	}
	if (visible_count == 0) {
		Label *empty = memnew(Label(query.is_empty() ? String::utf8(u8"Tus experiencias aparecer\u00e1n aqu\u00ed. Elige una plantilla para comenzar.") : "No se encontraron experiencias."));
		empty->set_custom_minimum_size(Size2(0, 60) * EDSCALE);
		rome_project_cards->add_child(empty);
	}
}

void ProjectManager::_rome_show_page(int p_page) {
	rome_page = p_page;
	rome_heading->set_text(p_page == 0 ? "Te damos la bienvenida a ROME Studio" : p_page == 1 ? "Recientes" : "Todas las plantillas");
	rome_banner->set_visible(p_page == 0);
	rome_recent_section->set_visible(p_page != 2);
	rome_templates_section->set_visible(p_page != 1);
	rome_search->set_visible(p_page == 1);
	_rome_refresh_cards();
}

void ProjectManager::_rome_build_hub() {
	for (int i = 0; i < main_vbox->get_child_count(); i++) {
		Control *old_control = Object::cast_to<Control>(main_vbox->get_child(i));
		if (old_control) { old_control->hide(); }
	}
	DisplayServer::get_singleton()->window_set_flag(DisplayServerEnums::WINDOW_FLAG_EXTEND_TO_TITLE, false);
	main_vbox->add_theme_constant_override("separation", 0);
	HBoxContainer *menu_row = memnew(HBoxContainer);
	menu_row->set_custom_minimum_size(Size2(0, 20) * EDSCALE);
	menu_row->add_theme_constant_override("separation", 0);
	main_vbox->add_child(menu_row);
	MenuButton *file = memnew(MenuButton);
	file->set_text("Archivo");
	file->add_theme_font_size_override("font_size", 12 * EDSCALE);
	file->get_popup()->add_item("Nueva experiencia", 0);
	file->get_popup()->add_item("Abrir experiencia...", 1);
	file->get_popup()->add_item("Ajustes de Studio", 2);
	file->get_popup()->connect("id_pressed", callable_mp(this, &ProjectManager::_rome_menu_action));
	menu_row->add_child(file);
	Button *plugins = memnew(Button);
	plugins->set_text("Plugins");
	plugins->set_flat(true);
	plugins->add_theme_font_size_override("font_size", 12 * EDSCALE);
	plugins->connect("pressed", callable_mp(this, &ProjectManager::_show_quick_settings));
	menu_row->add_child(plugins);
	Button *help = memnew(Button);
	help->set_text("Ayuda");
	help->set_flat(true);
	help->add_theme_font_size_override("font_size", 12 * EDSCALE);
	help->connect("pressed", callable_mp(this, &ProjectManager::_show_about));
	menu_row->add_child(help);
	for (Button *menu_button : { static_cast<Button *>(file), plugins, help }) {
		Ref<StyleBoxFlat> menu_style = rome_hub_style(Color(0, 0, 0, 0), 0);
		menu_style->set_content_margin(SIDE_LEFT, 7 * EDSCALE);
		menu_style->set_content_margin(SIDE_RIGHT, 7 * EDSCALE);
		menu_button->add_theme_style_override("normal", menu_style);
		menu_button->add_theme_font_size_override("font_size", 12 * EDSCALE);
	}
	PanelContainer *top_band = memnew(PanelContainer);
	top_band->set_custom_minimum_size(Size2(0, 36) * EDSCALE);
	top_band->add_theme_style_override("panel", rome_hub_style(Color(0.095, 0.10, 0.117), 0));
	main_vbox->add_child(top_band);
	rome_hub = memnew(HBoxContainer);
	Ref<Theme> hub_theme;
	hub_theme.instantiate();
	hub_theme->set_color("font_color", "Label", Color(0.94, 0.94, 0.95));
	hub_theme->set_color("font_color", "Button", Color(0.86, 0.87, 0.90));
	rome_hub->set_theme(hub_theme);
	rome_hub->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	rome_hub->add_theme_constant_override("separation", 0);
	main_vbox->add_child(rome_hub);
	PanelContainer *sidebar_panel = memnew(PanelContainer);
	sidebar_panel->set_custom_minimum_size(Size2(232, 0) * EDSCALE);
	Ref<StyleBoxFlat> sidebar_style = rome_hub_style(Color(0.063, 0.067, 0.078), 12);
	sidebar_style->set_corner_radius_all(0);
	sidebar_style->set_border_width(SIDE_RIGHT, 1);
	sidebar_style->set_border_color(Color(0.16, 0.17, 0.19));
	sidebar_panel->add_theme_style_override("panel", sidebar_style);
	rome_hub->add_child(sidebar_panel);
	VBoxContainer *sidebar = memnew(VBoxContainer);
	sidebar->add_theme_constant_override("separation", 10 * EDSCALE);
	sidebar_panel->add_child(sidebar);
	Label *brand = memnew(Label("ROME Studio"));
	brand->add_theme_font_size_override("font_size", 17 * EDSCALE);
	brand->add_theme_font_override("font", theme->get_font("bold", "EditorFonts"));
	HBoxContainer *brand_row = memnew(HBoxContainer);
	brand_row->set_custom_minimum_size(Size2(0, 64) * EDSCALE);
	brand_row->add_theme_constant_override("separation", 8 * EDSCALE);
	sidebar->add_child(brand_row);
	TextureRect *brand_icon = memnew(TextureRect);
	brand_icon->set_custom_minimum_size(Size2(28, 28) * EDSCALE);
	brand_icon->set_v_size_flags(Control::SIZE_SHRINK_CENTER);
	brand_icon->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	brand_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	brand_icon->set_texture(theme->get_icon("ROME", "EditorIcons"));
	brand_row->add_child(brand_icon);
	brand_row->add_child(brand);
	auto navigation = [this, sidebar](const String &p_title, const StringName &p_icon, const Callable &p_action) {
		Button *button = memnew(Button);
		button->set_text(p_title);
		button->set_text_alignment(HORIZONTAL_ALIGNMENT_LEFT);
		button->set_custom_minimum_size(Size2(0, 46) * EDSCALE);
		button->set_button_icon(theme->get_icon(p_icon, "EditorIcons"));
		button->add_theme_constant_override("h_separation", 14 * EDSCALE);
		button->add_theme_font_override("font", theme->get_font("bold", "EditorFonts"));
		button->add_theme_font_size_override("font_size", 14 * EDSCALE);
		button->set_flat(true);
		button->connect("pressed", p_action);
		sidebar->add_child(button);
		return button;
	};
	navigation("Nueva experiencia", "Add", callable_mp(this, &ProjectManager::_rome_show_page).bind(2));
	navigation("Recientes", "History", callable_mp(this, &ProjectManager::_rome_show_page).bind(1));
	navigation("Inicio", "Home", callable_mp(this, &ProjectManager::_rome_show_page).bind(0));
	navigation("Experiencias", "Folder", callable_mp(this, &ProjectManager::_rome_show_page).bind(1));
	navigation("Plantillas", "Grid", callable_mp(this, &ProjectManager::_rome_show_page).bind(2));
	navigation("Abrir archivo", "Load", callable_mp(this, &ProjectManager::_import_project));
	Control *spacer = memnew(Control);
	spacer->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	sidebar->add_child(spacer);
	navigation("Ajustes de Studio", "Tools", callable_mp(this, &ProjectManager::_show_quick_settings));

	PanelContainer *content_panel = memnew(PanelContainer);
	content_panel->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content_panel->add_theme_style_override("panel", rome_hub_style(Color(0.063, 0.067, 0.078), 24));
	rome_hub->add_child(content_panel);
	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll->add_theme_style_override("panel", memnew(StyleBoxEmpty));
	content_panel->add_child(scroll);
	VBoxContainer *content = memnew(VBoxContainer);
	content->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	content->add_theme_constant_override("separation", 24 * EDSCALE);
	scroll->add_child(content);
	HBoxContainer *heading_row = memnew(HBoxContainer);
	content->add_child(heading_row);
	rome_heading = memnew(Label);
	rome_heading->add_theme_font_size_override("font_size", 25 * EDSCALE);
	rome_heading->add_theme_font_override("font", theme->get_font("bold", "EditorFonts"));
	rome_heading->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	heading_row->add_child(rome_heading);
	rome_search = memnew(LineEdit);
	rome_search->set_placeholder("Buscar");
	rome_search->set_custom_minimum_size(Size2(220, 0) * EDSCALE);
	rome_search->connect("text_changed", callable_mp(this, &ProjectManager::_rome_filter));
	heading_row->add_child(rome_search);
	PanelContainer *banner = memnew(PanelContainer);
	rome_banner = banner;
	banner->set_custom_minimum_size(Size2(0, 224) * EDSCALE);
	banner->add_theme_style_override("panel", rome_hub_style(Color(0.09, 0.14, 0.21), 0));
	content->add_child(banner);
	TextureRect *banner_image = memnew(TextureRect);
	banner_image->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	banner_image->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_COVERED);
	banner_image->set_texture(rome_hub_image(RomeProjectTemplate::get_root().path_join("hub/hero.png")));
	banner->add_child(banner_image);
	ColorRect *shade = memnew(ColorRect);
	shade->set_color(Color(0.025, 0.035, 0.055, 0.52));
	shade->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	banner->add_child(shade);
	MarginContainer *banner_margin = memnew(MarginContainer);
	for (const String &side : { String("left"), String("right"), String("top"), String("bottom") }) {
		banner_margin->add_theme_constant_override("margin_" + side, 32 * EDSCALE);
	}
	banner->add_child(banner_margin);
	HBoxContainer *banner_row = memnew(HBoxContainer);
	banner_margin->add_child(banner_row);
	VBoxContainer *banner_copy = memnew(VBoxContainer);
	banner_copy->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	banner_copy->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	banner_copy->add_theme_constant_override("separation", 14 * EDSCALE);
	banner_row->add_child(banner_copy);
	Label *banner_title = memnew(Label(String::utf8(u8"Crea tu pr\u00f3xima experiencia")));
	banner_title->add_theme_font_size_override("font_size", 28 * EDSCALE);
	banner_title->add_theme_font_override("font", theme->get_font("bold", "EditorFonts"));
	banner_copy->add_child(banner_title);
	Label *banner_detail = memnew(Label("Construye mundos, crea objetos y programa en Luau.\nTodo listo desde la primera experiencia."));
	banner_copy->add_child(banner_detail);
	Button *start = memnew(Button);
	start->set_text("Nueva experiencia");
	start->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	start->set_custom_minimum_size(Size2(180, 38) * EDSCALE);
	start->add_theme_style_override("normal", rome_hub_style(Color(0.20, 0.36, 1.0), 10));
	start->add_theme_style_override("hover", rome_hub_style(Color(0.29, 0.44, 1.0), 10));
	start->connect("pressed", callable_mp(this, &ProjectManager::_rome_show_page).bind(2));
	banner_copy->add_child(start);
	rome_recent_section = memnew(VBoxContainer);
	rome_recent_section->add_theme_constant_override("separation", 18 * EDSCALE);
	content->add_child(rome_recent_section);
	Label *recent_title = memnew(Label("Mis experiencias recientes"));
	recent_title->add_theme_font_size_override("font_size", 20 * EDSCALE);
	recent_title->add_theme_font_override("font", theme->get_font("bold", "EditorFonts"));
	rome_recent_section->add_child(recent_title);
	rome_project_cards = memnew(HFlowContainer);
	rome_project_cards->add_theme_constant_override("h_separation", 14 * EDSCALE);
	rome_project_cards->add_theme_constant_override("v_separation", 14 * EDSCALE);
	rome_recent_section->add_child(rome_project_cards);
	rome_templates_section = memnew(VBoxContainer);
	rome_templates_section->add_theme_constant_override("separation", 18 * EDSCALE);
	content->add_child(rome_templates_section);
	Label *templates_title = memnew(Label("Abrir una plantilla"));
	templates_title->add_theme_font_size_override("font_size", 20 * EDSCALE);
	templates_title->add_theme_font_override("font", theme->get_font("bold", "EditorFonts"));
	rome_templates_section->add_child(templates_title);
	HFlowContainer *templates = memnew(HFlowContainer);
	templates->add_theme_constant_override("h_separation", 14 * EDSCALE);
	templates->add_theme_constant_override("v_separation", 14 * EDSCALE);
	rome_templates_section->add_child(templates);
	const char *ids[] = { "baseplate", "empty", "obby" };
	const char *names[] = { "Baseplate", "En blanco", "Classic Obby" };
	const char *details[] = { "Suelo cuadriculado y punto de inicio.", "Un mundo limpio para crear desde cero.", "Saltos, plataformas moviles y checkpoints." };
	for (int i = 0; i < 3; i++) {
		templates->add_child(rome_hub_card(names[i], details[i], rome_hub_image(RomeProjectTemplate::get_root().path_join("hub/" + String(ids[i]) + ".png")), callable_mp(this, &ProjectManager::_rome_new_template).bind(String(ids[i])), true));
	}
	project_list->connect(ProjectList::SIGNAL_LIST_CHANGED, callable_mp(this, &ProjectManager::_rome_refresh_cards), CONNECT_DEFERRED);
	_rome_show_page(0);
}
