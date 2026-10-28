@tool
extends "res://addons/GodotLuauUpdater/plugin_ia.gd"

# Plugin de GodotLuau, parte 5 de 5 (la que carga Godot): ciclo de vida,
# el panel de ajustes, el logo del juego, importar .rbxl, autocompletado
# propio y estadisticas de uso. Las demas partes estan en plugin_*.gd.

# ── Lifecycle ─────────────────────────────────────────────────────────────────

func _enter_tree() -> void:
	_detect_editor_lang()
	_compute_theme_colors()
	_load_font_pref()
	_sync_plugin_cfg()
	_cleanup_old_dlls()
	_gia_migrar_modelos()   # recupera modelos ya bajados y suelta duplicados
	_register_settings()
	_ensure_physics_limits()
	_ensure_render_limits()
	_apply_autocomplete_speed()
	_build_settings_panel()
	_check_watermark()
	_check_for_update()
	_connect_script_lifecycle()
	_purge_old_trash()
	_build_mp_toolbar()
	_setup_luau_highlighting()
	_inspector_plugin = ScriptEnabledInspector.new()
	add_inspector_plugin(_inspector_plugin)
	# Diferido: toca ProjectSettings y el filesystem, mejor con el editor ya en pie.
	call_deferred("_asegurar_logo_defecto")

func _exit_tree() -> void:
	if _inspector_plugin != null:
		remove_inspector_plugin(_inspector_plugin)
		_inspector_plugin = null
	_teardown_luau_highlighting()
	_disconnect_script_lifecycle()
	for n in [_http_version, _http_download, _http_autocomplete, _http_hash, _http_aimodel, _outdated_timer]:
		if n and is_instance_valid(n): n.queue_free()
	if _settings_panel and is_instance_valid(_settings_panel):
		remove_control_from_bottom_panel(_settings_panel)
		_settings_panel.queue_free()
	# Cerrar ventanas cliente y quitar nuestros run-args si el editor se cierra o
	# se desactiva el plugin (si quedaran, TODOS los Play futuros serían host).
	_kill_mp_children()
	var base := _strip_mp_args(str(ProjectSettings.get_setting("editor/run/main_run_args", "")))
	if str(ProjectSettings.get_setting("editor/run/main_run_args", "")) != base:
		ProjectSettings.set_setting("editor/run/main_run_args", base)
		ProjectSettings.save()
	_remove_res_file("GodotLuau/.gl_mp_session")
	_remove_res_file("GodotLuau/.gl_view_cmd")
	if _view_btn and is_instance_valid(_view_btn):
		_view_btn.queue_free()
		_view_btn = null
	_remove_mp_toolbar()

# ── UI helpers ────────────────────────────────────────────────────────────────

# Zona tipo tarjeta: fondo redondeado + borde de acento a la izquierda.
# Devuelve [tarjeta, contenedor_interno] — añade los controles al segundo.
func _make_zone(title: String, accent: Color) -> Array:
	var card := PanelContainer.new()
	var sb := StyleBoxFlat.new()
	sb.bg_color = _col_card
	sb.set_corner_radius_all(8)
	sb.set_content_margin_all(12)
	sb.border_width_left = 3
	sb.border_color = _accent(accent)
	card.add_theme_stylebox_override("panel", sb)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 6)
	card.add_child(vb)
	var lbl := Label.new()
	lbl.text = title
	lbl.add_theme_font_size_override("font_size", _fs(14))
	lbl.add_theme_color_override("font_color", _accent(accent))
	vb.add_child(lbl)
	return [card, vb]

# Fade-in escalonado de las zonas al construir el panel
func _animate_zones(zones: Array) -> void:
	for i in zones.size():
		var z : Control = zones[i]
		z.modulate.a = 0.0
		var tw := z.create_tween()
		tw.tween_interval(0.05 * i)
		tw.tween_property(z, "modulate:a", 1.0, 0.2)

func _make_row(title: String, desc: String, key: String, default_val: bool,
			   on_change: Callable = Callable()) -> Control:
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 3)
	var hb := HBoxContainer.new()
	hb.add_theme_constant_override("separation", 8)
	var tog := CheckButton.new()
	tog.button_pressed = ProjectSettings.get_setting(key, default_val)
	tog.toggled.connect(func(pressed: bool) -> void:
		ProjectSettings.set_setting(key, pressed)
		ProjectSettings.save()
		if on_change.is_valid(): on_change.call(pressed)
	)
	hb.add_child(tog)
	var lbl := Label.new()
	lbl.text = title
	lbl.add_theme_font_size_override("font_size", _fs(13))
	lbl.add_theme_color_override("font_color", _col_text)
	hb.add_child(lbl)
	vb.add_child(hb)
	if not desc.is_empty():
		var dlbl := Label.new()
		dlbl.text = desc
		dlbl.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		dlbl.add_theme_color_override("font_color", _col_dim)
		dlbl.add_theme_font_size_override("font_size", _fs(11))
		vb.add_child(dlbl)
	return vb

# ── Settings panel ────────────────────────────────────────────────────────────

func _build_settings_panel() -> void:
	var scroll := ScrollContainer.new()
	scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.size_flags_vertical   = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	scroll.custom_minimum_size    = Vector2(0, 300)
	_settings_panel = scroll
	_build_panel_contents()
	add_control_to_bottom_panel(scroll, "GodotLuau Config")
	if _first_build:
		make_bottom_panel_item_visible(scroll)
		_first_build = false

