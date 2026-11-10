#include "project_manager.h"

#include "core/io/image.h"
#include "core/io/file_access.h"
#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "core/object/callable_mp.h"
#include "editor/project_manager/project_dialog.h"
#include "editor/project_manager/project_list.h"
#include "editor/project_manager/rome_project_template.h"
#include "editor/rome_update_manager.h"
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
#include "scene/gui/option_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/gradient.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/style_box_flat.h"
#include "scene/resources/style_box.h"
#include "scene/main/scene_tree.h"
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

static Ref<Texture2D> rome_hub_template_image(const String &p_template) {
	const String base = RomeProjectTemplate::get_root().path_join("hub/" + p_template);
	Ref<Texture2D> texture = rome_hub_image(base + ".png");
	if (texture.is_valid()) {
		return texture;
	}
	// Vector previews ship in the editor too, so new templates remain visible
	// with an existing sidecar runtime and in standalone distributions.
	String svg;
	if (p_template == "2d") {
		svg = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
<defs><linearGradient id="sky" x2="0" y2="1"><stop stop-color="#183856"/><stop offset="1" stop-color="#0f1b30"/></linearGradient></defs>
<rect width="512" height="512" fill="url(#sky)"/>
<g stroke="#b4d6ee" stroke-opacity=".08" stroke-width="1"><path d="M0 64H512M0 128H512M0 192H512M0 256H512M0 320H512M0 384H512M0 448H512M64 0V512M128 0V512M192 0V512M256 0V512M320 0V512M384 0V512M448 0V512"/></g>
<circle cx="402" cy="104" r="36" fill="#ffd379"/>
<path d="M0 344l84-70 96 96 116-172 108 120 108-90V512H0z" fill="#223d56"/>
<path d="M0 414l118-80 90 72 121-104 183 92V512H0z" fill="#2d5267"/>
<path d="M0 438H512V512H0z" fill="#152c3c"/><path d="M0 438H512" stroke="#7ed6b5" stroke-width="12"/>
<rect x="57" y="343" width="120" height="24" rx="6" fill="#67cfb5"/><rect x="224" y="265" width="104" height="24" rx="6" fill="#74dbcb"/><rect x="369" y="191" width="94" height="24" rx="6" fill="#91e4dc"/>
<rect x="248" y="206" width="42" height="53" rx="9" fill="#ffad62"/><path d="M258 220h6m10 0h6" stroke="#26344a" stroke-width="5" stroke-linecap="round"/>
<path d="M145 310q57-100 105-40m51-47q40-104 87-38" fill="none" stroke="#d6f4ee" stroke-opacity=".65" stroke-width="4" stroke-dasharray="7 9"/>
</svg>)SVG";
	} else if (p_template == "mixed") {
		svg = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
<defs><linearGradient id="sky" x2="0" y2="1"><stop stop-color="#24375b"/><stop offset="1" stop-color="#111c32"/></linearGradient></defs>
<rect width="512" height="512" fill="url(#sky)"/>
<path d="M0 400L512 160V512H0z" fill="#1e3a51"/>
<g stroke="#82a5c6" stroke-opacity=".18"><path d="M0 453L512 213M0 506L512 266M86 512L512 313M200 512L512 366M314 512L512 419M428 512L512 472M0 342L362 512M0 269L512 509M0 196L512 436M0 123L512 363"/></g>
<rect x="44" y="102" width="202" height="238" rx="17" fill="#10283a" stroke="#6bd5bf" stroke-width="4"/>
<path d="M61 309H228" stroke="#67cfb5" stroke-width="11"/><path d="M77 261h71m28-65h49" stroke="#91e4dc" stroke-width="13" stroke-linecap="round"/>
<rect x="110" y="213" width="28" height="37" rx="6" fill="#ffad62"/>
<path d="M274 262l98-57 98 57-98 57z" fill="#a6c8ff"/><path d="M274 262v111l98 57V319z" fill="#5487d5"/><path d="M372 319l98-57v111l-98 57z" fill="#365998"/>
<path d="M253 176l32 16-32 16m15-16h47" fill="none" stroke="#ffcc79" stroke-width="7" stroke-linecap="round" stroke-linejoin="round"/>
<circle cx="372" cy="112" r="30" fill="#80d8ce" fill-opacity=".16"/><path d="M358 112h28m-14-14v28" stroke="#80d8ce" stroke-width="5" stroke-linecap="round"/>
</svg>)SVG";
	} else {
		return rome_hub_image(base + ".svg");
	}
	Ref<Image> image;
	image.instantiate();
	return image->load_svg_from_string(svg) == OK ? ImageTexture::create_from_image(image) : Ref<ImageTexture>();
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
	RomeProjectTemplate::configure(initial, p_template);
	initial["application/config/name"] = name;
	initial["application/config/icon"] = "res://icon.svg";
	const bool pure_2d = p_template == "2d";
	initial["application/config/features"] = PackedStringArray({ "4.8", pure_2d ? "GL Compatibility" : "Forward Plus" });
	initial["rendering/renderer/rendering_method"] = pure_2d ? "gl_compatibility" : "forward_plus";
	if (pure_2d) {
		initial["rendering/renderer/rendering_method.mobile"] = "gl_compatibility";
	}
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
	} else if (p_action == 4) {
		ROMEUpdateManager::install(this)->show_update_dialog();
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
	Vector<ProjectList::Item> projects = project_list->_projects;
	auto compare = [this](const ProjectList::Item &a, const ProjectList::Item &b) {
		if (rome_sort >= 4) {
			const int64_t left = rome_project_sizes.get(a.path, 0);
			const int64_t right = rome_project_sizes.get(b.path, 0);
			return left == right ? a.path < b.path : (rome_sort == 4 ? left > right : left < right);
		}
		if (rome_sort == 1 || rome_sort == 2) {
			const int comparison = a.project_name.naturalnocasecmp_to(b.project_name);
			return comparison == 0 ? a.path < b.path : (rome_sort == 1 ? comparison < 0 : comparison > 0);
		}
		return a.last_edited == b.last_edited ? a.path < b.path : (rome_sort == 3 ? a.last_edited < b.last_edited : a.last_edited > b.last_edited);
	};
	projects.sort_custom<decltype(compare)>(compare);
	for (const ProjectList::Item &project : projects) {
		if (!query.is_empty() && !project.project_name.to_lower().contains(query)) {
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
		Button *card = rome_hub_card(project.project_name, project.missing ? "No disponible en el disco" : project.get_last_edited_string(), texture, callable_mp(this, &ProjectManager::_rome_open_project).bind(project.path));
		card->set_disabled(project.missing);
		card->set_tooltip_text(project.path);
		if (rome_project_sizes.has(project.path)) {
			card->set_tooltip_text(project.path + "\n" + String::humanize_size(int64_t(rome_project_sizes[project.path])) + " en disco");
		}
		rome_project_cards->add_child(card);
		MenuButton *actions = memnew(MenuButton);
		actions->set_text(String::utf8("⋮"));
		actions->set_tooltip_text("Administrar experiencia");
		card->add_child(actions);
		actions->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
		actions->set_custom_minimum_size(Size2(24, 24) * EDSCALE);
		actions->set_position(Vector2(190, 220) * EDSCALE);
		actions->get_popup()->add_item("Abrir carpeta", 0);
		actions->get_popup()->add_item(String::utf8("Quitar o eliminar…"), 1);
		actions->get_popup()->connect("id_pressed", callable_mp(this, &ProjectManager::_rome_project_action).bind(project.path));
		visible_count++;
	}
	if (visible_count == 0) {
		Label *empty = memnew(Label(query.is_empty() ? String::utf8(u8"Tus experiencias aparecer\u00e1n aqu\u00ed. Elige una plantilla para comenzar.") : "No se encontraron experiencias."));
		empty->set_custom_minimum_size(Size2(0, 60) * EDSCALE);
		rome_project_cards->add_child(empty);
	}
}

void ProjectManager::_rome_project_action(int p_action, const String &p_path) {
	if (p_action == 0) {
		OS::get_singleton()->shell_show_in_file_manager(p_path, true);
		return;
	}
	rome_remove_path = p_path;
	if (!rome_remove_dialog) {
		rome_remove_dialog = memnew(ConfirmationDialog);
		rome_remove_dialog->set_title("Quitar o eliminar experiencia");
		rome_remove_dialog->set_ok_button_text("Confirmar");
		add_child(rome_remove_dialog);
		VBoxContainer *body = memnew(VBoxContainer);
		body->add_theme_constant_override("separation", 18 * EDSCALE);
		rome_remove_dialog->add_child(body);
		Label *info = memnew(Label);
		info->set_name("ROMEProjectRemoveInfo");
		info->set_custom_minimum_size(Size2(580, 0) * EDSCALE);
		info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		body->add_child(info);
		rome_remove_mode = memnew(OptionButton);
		rome_remove_mode->add_item("Solo quitar de ROME (conservar archivos)");
		rome_remove_mode->add_item("Quitar de ROME y enviar carpeta a la Papelera");
		rome_remove_mode->add_item("Solo enviar carpeta a la Papelera (conservar registro)");
		body->add_child(rome_remove_mode);
		rome_remove_dialog->connect("confirmed", callable_mp(this, &ProjectManager::_rome_remove_confirmed));
	}
	rome_remove_mode->select(0);
	Object::cast_to<Label>(rome_remove_dialog->find_child("ROMEProjectRemoveInfo", true, false))->set_text("Carpeta completa de la experiencia:\n" + p_path + "\n\nLa Papelera incluye todos los archivos y subcarpetas de esta ruta.");
	rome_remove_dialog->popup_centered(Size2i(620, 230) * EDSCALE);
}

void ProjectManager::_rome_sort_changed(int p_index) {
	rome_sort = p_index;
	EditorSettings::get_singleton()->set_setting("rome/hub/project_sort", p_index);
	if (p_index >= 4 && (rome_size_task == WorkerThreadPool::INVALID_TASK_ID || WorkerThreadPool::get_singleton()->is_task_completed(rome_size_task))) {
		if (rome_size_task != WorkerThreadPool::INVALID_TASK_ID) {
			WorkerThreadPool::get_singleton()->wait_for_task_completion(rome_size_task);
		}
		Vector<String> paths;
		for (const ProjectList::Item &project : project_list->_projects) {
			paths.push_back(project.path);
		}
		rome_size_task = WorkerThreadPool::get_singleton()->add_template_task(this, &ProjectManager::_rome_measure_sizes, paths, false, "Measuring project sizes");
	}
	_rome_refresh_cards();
}

void ProjectManager::_rome_measure_sizes(Vector<String> p_paths) {
	Dictionary sizes;
	for (const String &path : p_paths) {
		uint64_t total = 0;
		Vector<String> pending;
		pending.push_back(path);
		while (!pending.is_empty() && !rome_size_cancel.is_set()) {
			const String current = pending[pending.size() - 1];
			pending.resize(pending.size() - 1);
			Ref<DirAccess> directory = DirAccess::open(current);
			if (directory.is_null()) {
				continue;
			}
			directory->list_dir_begin();
			for (String entry = directory->get_next(); !entry.is_empty() && !rome_size_cancel.is_set(); entry = directory->get_next()) {
				if (entry == "." || entry == ".." || directory->is_link(entry)) {
					continue;
				}
				const String file = current.path_join(entry);
				if (directory->current_is_dir()) {
					pending.push_back(file);
				} else {
					Ref<FileAccess> opened = FileAccess::open(file, FileAccess::READ);
					if (opened.is_valid()) {
						total += opened->get_length();
					}
				}
			}
			directory->list_dir_end();
		}
		sizes[path] = int64_t(total);
	}
	if (!rome_size_cancel.is_set()) {
		callable_mp(this, &ProjectManager::_rome_sizes_ready).call_deferred(sizes);
	}
}

void ProjectManager::_rome_sizes_ready(const Dictionary &p_sizes) {
	rome_project_sizes = p_sizes;
	_rome_refresh_cards();
}

void ProjectManager::_rome_remove_confirmed() {
	const int mode = rome_remove_mode->get_selected();
	for (int i = 0; i < project_list->_projects.size(); i++) {
		if (project_list->_projects[i].path != rome_remove_path) {
			continue;
		}
		if (mode != 0) {
			const String path = rome_remove_path.simplify_path().trim_suffix("/");
			// Require a concrete project folder. Never trash a drive or a home folder.
			if (!path.is_absolute_path() || path.get_base_dir() == path || path.length() < 5 ||
					path == OS::get_singleton()->get_system_dir(OS::SYSTEM_DIR_DOCUMENTS) ||
					path == OS::get_singleton()->get_environment("USERPROFILE") ||
					path == OS::get_singleton()->get_environment("HOME") ||
					!FileAccess::exists(path.path_join("project.godot"))) {
				_show_error("No se puede eliminar esta ruta. Debe ser una carpeta de proyecto existente.");
				return;
			}
			const Error error = OS::get_singleton()->move_to_trash(path);
			if (error != OK) {
				_show_error("No se pudo enviar la carpeta a la Papelera. El registro se ha conservado.");
				return;
			}
		}
		if (mode == 2) {
			project_list->_projects.write[i].missing = true;
		} else {
			project_list->_remove_project(i, true);
		}
		_rome_refresh_cards();
		return;
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
	MenuButton *help = memnew(MenuButton);
	help->set_text("Ayuda");
	help->set_flat(true);
	help->add_theme_font_size_override("font_size", 12 * EDSCALE);
	help->get_popup()->add_item("Actualizaciones de ROME...", 4);
	help->get_popup()->add_item("Acerca de ROME Engine", 3);
	help->get_popup()->connect("id_pressed", callable_mp(this, &ProjectManager::_rome_menu_action));
	menu_row->add_child(help);
	for (Button *menu_button : { static_cast<Button *>(file), plugins, static_cast<Button *>(help) }) {
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
	String hero_path = RomeProjectTemplate::get_root().path_join("hub/hero-art.png");
	if (!FileAccess::exists(hero_path)) {
		hero_path = RomeProjectTemplate::get_root().path_join("hub/hero.png");
	}
	banner_image->set_texture(rome_hub_image(hero_path));
	banner->add_child(banner_image);
	Ref<Gradient> gradient;
	gradient.instantiate();
	gradient->set_color(0, Color(0.025, 0.035, 0.055, 0.82));
	gradient->set_color(1, Color(0.025, 0.035, 0.055, 0.04));
	gradient->set_offset(1, 0.75);
	Ref<GradientTexture2D> scrim;
	scrim.instantiate();
	scrim->set_gradient(gradient);
	scrim->set_width(512);
	scrim->set_height(1);
	scrim->set_fill_from(Vector2(0, 0));
	scrim->set_fill_to(Vector2(1, 0));
	TextureRect *shade = memnew(TextureRect);
	shade->set_texture(scrim);
	shade->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	shade->set_stretch_mode(TextureRect::STRETCH_SCALE);
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
	OptionButton *sort = memnew(OptionButton);
	sort->set_name("ROMEProjectSort");
	sort->add_item(String::utf8("Última edición · más recientes"));
	sort->add_item(String::utf8("Nombre · A–Z"));
	sort->add_item(String::utf8("Nombre · Z–A"));
	sort->add_item(String::utf8("Última edición · más antiguos"));
	sort->add_item(String::utf8("Tamaño en disco · mayor primero"));
	sort->add_item(String::utf8("Tamaño en disco · menor primero"));
	sort->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	rome_sort = EditorSettings::get_singleton()->has_setting("rome/hub/project_sort") ? CLAMP(int(EditorSettings::get_singleton()->get_setting("rome/hub/project_sort")), 0, 5) : 0;
	sort->select(rome_sort);
	sort->connect("item_selected", callable_mp(this, &ProjectManager::_rome_sort_changed));
	rome_recent_section->add_child(sort);
	if (rome_sort >= 4) {
		callable_mp(this, &ProjectManager::_rome_sort_changed).call_deferred(rome_sort);
	}
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
	const char *ids[] = { "baseplate", "empty", "obby", "2d", "mixed" };
	const char *names[] = { "Baseplate", "En blanco", "Classic Obby", "Escena 2D", "2D + 3D" };
	const char *details[] = { "Suelo cuadriculado y punto de inicio.", "Un mundo limpio para crear desde cero.", "Saltos, plataformas moviles y checkpoints.", "Un espacio 2D con suelo, colisiones y camara.", "Una base para combinar mundos y escenas 2D y 3D." };
	for (int i = 0; i < 5; i++) {
		templates->add_child(rome_hub_card(names[i], details[i], rome_hub_template_image(ids[i]), callable_mp(this, &ProjectManager::_rome_new_template).bind(String(ids[i])), true));
	}
	project_list->connect(ProjectList::SIGNAL_LIST_CHANGED, callable_mp(this, &ProjectManager::_rome_refresh_cards), CONNECT_DEFERRED);
	_rome_show_page(0);
	ROMEUpdateManager::install(this)->set_quit_callback(callable_mp(SceneTree::get_singleton(), &SceneTree::quit).bind(0));
}
