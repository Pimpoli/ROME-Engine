#pragma once

#include "core/config/project_settings.h"

class RomeProjectTemplate {
	static String update_message;
	static bool update_failed;

public:
	static String get_root();
	static String get_identity();
	static bool is_available();
	static Error install(const String &p_path, const String &p_template);
	static void configure(ProjectSettings::CustomMap &r_settings);
	static Error update_project(const String &p_path);
	static String take_update_message(bool *r_failed = nullptr);
};