func _build_panel_contents() -> void:
	var outer := MarginContainer.new()
	for s in ["margin_left","margin_right","margin_top","margin_bottom"]:
		outer.add_theme_constant_override(s, 14)
	# EXPAND_FILL + ancho minimo: sin esto el panel se encogia al contenido, asi
	# que quedaba estrecho al estar al dia (fila corta) y solo ancho cuando habia
	# actualizacion pendiente (fila larga). Ahora ocupa siempre el ancho del panel.
	outer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	outer.custom_minimum_size = Vector2(760, 0)
	_settings_panel.add_child(outer)

	var vbox := VBoxContainer.new()
	vbox.add_theme_constant_override("separation", 10)
	vbox.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	outer.add_child(vbox)

	# ── Header: solo título + idioma ─────────────────────────────────────
	var hdr_row := HBoxContainer.new()
	hdr_row.add_theme_constant_override("separation", 8)
	vbox.add_child(hdr_row)

	var hdr := Label.new()
	hdr.text = _t("panel_title")
	hdr.add_theme_color_override("font_color", _accent(Color(0.4, 0.8, 1.0)))
	hdr.add_theme_font_size_override("font_size", _fs(17))
	hdr.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	hdr_row.add_child(hdr)

	var lang_lbl := Label.new()
	lang_lbl.text = "🌐"
	hdr_row.add_child(lang_lbl)
	var lang_opt := OptionButton.new()
	lang_opt.add_item("English", 0)
	lang_opt.add_item("Español", 1)
	lang_opt.add_item("Português (BR)", 2)
	match _lang:
		"es": lang_opt.select(1)
		"pt": lang_opt.select(2)
		_:    lang_opt.select(0)
	lang_opt.item_selected.connect(func(idx: int) -> void:
		match idx:
			0: _lang = "en"
			1: _lang = "es"
			2: _lang = "pt"
		# Persistir la elección: la extensión recordará el idioma siempre
		EditorInterface.get_editor_settings().set_setting("godot_luau/lang", _lang)
		_rebuild_panel()
	)
	hdr_row.add_child(lang_opt)

	# ── Barra de notificaciones (mensajes transitorios) ─────────────────
	_notif_bar = PanelContainer.new()
	_notif_bar.visible = false
	var notif_sb := StyleBoxFlat.new()
	notif_sb.bg_color = Color(0.16, 0.18, 0.24)
	notif_sb.set_corner_radius_all(6)
	notif_sb.set_content_margin_all(8)
	_notif_bar.add_theme_stylebox_override("panel", notif_sb)
	var notif_hb := HBoxContainer.new()
	notif_hb.add_theme_constant_override("separation", 8)
	_notif_bar.add_child(notif_hb)
	_notif_label = Label.new()
	_notif_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_notif_label.add_theme_font_size_override("font_size", _fs(11))
	notif_hb.add_child(_notif_label)
	var notif_x := Button.new()
	notif_x.flat = true
	notif_x.text = "✕"
	notif_x.pressed.connect(func(): _notif_bar.visible = false)
	notif_hb.add_child(notif_x)
	vbox.add_child(_notif_bar)

	var zones : Array = []

	# ══ ZONA 1 · 🔄 ACTUALIZACIONES ══════════════════════════════════════
	var z_upd := _make_zone(_t("sec_updates"), Color(0.3, 0.9, 0.8))
	vbox.add_child(z_upd[0]); zones.append(z_upd[0])
	var upd_vb : VBoxContainer = z_upd[1]

	var ver_row := HBoxContainer.new()
	ver_row.add_theme_constant_override("separation", 6)
	upd_vb.add_child(ver_row)

	_ver_label = Label.new()
	_ver_label.text = _get_local_version()
	_ver_label.add_theme_color_override("font_color", _col_text)
	_ver_label.add_theme_font_size_override("font_size", _fs(13))
	_ver_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	ver_row.add_child(_ver_label)

	# Lupa multiaccion: buscar actualizaciones cuando esta al dia, o Actualizar /
	# Reintentar segun el estado. Ya cumple lo de "buscar de nuevo", asi que NO se
	# anade otra lupa aparte (eso duplicaba el boton).
	_ver_btn = Button.new()
	_ver_btn.text = _t("btn_check_ver")
	_ver_btn.flat = true
	_ver_btn.tooltip_text = "Check for updates"
	_ver_btn.pressed.connect(_on_action_pressed)
	_bar_action = ""
	ver_row.add_child(_ver_btn)

	_reinstall_btn = Button.new()
	_reinstall_btn.text = _t("btn_reinstall")
	_reinstall_btn.flat = true
	_reinstall_btn.add_theme_color_override("font_color", Color(0.6, 0.75, 1.0))
	_reinstall_btn.tooltip_text = "Re-download and reinstall the current version"
	_reinstall_btn.pressed.connect(_start_reinstall)
	ver_row.add_child(_reinstall_btn)

	# Historial completo: instalar CUALQUIER version publicada, no solo el
	# respaldo local de la anterior.
	_versions_btn = Button.new()
	_versions_btn.text = _t("btn_versions")
	_versions_btn.flat = true
	_versions_btn.add_theme_color_override("font_color", Color(0.75, 0.85, 1.0))
	_versions_btn.tooltip_text = "Install any published version"
	_versions_btn.pressed.connect(_on_versions_pressed)
	ver_row.add_child(_versions_btn)

	_versions_menu = PopupMenu.new()
	_versions_menu.id_pressed.connect(_on_version_chosen)
	_versions_btn.add_child(_versions_menu)

	# Volver a la version anterior (visible solo si hay un respaldo guardado)
	_rollback_btn = Button.new()
	_rollback_btn.flat = true
	_rollback_btn.add_theme_color_override("font_color", Color(1.0, 0.75, 0.4))
	_rollback_btn.tooltip_text = "Restore the previous version saved on your PC"
	_rollback_btn.pressed.connect(_on_rollback_pressed)
	ver_row.add_child(_rollback_btn)
	_refresh_rollback_btn()

	var gh_row := HBoxContainer.new()
	gh_row.add_theme_constant_override("separation", 6)
	upd_vb.add_child(gh_row)
	var gh_lbl := Label.new()
	gh_lbl.text = _t("github_label")
	gh_lbl.add_theme_font_size_override("font_size", _fs(11))
	gh_lbl.add_theme_color_override("font_color", _col_faint)
	gh_row.add_child(gh_lbl)
	var gh_link := Label.new()
	gh_link.text = "github.com/Pimpoli/GodotLuau"
	gh_link.add_theme_font_size_override("font_size", _fs(11))
	gh_link.add_theme_color_override("font_color", _accent(Color(0.35, 0.65, 1.0)))
	gh_link.mouse_filter = Control.MOUSE_FILTER_STOP
	gh_link.mouse_default_cursor_shape = Control.CURSOR_POINTING_HAND
	gh_link.gui_input.connect(func(e: InputEvent) -> void:
		if e is InputEventMouseButton and e.button_index == MOUSE_BUTTON_LEFT and e.pressed:
			OS.shell_open(GITHUB_URL)
	)
	gh_row.add_child(gh_link)

	# ══ ZONA 2 · 🤖 IA Y AUTOCOMPLETADO ══════════════════════════════════
	var z_ac := _make_zone(_t("sec_autocomplete"), Color(0.4, 0.8, 1.0))
	vbox.add_child(z_ac[0]); zones.append(z_ac[0])
	var ac_vb : VBoxContainer = z_ac[1]

	# ── 1º: Autocompletado Instantáneo (arriba) ──────────────────────
	ac_vb.add_child(_make_row(_t("speed_title"), _t("speed_desc"),
		"godot_luau/instant_autocomplete", true,
		func(on: bool) -> void:
			if on:
				ProjectSettings.set_setting("godot_luau/ai_autocomplete_enabled", false)
				ProjectSettings.save()
				_apply_autocomplete_speed()
				_rebuild_panel()
	))

	# ── 2º: Autocompletado con IA (abajo, junto al modelo) ───────────
	# IA e Instantáneo son mutuamente excluyentes; la IA requiere
	# que el modelo seleccionado esté disponible (o descargado).
	ac_vb.add_child(_make_row(_t("ai_title"), _t("ai_desc"),
		"godot_luau/ai_autocomplete_enabled", false,
		func(on: bool) -> void:
			if on:
				if not _ai_model_available(_selected_model_id()):
					ProjectSettings.set_setting("godot_luau/ai_autocomplete_enabled", false)
					ProjectSettings.save()
					_set_notif(_t("aim_need_download"), Color(1.0, 0.75, 0.3))
					_rebuild_panel()
					return
				ProjectSettings.set_setting("godot_luau/instant_autocomplete", false)
				ProjectSettings.save()
				_rebuild_panel()
	))

	# Selector de modelo (como el selector de idioma)
	var sel_row := HBoxContainer.new()
	sel_row.add_theme_constant_override("separation", 8)
	ac_vb.add_child(sel_row)

	var sel_lbl := Label.new()
	sel_lbl.text = _t("aim_select_label")
	sel_lbl.add_theme_font_size_override("font_size", _fs(12))
	sel_lbl.add_theme_color_override("font_color", _col_text)
	sel_row.add_child(sel_lbl)

	var model_opt := OptionButton.new()
	model_opt.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	var cur_id := _selected_model_id()
	for mi in AI_MODELS.size():
		var m : Dictionary = AI_MODELS[mi]
		model_opt.add_item("%s  —  %s %s" % [m["name"], m["params"], _t("aim_params_suffix")], mi)
		if m["id"] == cur_id:
			model_opt.select(mi)
	model_opt.item_selected.connect(func(idx: int) -> void:
		var m : Dictionary = AI_MODELS[idx]
		ProjectSettings.set_setting("godot_luau/ai_model_selected", m["id"])
		# Si la IA estaba activa pero el modelo nuevo no está disponible, apagarla
		if bool(ProjectSettings.get_setting("godot_luau/ai_autocomplete_enabled", false)) \
				and not _ai_model_available(str(m["id"])):
			ProjectSettings.set_setting("godot_luau/ai_autocomplete_enabled", false)
			_set_notif(_t("aim_need_download"), Color(1.0, 0.75, 0.3))
		ProjectSettings.save()
		_rebuild_panel()
	)
	sel_row.add_child(model_opt)

	# Botón "Descargar ⏬" solo si el modelo elegido es descargable y falta
	var sel_entry := _model_entry(cur_id)
	if sel_entry.has("url") and not _ai_model_available(cur_id):
		var dl_btn := Button.new()
		dl_btn.text = _t("aim_btn_dl")
		dl_btn.add_theme_color_override("font_color", _accent(Color(0.4, 0.9, 1.0)))
		dl_btn.pressed.connect(_download_selected_model)
		sel_row.add_child(dl_btn)

	_aim_status = Label.new()
	_aim_status.add_theme_font_size_override("font_size", _fs(11))
	ac_vb.add_child(_aim_status)
	_refresh_aim_status()

	# ── Modelo de IA personalizado (misma distribución que el
	#    autocompletado personalizado: URL+Descargar, debajo Importar/Limpiar) ──
	var aim_hdr := Label.new()
	aim_hdr.text = _t("aim_header")
	aim_hdr.add_theme_font_size_override("font_size", _fs(12))
	aim_hdr.add_theme_color_override("font_color", _accent(Color(0.95, 0.8, 0.4)))
	ac_vb.add_child(aim_hdr)

	var aim_note := Label.new()
	aim_note.text = _t("aim_note")
	aim_note.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	aim_note.add_theme_font_size_override("font_size", _fs(11))
	aim_note.add_theme_color_override("font_color", _col_dim)
	ac_vb.add_child(aim_note)

	var aim_row := HBoxContainer.new()
	aim_row.add_theme_constant_override("separation", 6)
	ac_vb.add_child(aim_row)
	_aim_url_field = LineEdit.new()
	_aim_url_field.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_aim_url_field.placeholder_text = _t("cac_url_hint")
	_aim_url_field.text = str(ProjectSettings.get_setting("godot_luau/ai_model_url", ""))
	_aim_url_field.text_changed.connect(func(txt: String) -> void:
		ProjectSettings.set_setting("godot_luau/ai_model_url", txt)
		ProjectSettings.save()
	)
	aim_row.add_child(_aim_url_field)
	var aim_dl := Button.new()
	aim_dl.text = _t("cac_btn_download")
	aim_dl.pressed.connect(_download_ai_model)
	aim_row.add_child(aim_dl)

	var aim_btns := HBoxContainer.new()
	aim_btns.add_theme_constant_override("separation", 8)
	ac_vb.add_child(aim_btns)
	var aim_imp := Button.new()
	aim_imp.text = _t("cac_btn_import")
	aim_imp.pressed.connect(_import_ai_model_file)
	aim_btns.add_child(aim_imp)
	var aim_clr := Button.new()
	aim_clr.text = _t("cac_btn_clear")
	aim_clr.add_theme_color_override("font_color", Color(1.0, 0.6, 0.6))
	aim_clr.pressed.connect(_clear_ai_model)
	aim_btns.add_child(aim_clr)

	var cac_hdr := Label.new()
	cac_hdr.text = _t("cac_header")
	cac_hdr.add_theme_font_size_override("font_size", _fs(12))
	cac_hdr.add_theme_color_override("font_color", _accent(Color(0.6, 0.9, 0.6)))
	ac_vb.add_child(cac_hdr)

	ac_vb.add_child(_make_row(_t("cac_toggle_title"), _t("cac_toggle_desc"),
		"godot_luau/custom_autocomplete_enabled", false))

	var url_lbl := Label.new()
	url_lbl.text = _t("cac_url_label")
	url_lbl.add_theme_font_size_override("font_size", _fs(11))
	url_lbl.add_theme_color_override("font_color", _col_dim)
	ac_vb.add_child(url_lbl)

	var url_row := HBoxContainer.new()
	url_row.add_theme_constant_override("separation", 6)
	ac_vb.add_child(url_row)
	_cac_url_field = LineEdit.new()
	_cac_url_field.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_cac_url_field.placeholder_text = _t("cac_url_hint")
	_cac_url_field.text = str(ProjectSettings.get_setting("godot_luau/custom_autocomplete_url", ""))
	_cac_url_field.text_changed.connect(func(txt: String) -> void:
		ProjectSettings.set_setting("godot_luau/custom_autocomplete_url", txt)
		ProjectSettings.save()
	)
	url_row.add_child(_cac_url_field)
	var btn_dl := Button.new()
	btn_dl.text = _t("cac_btn_download")
	btn_dl.pressed.connect(_download_custom_ac)
	url_row.add_child(btn_dl)

	var cac_btns := HBoxContainer.new()
	cac_btns.add_theme_constant_override("separation", 8)
	ac_vb.add_child(cac_btns)
	var btn_import := Button.new()
	btn_import.text = _t("cac_btn_import")
	btn_import.pressed.connect(_import_custom_ac_file)
	cac_btns.add_child(btn_import)
	var btn_clr_ac := Button.new()
	btn_clr_ac.text = _t("cac_btn_clear")
	btn_clr_ac.add_theme_color_override("font_color", Color(1.0, 0.6, 0.6))
	btn_clr_ac.pressed.connect(_clear_custom_ac)
	cac_btns.add_child(btn_clr_ac)

	_cac_status = Label.new()
	_cac_status.add_theme_font_size_override("font_size", _fs(11))
	ac_vb.add_child(_cac_status)
	_refresh_cac_status()

	# ══ ZONA 2b · 🧠 GODOTLUAU IA ════════════════════════════════════════
	# Capa 0: el analizador de compatibilidad. No usa ningun modelo — parsea
	# con el Luau real del motor y cruza cada simbolo contra lo que el puente
	# implementa de verdad (tabla generada del propio codigo). Por eso es
	# instantaneo, funciona en cualquier PC y no se equivoca.
	var z_gia := _make_zone(_t("gia_title"), Color(0.75, 0.55, 1.0))
	vbox.add_child(z_gia[0]); zones.append(z_gia[0])
	var gia_vb : VBoxContainer = z_gia[1]

	var gia_desc := Label.new()
	gia_desc.text = _t("gia_desc")
	gia_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	gia_desc.add_theme_font_size_override("font_size", _fs(11))
	gia_desc.add_theme_color_override("font_color", _col_dim)
	gia_vb.add_child(gia_desc)

	var gia_row := HBoxContainer.new()
	gia_row.add_theme_constant_override("separation", 8)
	gia_vb.add_child(gia_row)

	var gia_chat := Button.new()
	gia_chat.text = "💬  " + _t("gia_open")
	gia_chat.pressed.connect(_gia_abrir_chat)
	gia_row.add_child(gia_chat)

	var gia_proj := Button.new()
	gia_proj.text = "📂  " + _t("gia_scan_project")
	gia_proj.pressed.connect(func() -> void: _gia_analizar("proyecto"))
	gia_row.add_child(gia_proj)

	var gia_scene := Button.new()
	gia_scene.text = "🎬  " + _t("gia_scan_scene")
	gia_scene.pressed.connect(func() -> void: _gia_analizar("escena"))
	gia_row.add_child(gia_scene)

	_gia_status = Label.new()
	_gia_status.text = _t("gia_ready")
	_gia_status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_gia_status.add_theme_font_size_override("font_size", _fs(11))
	gia_vb.add_child(_gia_status)

	# ── Motor y modelos: descargar / actualizar ──────────────────────
	# Van con SU boton, aparte de la version de GodotLuau: la IA se puede
	# mejorar sin publicar una version nueva del motor, y quien no la quiera
	# no descarga nada. El chat es lo unico que necesita esto; las revisiones
	# de arriba funcionan siempre.
	var sep := HSeparator.new()
	gia_vb.add_child(sep)

	var dl_tit := Label.new()
	dl_tit.text = _t("gia_dl_title")
	dl_tit.add_theme_font_size_override("font_size", _fs(12))
	dl_tit.add_theme_color_override("font_color", _accent(Color(0.75, 0.55, 1.0)))
	gia_vb.add_child(dl_tit)

	# Fila del MOTOR (la extension del chat)
	var mrow := HBoxContainer.new()
	mrow.add_theme_constant_override("separation", 8)
	gia_vb.add_child(mrow)
	var mlbl := Label.new()
	mlbl.text = "%s  ·  ≈5,5 MB" % _t("gia_engine")
	mlbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	mlbl.add_theme_font_size_override("font_size", _fs(11))
	mrow.add_child(mlbl)
	var mest := Label.new()
	mest.add_theme_font_size_override("font_size", _fs(11))
	if _gia_motor_disponible():
		mest.text = "✅ " + _t("gia_installed")
		mest.add_theme_color_override("font_color", Color(0.5, 0.9, 0.5))
	else:
		mest.text = _t("gia_not_installed")
		mest.add_theme_color_override("font_color", _col_dim)
	mrow.add_child(mest)
	var mbtn := Button.new()
	mbtn.text = ("↻ " + _t("gia_update")) if _gia_motor_disponible() else ("⏬ " + _t("gia_download"))
	mbtn.pressed.connect(func() -> void: _gia_descargar("motor"))
	mrow.add_child(mbtn)

	# Una fila por modelo
	for m in GIA_MODELOS:
		var row := HBoxContainer.new()
		row.add_theme_constant_override("separation", 8)
		gia_vb.add_child(row)
		var lbl := Label.new()
		lbl.text = "%s  ·  %s" % [str(m["nombre"]), str(m["tam"])]
		lbl.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		lbl.add_theme_font_size_override("font_size", _fs(11))
		row.add_child(lbl)
		var est := Label.new()
		est.add_theme_font_size_override("font_size", _fs(11))
		var puesto : bool = _gia_modelo_disponible(str(m["id"]))
		if puesto:
			est.text = "✅ " + _t("gia_installed")
			est.add_theme_color_override("font_color", Color(0.5, 0.9, 0.5))
		else:
			est.text = _t("gia_not_installed")
			est.add_theme_color_override("font_color", _col_dim)
		row.add_child(est)
		var btn := Button.new()
		btn.text = ("↻ " + _t("gia_update")) if puesto else ("⏬ " + _t("gia_download"))
		var mid : String = str(m["id"])
		btn.pressed.connect(func() -> void: _gia_descargar(mid))
		row.add_child(btn)

	# Barra de progreso (oculta hasta que hay descarga)
	_gia_dl_bar = ProgressBar.new()
	_gia_dl_bar.show_percentage = false
	_gia_dl_bar.custom_minimum_size = Vector2(0, 10)
	_gia_dl_bar.visible = false
	gia_vb.add_child(_gia_dl_bar)

	var prow := HBoxContainer.new()
	prow.add_theme_constant_override("separation", 8)
	gia_vb.add_child(prow)
	_gia_dl_txt = Label.new()
	_gia_dl_txt.add_theme_font_size_override("font_size", _fs(10))
	_gia_dl_txt.add_theme_color_override("font_color", _col_dim)
	_gia_dl_txt.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_gia_dl_txt.visible = false
	prow.add_child(_gia_dl_txt)
	_gia_dl_cancel = Button.new()
	_gia_dl_cancel.text = _t("gia_cancel")
	_gia_dl_cancel.visible = false
	_gia_dl_cancel.pressed.connect(_gia_dl_cancelar)
	prow.add_child(_gia_dl_cancel)

	# Si al reconstruir el panel habia una descarga en curso, se recuperan.
	if _gia_dl_activa():
		_gia_dl_ui_visible(true)

	var nota := Label.new()
	nota.text = _t("gia_note")
	nota.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	nota.add_theme_font_size_override("font_size", _fs(10))
	nota.add_theme_color_override("font_color", _col_dim)
	gia_vb.add_child(nota)

	# ══ ZONA 3 · 📊 DATOS DE USO ═════════════════════════════════════════
	var z_data := _make_zone(_t("sec_data"), Color(0.5, 0.9, 0.5))
	vbox.add_child(z_data[0]); zones.append(z_data[0])
	var data_vb : VBoxContainer = z_data[1]

	data_vb.add_child(_make_row(_t("share_title"), _t("share_desc"),
		"godot_luau/share_data_enabled", false))

	var stats_sub := Label.new()
	stats_sub.text = _t("stats_header")
	stats_sub.add_theme_font_size_override("font_size", _fs(11))
	stats_sub.add_theme_color_override("font_color", _col_dim)
	data_vb.add_child(stats_sub)

	_stats_label = Label.new()
	_stats_label.add_theme_color_override("font_color", _col_dim)
	_stats_label.add_theme_font_size_override("font_size", _fs(11))
	data_vb.add_child(_stats_label)
	_refresh_stats()

	var stat_btns := HBoxContainer.new()
	stat_btns.add_theme_constant_override("separation", 8)
	data_vb.add_child(stat_btns)
	var btn_ref := Button.new()
	btn_ref.text = _t("btn_refresh")
	btn_ref.pressed.connect(_refresh_stats)
	stat_btns.add_child(btn_ref)
	var btn_open := Button.new()
	btn_open.text = _t("btn_open")
	btn_open.pressed.connect(func(): OS.shell_open(OS.get_user_data_dir()))
	stat_btns.add_child(btn_open)
	var btn_del := Button.new()
	btn_del.text = _t("btn_clear")
	btn_del.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))
	btn_del.pressed.connect(_confirm_clear_data)
	stat_btns.add_child(btn_del)

	# ══ ZONA 4 · 🔧 DEBUG ════════════════════════════════════════════════
	var z_dbg := _make_zone(_t("sec_debug"), Color(1.0, 0.7, 0.3))
	vbox.add_child(z_dbg[0]); zones.append(z_dbg[0])
	var dbg_vb : VBoxContainer = z_dbg[1]

	dbg_vb.add_child(_make_row(_t("debug_title"), _t("debug_desc"),
		"godot_luau/script_output", true))

	dbg_vb.add_child(_make_row(_t("dbg_title"), _t("dbg_desc"),
		"godot_luau/debug_mode", false))

	dbg_vb.add_child(_make_row(_t("anim_title"), _t("anim_desc"),
		"godot_luau/debug_test_animation", false))

	dbg_vb.add_child(_make_row(_t("notif_outdated_title"), _t("notif_outdated_desc"),
		"godot_luau/notify_outdated_version", true))

	# ══ ZONA 5 · 🎨 APARIENCIA ═══════════════════════════════════════════
	var z_app := _make_zone(_t("sec_appearance"), Color(0.85, 0.6, 1.0))
	vbox.add_child(z_app[0]); zones.append(z_app[0])
	var app_vb : VBoxContainer = z_app[1]

	var size_lbl := Label.new()
	size_lbl.text = _t("font_size_label")
	size_lbl.add_theme_font_size_override("font_size", _fs(12))
	size_lbl.add_theme_color_override("font_color", _col_text)
	app_vb.add_child(size_lbl)

	var size_row := HBoxContainer.new()
	size_row.add_theme_constant_override("separation", 10)
	app_vb.add_child(size_row)

	var slider := HSlider.new()
	slider.min_value = 0
	slider.max_value = 100
	slider.step = 1
	slider.value = _font_pct
	slider.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	slider.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	slider.custom_minimum_size = Vector2(140, 0)
	size_row.add_child(slider)

	var pct_lbl := Label.new()
	pct_lbl.text = "%d%%" % _font_pct
	pct_lbl.custom_minimum_size = Vector2(46, 0)
	pct_lbl.add_theme_font_size_override("font_size", _fs(12))
	pct_lbl.add_theme_color_override("font_color", _accent(Color(0.85, 0.6, 1.0)))
	size_row.add_child(pct_lbl)

	slider.value_changed.connect(func(v: float) -> void:
		pct_lbl.text = "%d%%" % int(v)
	)
	slider.drag_ended.connect(func(changed: bool) -> void:
		if not changed: return
		EditorInterface.get_editor_settings().set_setting("godot_luau/panel_font_scale_pct", int(slider.value))
		_apply_font_pct(int(slider.value))
		_rebuild_panel()
	)

	# ══ ZONA 6 · 🖼  MARCA ═══════════════════════════════════════════════
	# El logo del juego. Vive SIEMPRE en res://GodotLuau/icons/MenuLogo.svg:
	# el menu de Luau lo pide por esa ruta, asi que cambiarlo es sobreescribir
	# ese archivo. La imagen de fabrica (Rocket Meteor) se queda intacta al
	# lado para poder volver a ella.
	var z_brand := _make_zone(_t("sec_brand"), Color(1.0, 0.55, 0.75))
	vbox.add_child(z_brand[0]); zones.append(z_brand[0])
	var brand_vb : VBoxContainer = z_brand[1]

	var brand_row := HBoxContainer.new()
	brand_row.add_theme_constant_override("separation", 10)
	brand_vb.add_child(brand_row)

	var logo_prev := TextureRect.new()
	logo_prev.custom_minimum_size = Vector2(96, 54)
	logo_prev.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	logo_prev.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	brand_row.add_child(logo_prev)
	_refresh_logo_preview(logo_prev)

	var brand_col := VBoxContainer.new()
	brand_col.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	brand_col.add_theme_constant_override("separation", 4)
	brand_row.add_child(brand_col)

	var brand_lbl := Label.new()
	brand_lbl.text = _t("brand_logo")
	brand_lbl.add_theme_font_size_override("font_size", _fs(13))
	brand_lbl.add_theme_color_override("font_color", _col_text)
	brand_col.add_child(brand_lbl)

	var brand_desc := Label.new()
	brand_desc.text = _t("brand_desc")
	brand_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	brand_desc.add_theme_color_override("font_color", _col_dim)
	brand_desc.add_theme_font_size_override("font_size", _fs(11))
	brand_col.add_child(brand_desc)

	var brand_btns := HBoxContainer.new()
	brand_btns.add_theme_constant_override("separation", 8)
	brand_col.add_child(brand_btns)

	var btn_logo := Button.new()
	btn_logo.text = _t("brand_pick")
	btn_logo.pressed.connect(func() -> void: _pick_logo(logo_prev))
	brand_btns.add_child(btn_logo)

	var btn_logo_reset := Button.new()
	btn_logo_reset.text = _t("brand_reset")
	btn_logo_reset.pressed.connect(func() -> void: _reset_logo(logo_prev))
	brand_btns.add_child(btn_logo_reset)

	# ══ ZONA 7 · 📦 IMPORTAR .rbxl ═══════════════════════════════════════
	var z_imp := _make_zone(_t("sec_import"), Color(1.0, 0.8, 0.35))
	vbox.add_child(z_imp[0]); zones.append(z_imp[0])
	var imp_vb : VBoxContainer = z_imp[1]

	var imp_desc := Label.new()
	imp_desc.text = _t("import_desc")
	imp_desc.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	imp_desc.add_theme_color_override("font_color", _col_dim)
	imp_desc.add_theme_font_size_override("font_size", _fs(11))
	imp_vb.add_child(imp_desc)

	var btn_imp := Button.new()
	btn_imp.text = _t("import_pick")
	btn_imp.size_flags_horizontal = Control.SIZE_SHRINK_BEGIN
	btn_imp.pressed.connect(_pick_rbxl)
	imp_vb.add_child(btn_imp)

	# ── Footer ──────────────────────────────────────────────────────────
	var data_lbl := Label.new()
	data_lbl.text = _t("footer_data")
	data_lbl.add_theme_color_override("font_color", _col_faint)
	data_lbl.add_theme_font_size_override("font_size", _fs(10))
	vbox.add_child(data_lbl)

	_animate_zones(zones)

