#include "editor_scene_tabs.h"

#include "core/object/callable_mp.h"
#include "core/object/script_language.h"
#include "editor/editor_interface.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/2d/node_2d.h"
#include "scene/gui/control.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/tab_bar.h"

void EditorSceneTabs::_rome_update_documents() {
	ScriptEditor *scripts = ScriptEditor::get_singleton();
	if (!rome_document_tabs || !scripts || !is_inside_tree()) {
		return;
	}
	if (!rome_script_signals_connected) {
		scripts->connect("script_close", callable_mp(this, &EditorSceneTabs::_rome_script_closed));
		rome_script_signals_connected = true;
	}
	rome_scripts = scripts->get_open_scripts();
	const int scenes = scene_tabs->get_tab_count();
	const int count = scenes + rome_scripts.size();
	rome_document_tabs->set_block_signals(true);
	if (rome_document_tabs->get_tab_count() != count) {
		rome_document_tabs->set_tab_count(count);
	}
	auto apply = [this](int p_tab, const String &p_title, const Ref<Texture2D> &p_icon, const String &p_tooltip) {
		// This runs several times a second; unchanged tabs must not relayout the bar.
		if (rome_document_tabs->get_tab_title(p_tab) != p_title) {
			rome_document_tabs->set_tab_title(p_tab, p_title);
		}
		if (rome_document_tabs->get_tab_icon(p_tab) != p_icon) {
			rome_document_tabs->set_tab_icon(p_tab, p_icon);
		}
		if (rome_document_tabs->get_tab_tooltip(p_tab) != p_tooltip) {
			rome_document_tabs->set_tab_tooltip(p_tab, p_tooltip);
		}
	};
	for (int i = 0; i < scenes; i++) {
		apply(i, scene_tabs->get_tab_title(i), scene_tabs->get_tab_icon(i), EditorNode::get_editor_data().get_scene_path(i));
	}
	for (int i = 0; i < rome_scripts.size(); i++) {
		const Ref<Script> script = rome_scripts[i];
		String name = script->get_meta("rome_script_name", script->get_name());
		if (name.is_empty()) {
			name = script->get_path().get_file().get_basename();
		}
		// Studio names a script tab after the object; its class shows as the icon.
		const String type = script->get_meta("rome_script_type", String());
		String label = name;
		ScriptEditorBase *editor = scripts->get_script_container()->get_resource_editor(script);
		if (editor && editor->is_unsaved()) {
			label += "(*)";
		}
		Ref<Texture2D> icon = type.is_empty() ? Ref<Texture2D>() : EditorNode::get_singleton()->get_class_icon(type);
		if (icon.is_null()) {
			icon = get_editor_theme_icon("Script");
		}
		apply(scenes + i, label, icon, name + (type.is_empty() ? String() : " (" + type + ")") + "\n" + script->get_path());
	}
	int selected = scene_tabs->get_current_tab();
	if (EditorNode::get_editor_main_screen()->get_current_tab_control() == scripts) {
		ScriptEditorBase *current = scripts->get_current_editor();
		if (current) {
			const int index = rome_scripts.find(current->get_edited_resource());
			if (index >= 0) {
				selected = scenes + index;
			}
		}
	}
	if (selected >= 0 && selected < count && rome_document_tabs->get_current_tab() != selected) {
		rome_document_tabs->set_current_tab(selected);
		rome_document_tabs->ensure_tab_visible(selected);
	}
	rome_document_tabs->set_block_signals(false);
	_scene_tabs_resized();
}

void EditorSceneTabs::_rome_show_scene() {
	Node *root = EditorNode::get_editor_data().get_edited_scene_root();
	EditorInterface::get_singleton()->set_main_screen_editor(root && (Object::cast_to<Node2D>(root) || Object::cast_to<Control>(root)) ? "2D" : "3D");
	_rome_update_documents();
}

