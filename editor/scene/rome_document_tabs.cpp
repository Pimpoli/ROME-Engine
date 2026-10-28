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
	for (int i = 0; i < scenes; i++) {
		rome_document_tabs->set_tab_title(i, scene_tabs->get_tab_title(i));
		rome_document_tabs->set_tab_icon(i, scene_tabs->get_tab_icon(i));
		rome_document_tabs->set_tab_tooltip(i, EditorNode::get_editor_data().get_scene_path(i));
	}
	for (int i = 0; i < rome_scripts.size(); i++) {
		const Ref<Script> script = rome_scripts[i];
		String name = script->get_meta("rome_script_name", script->get_name());
		if (name.is_empty()) {
			name = script->get_path().get_file().get_basename();
		}
		const String type = script->get_meta("rome_script_type", script->get_path().get_extension());
		String label = name + (type.is_empty() ? String() : "." + type);
		ScriptEditorBase *editor = scripts->get_script_container()->get_resource_editor(script);
		if (editor && editor->is_unsaved()) {
			label += " *";
		}
		rome_document_tabs->set_tab_title(scenes + i, label);
		rome_document_tabs->set_tab_icon(scenes + i, get_editor_theme_icon("Script"));
		rome_document_tabs->set_tab_tooltip(scenes + i, script->get_path());
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
	if (mouse.is_valid() && mouse->is_pressed() && mouse->get_button_index() == MouseButton::RIGHT) {
		const int index = rome_document_tabs->get_tab_idx_at_point(mouse->get_position());
		if (index >= 0 && index < scene_tabs->get_tab_count()) {
			_update_context_menu(index);
			scene_tabs_context_menu->set_position(rome_document_tabs->get_screen_position() + mouse->get_position());
			scene_tabs_context_menu->popup();
		}
	}
}

Array EditorSceneTabs::_rome_get_documents() {
	_rome_update_documents();
	Array result;
	if (!rome_document_tabs) {
		return result;
	}
	for (int i = 0; i < rome_document_tabs->get_tab_count(); i++) {
		Dictionary item;
		item["kind"] = i < scene_tabs->get_tab_count() ? "scene" : "script";
		item["label"] = rome_document_tabs->get_tab_title(i);
		item["path"] = rome_document_tabs->get_tab_tooltip(i);
		item["active"] = rome_document_tabs->get_current_tab() == i;
		result.push_back(item);
	}
	return result;
}