# ── Marca: el logo del juego ─────────────────────────────────────────────────
# Una sola ruta manda: res://GodotLuau/icons/MenuLogo.svg. La pide el menu de
# Luau, asi que cambiar el logo es escribir ese archivo — sin tocar ni un
# script. El de fabrica es el de Rocket Meteor (el estudio).
#
# SIEMPRE .svg, tambien cuando el usuario elige un PNG/JPG suyo: la imagen se
# reescala (tope 512) y se embebe en un SVG. Asi el archivo es liviano y la
# ruta no cambia nunca de extension.
#
# El arranque de Godot, la ventana y la barra de tareas llevan el logo de
# GodotLuau: el arranque siempre (sobre negro, como empieza la pantalla de
# carga de GodotLuau), y el icono mientras el logo sea el de fabrica; con un
# logo propio, el icono es ese logo.
const LOGO_PATH  := "res://GodotLuau/icons/MenuLogo.svg"
const LOGO_FABRICA := "res://GodotLuau/icons/RocketMeteor.svg"
const ICONO_FABRICA := "res://GodotLuau/icons/GodotLuauIcono.svg"
const SPLASH_FABRICA := "res://GodotLuau/icons/GodotLuauSplash.png"
const ICO_PATH    := "res://GodotLuau/icons/GodotLuau.ico"
# Restos de versiones anteriores, que se migran y se borran: el logo PNG de la
# 1.16, el BootSplash.png del arranque con imagen y el logo de fabrica de Multi
# Game Inc (el nombre anterior del estudio, ahora Rocket Meteor). Ese logo,
# reconocido por su huella md5 (SVG de la 1.17 en desarrollo; PNG de la 1.16 y
# de la 1.15.1), se cambia solo por el de Rocket Meteor; un logo del usuario no
# se toca nunca.
const LOGO_PNG_VIEJO := "res://GodotLuau/icons/MenuLogo.png"
const SPLASH_VIEJO := "res://GodotLuau/icons/BootSplash.png"
const MGI_VIEJOS := ["res://GodotLuau/icons/MultiGameInc.svg", "res://GodotLuau/icons/MultiGameInc.png"]
const MGI_MD5 := ["68d9a4d8ba1e63db536292f1ababd601", "e93ee4f17efe91b2472fc137a53459a6",
	"d068bc2725f6c368231c70841994f313"]