void EditorSceneTabs::_rome_document_selected(int p_tab) {
	const int scenes = scene_tabs->get_tab_count();
	if (p_tab < scenes) {
		ERR_FAIL_INDEX(p_tab, scenes);
		if (scene_tabs->get_current_tab() != p_tab) {
			scene_tabs->set_current_tab(p_tab);
		}
		_rome_show_scene();
		callable_mp(this, &EditorSceneTabs::_rome_show_scene).call_deferred();
	} else {
		const int script_index = p_tab - scenes;
		ERR_FAIL_INDEX(script_index, rome_scripts.size());
		ScriptEditor::get_singleton()->edit(rome_scripts[script_index], true);
		ScriptEditor::get_singleton()->focus_script_editor(rome_scripts[script_index]);
		_rome_update_documents();
	}
}

void EditorSceneTabs::_rome_document_closed(int p_tab) {
	const int scenes = scene_tabs->get_tab_count();
	if (p_tab < scenes) {
		ERR_FAIL_INDEX(p_tab, scenes);
		_scene_tab_closed(p_tab);
	} else {
		const int script_index = p_tab - scenes;
		ERR_FAIL_INDEX(script_index, rome_scripts.size());
		ScriptEditor::get_singleton()->request_close_resource(rome_scripts[script_index]);
	}
}

void EditorSceneTabs::_rome_script_closed(const Ref<Script> &p_script) {
	ScriptEditor *scripts = ScriptEditor::get_singleton();
	ScriptEditorBase *current = scripts->get_current_editor();
	if (EditorNode::get_editor_main_screen()->get_current_tab_control() == scripts && current && current->get_edited_resource() == p_script) {
		// The close signal precedes document removal and history restoration.
		// Return after those operations so a neighboring script cannot steal focus.
		callable_mp(this, &EditorSceneTabs::_rome_show_scene).call_deferred();
	}
}

void EditorSceneTabs::_rome_document_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mouse = p_event;
	if (mouse.is_null() || !mouse->is_pressed()) {
		return;
	}
	const int index = rome_document_tabs->get_tab_idx_at_point(mouse->get_position());
	if (index < 0) {
		return;
	}
	if (mouse->get_button_index() == MouseButton::MIDDLE) {
		rome_document_tabs->accept_event();
		_rome_document_closed(index);
	} else if (mouse->get_button_index() == MouseButton::RIGHT) {
		PopupMenu *menu = scene_tabs_context_menu;
		if (index < scene_tabs->get_tab_count()) {
			_update_context_menu(index);
		} else {
			rome_menu_tab = index;
			menu = rome_document_menu;
			menu->reset_size();
		}
		menu->set_position(rome_document_tabs->get_screen_position() + mouse->get_position());
		menu->popup();
	}
}

void EditorSceneTabs::_rome_document_menu_pressed(int p_id) {
	const int script_index = rome_menu_tab - scene_tabs->get_tab_count();
	if (script_index < 0 || script_index >= rome_scripts.size()) {
		return;
	}
	ScriptEditor *scripts = ScriptEditor::get_singleton();
	if (p_id == 0) {
		scripts->request_close_resource(rome_scripts[script_index]);
	} else {
		scripts->rome_close_documents(p_id == 1 ? Ref<Resource>(rome_scripts[script_index]) : Ref<Resource>());
	}
}

Array EditorSceneTabs::_rome_get_documents() {
	_rome_update_documents();
	Array result;
	if (!rome_document_tabs) {
		return result;
	}
	const int scenes = scene_tabs->get_tab_count();
	for (int i = 0; i < rome_document_tabs->get_tab_count(); i++) {
		Dictionary item;
		const bool scene = i < scenes;
		item["kind"] = scene ? "scene" : "script";
		item["label"] = rome_document_tabs->get_tab_title(i);
		item["path"] = scene ? EditorNode::get_editor_data().get_scene_path(i) : rome_scripts[i - scenes]->get_path();
		item["type"] = scene ? String() : String(rome_scripts[i - scenes]->get_meta("rome_script_type", String()));
		item["has_icon"] = rome_document_tabs->get_tab_icon(i).is_valid();
		item["active"] = rome_document_tabs->get_current_tab() == i;
		result.push_back(item);
	}
	return result;
}
