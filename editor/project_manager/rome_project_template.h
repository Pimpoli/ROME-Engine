#pragma once

#include "core/config/project_settings.h"

class RomeProjectTemplate {
public:
	static String get_root();
	static bool is_available();
	static Error install(const String &p_path, const String &p_template);
	static void configure(ProjectSettings::CustomMap &r_settings);
};