const LOGO_LADO := 512

# ── .ico nativo: el icono correcto DESDE EL PRIMER FRAME ────────────────────
# Al ejecutar salia el icono de Godot y solo despues cambiaba al nuestro. El
# motivo: "application/config/icon" lo rasteriza el motor DESPUES de arrancar,
# asi que la barra de tareas de Windows ya ha pintado el icono por defecto.
# Lo que Windows lee al instante es "application/config/windows_native_icon",
# que pide un .ico — y no teniamos ninguno.
#
# Un .ico moderno puede llevar PNG dentro tal cual (Vista en adelante), asi que
# basta con la cabecera: ICONDIR (6 bytes) + un ICONDIRENTRY (16) por tamaño +
# los PNG. Lleva varios tamaños, cada uno rasterizado del SVG a su medida:
# Windows coge el de 16/24/32 para la barra de titulo y la de tareas, y uno
# reducido desde 256 salia borroso.
const ICO_TAMANOS := [16, 24, 32, 48, 64, 128, 256]

# La imagen de `fuente` a `lado` px: un SVG se rasteriza a ese tamaño; lo
# demas se reescala.
func _imagen_a(fuente: String, lado: int) -> Image:
	var ruta := ProjectSettings.globalize_path(fuente)
	var img := Image.new()
	if fuente.get_extension().to_lower() == "svg":
		var datos := FileAccess.get_file_as_bytes(ruta)
		var base := Image.new()
		if base.load_svg_from_buffer(datos, 1.0) != OK or base.get_width() == 0: return null
		if img.load_svg_from_buffer(datos, float(lado) / float(max(base.get_width(), base.get_height()))) != OK: return null
	elif img.load(ruta) != OK:
		return null
	if img.get_width() != lado or img.get_height() != lado:
		img.resize(lado, lado, Image.INTERPOLATE_LANCZOS)
	return img

func _regenerar_ico(fuente: String) -> void:
	var pngs := []
	for lado in ICO_TAMANOS:
		var img := _imagen_a(fuente, lado)
		if img == null: return
		pngs.append(img.save_png_to_buffer())
	var n := pngs.size()
	var cab := PackedByteArray()
	cab.resize(6 + 16 * n)
	cab.encode_u16(0, 0)          # reservado
	cab.encode_u16(2, 1)          # tipo 1 = icono
	cab.encode_u16(4, n)          # imagenes
	var datos := 6 + 16 * n
	for i in n:
		var lado: int = ICO_TAMANOS[i]
		var e := 6 + 16 * i
		cab.encode_u8(e, lado if lado < 256 else 0)       # ancho (0 = 256)
		cab.encode_u8(e + 1, lado if lado < 256 else 0)   # alto
		cab.encode_u8(e + 2, 0)                           # colores de paleta
		cab.encode_u8(e + 3, 0)                           # reservado
		cab.encode_u16(e + 4, 1)                          # planos
		cab.encode_u16(e + 6, 32)                         # bits por pixel
		cab.encode_u32(e + 8, pngs[i].size())
		cab.encode_u32(e + 12, datos)                     # donde empieza su PNG
		datos += pngs[i].size()
	var f := FileAccess.open(ProjectSettings.globalize_path(ICO_PATH), FileAccess.WRITE)
	if f == null: return
	f.store_buffer(cab)
	for p in pngs:
		f.store_buffer(p)
	f.close()

# Envuelve una imagen en un SVG con el PNG optimizado embebido. Devuelve true
# si quedo escrito. No vectoriza (trazar un logo raster deformaria las letras):
# recorta la resolucion sobrante, que es donde estaba el peso.
func _img_a_svg(img: Image, destino: String) -> bool:
	var w := img.get_width()
	var h := img.get_height()
	if w > LOGO_LADO or h > LOGO_LADO:
		var esc := float(LOGO_LADO) / float(max(w, h))
		img.resize(int(w * esc), int(h * esc), Image.INTERPOLATE_LANCZOS)
		w = img.get_width(); h = img.get_height()
	var b64 := Marshalls.raw_to_base64(img.save_png_to_buffer())
	var svg := '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="%d" height="%d" viewBox="0 0 %d %d"><image width="%d" height="%d" xlink:href="data:image/png;base64,%s"/></svg>' \
		% [w, h, w, h, w, h, b64]
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(destino.get_base_dir()))
	var f := FileAccess.open(ProjectSettings.globalize_path(destino), FileAccess.WRITE)
	if f == null: return false
	f.store_string(svg)
	f.close()
	return true

# El logo vigente: el del usuario si lo hay; si no, el de fabrica.
func _logo_actual() -> String:
	if FileAccess.file_exists(LOGO_PATH): return LOGO_PATH
	if FileAccess.file_exists(LOGO_PNG_VIEJO): return LOGO_PNG_VIEJO   # proyecto sin migrar
	return LOGO_FABRICA

func _refresh_logo_preview(rect: TextureRect) -> void:
	if not is_instance_valid(rect): return
	var img := Image.new()
	if img.load(ProjectSettings.globalize_path(_logo_actual())) == OK:
		rect.texture = ImageTexture.create_from_image(img)

func _pick_logo(rect: TextureRect) -> void:
	var fd := EditorFileDialog.new()
	fd.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	fd.access = EditorFileDialog.ACCESS_FILESYSTEM
	fd.add_filter("*.png,*.jpg,*.jpeg,*.webp,*.svg", "Imagen")
	fd.title = _t("brand_pick")
	EditorInterface.get_base_control().add_child(fd)
	fd.file_selected.connect(func(ruta: String) -> void:
		_aplicar_logo(ruta, rect)
		fd.queue_free()
	)
	fd.canceled.connect(func() -> void: fd.queue_free())
	fd.popup_centered_ratio(0.6)

func _aplicar_logo(origen: String, rect: TextureRect) -> void:
	# Se copia DENTRO del proyecto: si se dejara la ruta de fuera, el juego
	# exportado no encontraria la imagen.
	var img := Image.new()
	if img.load(origen) != OK:
		_notify(_t("brand_fail"), Color(1.0, 0.5, 0.5))
		return
	if not _img_a_svg(img, LOGO_PATH):
		_notify(_t("brand_fail"), Color(1.0, 0.5, 0.5))
		return
	_borrar_logo_png_viejo()
	_aplicar_marca()
	_refresh_logo_preview(rect)
	EditorInterface.get_resource_filesystem().scan()
	_notify(_t("brand_ok"), Color(0.5, 0.9, 0.5))

func _reset_logo(rect: TextureRect) -> void:
	# La de fabrica ya es un SVG optimizado: copia directa de bytes.
	var datos := FileAccess.get_file_as_bytes(ProjectSettings.globalize_path(LOGO_FABRICA))
	if datos.is_empty():
		_notify(_t("brand_fail"), Color(1.0, 0.5, 0.5))
		return
	var f := FileAccess.open(ProjectSettings.globalize_path(LOGO_PATH), FileAccess.WRITE)
	if f == null:
		_notify(_t("brand_fail"), Color(1.0, 0.5, 0.5))
		return
	f.store_buffer(datos)
	f.close()
	_borrar_logo_png_viejo()
	_aplicar_marca()
	_refresh_logo_preview(rect)
	EditorInterface.get_resource_filesystem().scan()
	_notify(_t("brand_reset_ok"), Color(0.5, 0.9, 0.5))

func _borrar_logo_png_viejo() -> void:
	for p in [LOGO_PNG_VIEJO, LOGO_PNG_VIEJO + ".import"]:
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(p))

func _es_logo_mgi(ruta: String) -> bool:
	return FileAccess.file_exists(ruta) and FileAccess.get_md5(ruta) in MGI_MD5

func _logo_es_fabrica() -> bool:
	return FileAccess.file_exists(LOGO_PATH) and FileAccess.file_exists(LOGO_FABRICA) \
		and FileAccess.get_md5(LOGO_PATH) == FileAccess.get_md5(LOGO_FABRICA)

func _copiar_logo_fabrica() -> bool:
	var datos := FileAccess.get_file_as_bytes(ProjectSettings.globalize_path(LOGO_FABRICA))
	if datos.is_empty(): return false
	var f := FileAccess.open(ProjectSettings.globalize_path(LOGO_PATH), FileAccess.WRITE)
	if f == null: return false
	f.store_buffer(datos)
	f.close()
	return true

# El logo por defecto TAMBIEN al instalar desde el zip y al actualizar.
# nuevo_proyecto.py deja el icono y el arranque puestos, pero quien descomprime
# GodotLuau.zip a mano se quedaba con el logo de Godot (visto en la Prueba 2).
# Aqui se completa lo que falte, sin pisar nada que el usuario haya puesto.
func _asegurar_logo_defecto() -> void:
	var cambio := false
	# 1. El logo de fabrica de Multi Game Inc (PNG de la 1.15-1.16 o SVG) pasa
	#    a ser el de Rocket Meteor.
	if _es_logo_mgi(LOGO_PNG_VIEJO) or _es_logo_mgi(LOGO_PATH):
		if _copiar_logo_fabrica():
			print("[GodotLuau] Default logo changed to Rocket Meteor")
			cambio = true
	# 2. Migrar el MenuLogo.png de la 1.16 (un logo del usuario).
	if not FileAccess.file_exists(LOGO_PATH) and FileAccess.file_exists(LOGO_PNG_VIEJO):
		var img := Image.new()
		if img.load(ProjectSettings.globalize_path(LOGO_PNG_VIEJO)) == OK and _img_a_svg(img, LOGO_PATH):
			print("[GodotLuau] Logo migrated to MenuLogo.svg")
			cambio = true
	# 3. Si no hay MenuLogo, nace como copia de la de fabrica.
	if not FileAccess.file_exists(LOGO_PATH) and _copiar_logo_fabrica():
		cambio = true
	if not FileAccess.file_exists(LOGO_PATH):
		return
	# 4. Icono y arranque SOLO si siguen en los de Godot o en los nuestros de
	#    antes (no pisar los que haya puesto el usuario).
	var icono := String(ProjectSettings.get_setting("application/config/icon", ""))
	var icono_viejo: bool = icono.is_empty() or icono == "res://icon.svg" or icono == LOGO_PNG_VIEJO \
		or icono in MGI_VIEJOS or not FileAccess.file_exists(icono)
	var con_imagen := bool(ProjectSettings.get_setting("application/boot_splash/show_image", true))
	var splash := String(ProjectSettings.get_setting("application/boot_splash/image", ""))
	# (Sin el PNG de fabrica, un arranque vacio se deja: si no, se reaplicaria
	# y guardaria el proyecto en cada arranque.)
	var splash_viejo: bool = splash == SPLASH_VIEJO or splash == LOGO_PNG_VIEJO \
		or (splash.is_empty() and (con_imagen or FileAccess.file_exists(SPLASH_FABRICA))) \
		or (con_imagen and not splash.is_empty() and not FileAccess.file_exists(splash))
	if cambio or icono_viejo or splash_viejo or not FileAccess.file_exists(ICO_PATH):
		_aplicar_marca()
		print("[GodotLuau] Default branding applied (GodotLuau window icon + boot splash)")
	_borrar_restos_marca()

# El arranque de Godot con el logo de GodotLuau sobre negro, y el icono de la
# ventana: el de GodotLuau con el logo de fabrica, o el logo del usuario.
func _aplicar_marca() -> void:
	var con_logo := FileAccess.file_exists(SPLASH_FABRICA)
	ProjectSettings.set_setting("application/boot_splash/show_image", con_logo)
	ProjectSettings.set_setting("application/boot_splash/image", SPLASH_FABRICA if con_logo else "")
	# A su tamaño, sin estirar: desde Godot 4.4 es stretch_mode (0); antes, fullsize.
	if ProjectSettings.has_setting("application/boot_splash/stretch_mode"):
		ProjectSettings.set_setting("application/boot_splash/stretch_mode", 0)
	else:
		ProjectSettings.set_setting("application/boot_splash/fullsize", false)
	ProjectSettings.set_setting("application/boot_splash/use_filter", true)
	ProjectSettings.set_setting("application/boot_splash/bg_color", Color(0, 0, 0, 1))
	var icono := LOGO_PATH
	if _logo_es_fabrica() and FileAccess.file_exists(ICONO_FABRICA):
		icono = ICONO_FABRICA
	# Godot pone su icono en la barra de titulo, en la barra de tareas y en la
	# ficha del proyecto; con esto sale el nuestro.
	ProjectSettings.set_setting("application/config/icon", icono)
	# Y el .ico, que es el que Windows pinta ANTES de que arranque el motor. Se
	# rehace siempre: con el logo nuevo el viejo se quedaba en la barra de tareas.
	_regenerar_ico(icono)
	if FileAccess.file_exists(ICO_PATH):
		ProjectSettings.set_setting("application/config/windows_native_icon", ICO_PATH)
	ProjectSettings.save()
	# La ventana abierta ahora mismo tambien, sin esperar a reiniciar.
	var img := Image.new()
	if img.load(ProjectSettings.globalize_path(icono)) == OK:
		DisplayServer.set_icon(img)

# Los restos de la marca de antes: BootSplash.png (el arranque ya es el logo de
# GodotLuau de fabrica), el logo de Multi Game Inc y el MenuLogo.png ya
# migrado. Nunca con el escaner del editor en marcha (crashea el editor): se
# reintenta.
func _borrar_restos_marca() -> void:
	var restos := []
	if String(ProjectSettings.get_setting("application/boot_splash/image", "")) != SPLASH_VIEJO:
		restos.append(SPLASH_VIEJO)
	restos.append_array(MGI_VIEJOS)
	if FileAccess.file_exists(LOGO_PATH):
		restos.append(LOGO_PNG_VIEJO)
	var hay := false
	for r in restos:
		if FileAccess.file_exists(r) or FileAccess.file_exists(r + ".import"):
			hay = true
	if not hay:
		return
	var fs := EditorInterface.get_resource_filesystem()
	if fs == null:
		return
	if fs.is_scanning():
		get_tree().create_timer(2.0).timeout.connect(_borrar_restos_marca)
		return
	for r in restos:
		for p in [r, r + ".import"]:
			if FileAccess.file_exists(p):
				DirAccess.remove_absolute(ProjectSettings.globalize_path(p))
	fs.scan()

# ── Importar un .rbxl sin salir del editor ───────────────────────────────────
func _pick_rbxl() -> void:
	var fd := EditorFileDialog.new()
	fd.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILE
	fd.access = EditorFileDialog.ACCESS_FILESYSTEM
	# .gdlu / .rbxm son el paquete del GodotLuau Exporter (mismo formato
	# binario que el .rbxl, pero con las imagenes del juego dentro).
	fd.add_filter("*.gdlu,*.rbxl,*.rbxm", "Roblox / GodotLuau")
	fd.title = _t("import_pick")
	EditorInterface.get_base_control().add_child(fd)
	fd.file_selected.connect(func(ruta: String) -> void:
		_importar_rbxl(ruta)
		fd.queue_free()
	)
	fd.canceled.connect(func() -> void: fd.queue_free())
	fd.popup_centered_ratio(0.6)

func _importar_rbxl(ruta: String) -> void:
	if not ClassDB.class_exists("RBXImporter"):
		_notify("RBXImporter no disponible (¿DLL cargada?)", Color(1.0, 0.5, 0.5))
		return
	var imp = ClassDB.instantiate("RBXImporter")
	imp.Begin(ruta)
	# Se avanza a trozos para no congelar el editor mientras carga.
	while not (imp.IsDone() or imp.HasFailed()):
		for i in range(60):
			if imp.IsDone() or imp.HasFailed(): break
			imp.Step()
		await EditorInterface.get_base_control().get_tree().process_frame
	if imp.HasFailed():
		_notify("No se pudo importar ese .rbxl", Color(1.0, 0.5, 0.5))
		return
	var raiz: Node = imp.GetResult()
	if raiz == null:
		_notify("El .rbxl se leyo pero vino vacio", Color(1.0, 0.5, 0.5))
		return
	var nombre := ruta.get_file().get_basename()
	raiz.name = nombre
	var escena := PackedScene.new()
	_dar_owner(raiz, raiz)
	if escena.pack(raiz) != OK:
		_notify("No se pudo empaquetar la escena", Color(1.0, 0.5, 0.5))
		return
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path("res://Escenas"))
	# Binaria (.scn): con Build a Mountain la de texto pesaba 32 MB y tardaba
	# 2,2 s en leerse al darle a Play; la binaria, 11,5 MB y 0,04 s.
	var destino := "res://Escenas/%s.scn" % nombre
	if ResourceSaver.save(escena, destino) != OK:
		_notify("No se pudo guardar " + destino, Color(1.0, 0.5, 0.5))
		return
	EditorInterface.get_resource_filesystem().scan()
	EditorInterface.open_scene_from_path(destino)
	_notify("Importado: " + destino, Color(0.5, 0.9, 0.5))

# Para que PackedScene guarde el arbol entero hace falta que TODO nodo tenga
# dueno; si no, se empaqueta solo la raiz y la escena sale vacia.
func _dar_owner(n: Node, raiz: Node) -> void:
	for h in n.get_children():
		h.owner = raiz
		_dar_owner(h, raiz)

func _rebuild_panel() -> void:
	# Reconstruye solo el contenido, sin quitar el panel inferior (evita el parpadeo)
	if not (_settings_panel and is_instance_valid(_settings_panel)):
		return
	_compute_theme_colors()
	for c in _settings_panel.get_children():
		_settings_panel.remove_child(c)
		c.queue_free()
	_stats_label   = null
	_cac_status    = null
	_cac_url_field = null
	_aim_status    = null
	_aim_url_field = null
	_ver_label     = null
	_ver_btn       = null
	_reinstall_btn = null
	_notif_bar     = null
	_notif_label   = null
	_build_panel_contents()

# ── Custom autocomplete ───────────────────────────────────────────────────────

func _refresh_cac_status() -> void:
	if not _cac_status or not is_instance_valid(_cac_status): return
	var path := OS.get_user_data_dir() + "/godotluau_custom_autocomplete.json"
	if not FileAccess.file_exists(path):
		_cac_status.text = _t("cac_status_none")
		_cac_status.add_theme_color_override("font_color", Color(0.55, 0.55, 0.55))
		return
	var f := FileAccess.open(path, FileAccess.READ)
	if not f: _cac_status.text = _t("cac_status_none"); return
	var parsed = JSON.parse_string(f.get_as_text())
	if parsed is Array:
		_cac_status.text = _t("cac_status_ok") % (parsed as Array).size()
		_cac_status.add_theme_color_override("font_color", Color(0.4, 0.9, 0.4))
	else:
		_cac_status.text = _t("cac_status_bad_json")
		_cac_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))

func _download_custom_ac() -> void:
	if _http_autocomplete and is_instance_valid(_http_autocomplete): return
	var url := str(ProjectSettings.get_setting("godot_luau/custom_autocomplete_url", ""))
	if url.is_empty(): return
	if _cac_status and is_instance_valid(_cac_status):
		_cac_status.text = _t("cac_status_dl")
		_cac_status.add_theme_color_override("font_color", Color(0.9, 0.85, 0.3))
	_http_autocomplete = HTTPRequest.new()
	_http_autocomplete.timeout = 20.0
	add_child(_http_autocomplete)
	_http_autocomplete.request_completed.connect(_on_custom_ac_downloaded)
	if _http_autocomplete.request(url) != OK:
		_http_autocomplete.queue_free(); _http_autocomplete = null
		if _cac_status and is_instance_valid(_cac_status):
			_cac_status.text = _t("cac_status_err")
			_cac_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))

func _on_custom_ac_downloaded(result: int, code: int, _hdrs: PackedStringArray, body: PackedByteArray) -> void:
	if _http_autocomplete and is_instance_valid(_http_autocomplete):
		_http_autocomplete.queue_free(); _http_autocomplete = null
	if result != HTTPRequest.RESULT_SUCCESS or code != 200:
		if _cac_status and is_instance_valid(_cac_status):
			_cac_status.text = _t("cac_status_err")
			_cac_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))
		return
	_save_custom_ac(body.get_string_from_utf8())

func _import_custom_ac_file() -> void:
	var dlg := FileDialog.new()
	dlg.title     = _t("cac_btn_import")
	dlg.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	dlg.access    = FileDialog.ACCESS_FILESYSTEM
	dlg.filters   = ["*.json ; JSON files"]
	dlg.file_selected.connect(func(p: String) -> void:
		var text := FileAccess.get_file_as_string(p)
		if not text.is_empty(): _save_custom_ac(text)
		dlg.queue_free()
	)
	dlg.canceled.connect(func(): dlg.queue_free())
	get_editor_interface().get_base_control().add_child(dlg)
	dlg.popup_centered_ratio(0.6)

func _save_custom_ac(json_text: String) -> void:
	var parsed = JSON.parse_string(json_text)
	if not _cac_status or not is_instance_valid(_cac_status): return
	if not parsed is Array:
		_cac_status.text = _t("cac_status_bad_json")
		_cac_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))
		return
	var path := OS.get_user_data_dir() + "/godotluau_custom_autocomplete.json"
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f: f.store_string(json_text)
	_cac_status.text = _t("cac_status_ok") % (parsed as Array).size()
	_cac_status.add_theme_color_override("font_color", Color(0.4, 0.9, 0.4))

func _clear_custom_ac() -> void:
	var path := OS.get_user_data_dir() + "/godotluau_custom_autocomplete.json"
	if FileAccess.file_exists(path): DirAccess.remove_absolute(path)
	_refresh_cac_status()
