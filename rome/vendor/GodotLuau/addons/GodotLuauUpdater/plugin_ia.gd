@tool
extends "res://addons/GodotLuauUpdater/plugin_actualizador.gd"

# Plugin de GodotLuau, parte 4 de 5: la IA. Modelos de autocompletado y
# GodotLuau IA (analizador, chat, descarga de modelos y arreglos).

# ── Modelos de IA: selección, descarga y estado ──────────────────────────────

func _ai_model_path() -> String:
	return OS.get_user_data_dir() + "/godotluau_ai_model.json"

func _selected_model_id() -> String:
	return str(ProjectSettings.get_setting("godot_luau/ai_model_selected", "mini"))

func _model_entry(id: String) -> Dictionary:
	for m in AI_MODELS:
		if m["id"] == id: return m
	return AI_MODELS[0]

func _model_file(id: String) -> String:
	if id == "custom": return _ai_model_path()
	return OS.get_user_data_dir() + "/godotluau_models/" + id + ".json"

func _ai_model_available(id: String) -> bool:
	var e := _model_entry(id)
	if e.get("builtin", false): return true
	return FileAccess.file_exists(_model_file(id))

# Descarga el modelo seleccionado del catálogo (solo cuando el usuario pulsa ⏬)
func _download_selected_model() -> void:
	if _http_aimodel and is_instance_valid(_http_aimodel): return
	var id := _selected_model_id()
	var e := _model_entry(id)
	var url := str(e.get("url", ""))
	if url.is_empty(): return
	if _aim_status and is_instance_valid(_aim_status):
		_aim_status.text = _t("cac_status_dl")
		_aim_status.add_theme_color_override("font_color", Color(0.9, 0.85, 0.3))
	_http_aimodel = HTTPRequest.new()
	_http_aimodel.timeout = 60.0
	add_child(_http_aimodel)
	_http_aimodel.request_completed.connect(func(result: int, code: int, _h: PackedStringArray, body: PackedByteArray) -> void:
		if _http_aimodel and is_instance_valid(_http_aimodel):
			_http_aimodel.queue_free(); _http_aimodel = null
		if result != HTTPRequest.RESULT_SUCCESS or code != 200:
			if _aim_status and is_instance_valid(_aim_status):
				_aim_status.text = _t("aim_status_err")
				_aim_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))
			return
		var text := body.get_string_from_utf8()
		var parsed = JSON.parse_string(text)
		if not (parsed is Dictionary and (parsed as Dictionary).has("bigrams")):
			if _aim_status and is_instance_valid(_aim_status):
				_aim_status.text = _t("aim_status_bad")
				_aim_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))
			return
		DirAccess.make_dir_recursive_absolute(OS.get_user_data_dir() + "/godotluau_models")
		var f := FileAccess.open(_model_file(id), FileAccess.WRITE)
		if f: f.store_string(text)
		_rebuild_panel()
	)
	if _http_aimodel.request(url) != OK:
		_http_aimodel.queue_free(); _http_aimodel = null
		if _aim_status and is_instance_valid(_aim_status):
			_aim_status.text = _t("aim_status_err")
			_aim_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))

func _refresh_aim_status() -> void:
	if not (_aim_status and is_instance_valid(_aim_status)): return
	var id := _selected_model_id()
	var e := _model_entry(id)
	if e.get("builtin", false):
		_aim_status.text = _t("aim_builtin_note")
		_aim_status.add_theme_color_override("font_color", Color(0.4, 0.9, 0.4))
		return
	var path := _model_file(id)
	if not FileAccess.file_exists(path):
		_aim_status.text = _t("aim_missing")
		_aim_status.add_theme_color_override("font_color", Color(0.9, 0.75, 0.3))
		return
	var parsed = JSON.parse_string(FileAccess.get_file_as_string(path))
	if parsed is Dictionary and (parsed as Dictionary).has("bigrams"):
		var n : int = int((parsed as Dictionary).get("params", 0))
		if n == 0:
			n = ((parsed as Dictionary)["bigrams"] as Dictionary).size()
			if (parsed as Dictionary).has("trigrams"):
				n += ((parsed as Dictionary)["trigrams"] as Dictionary).size()
		_aim_status.text = _t("aim_status_ok") % n
		_aim_status.add_theme_color_override("font_color", Color(0.4, 0.9, 0.4))
	else:
		_aim_status.text = _t("aim_status_bad")
		_aim_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))

func _save_ai_model(json_text: String) -> void:
	var parsed = JSON.parse_string(json_text)
	if not (parsed is Dictionary and (parsed as Dictionary).has("bigrams")):
		if _aim_status and is_instance_valid(_aim_status):
			_aim_status.text = _t("aim_status_bad")
			_aim_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))
		return
	var f := FileAccess.open(_ai_model_path(), FileAccess.WRITE)
	if f: f.store_string(json_text)
	_refresh_aim_status()

func _download_ai_model() -> void:
	if _http_aimodel and is_instance_valid(_http_aimodel): return
	var url := str(ProjectSettings.get_setting("godot_luau/ai_model_url", ""))
	if url.is_empty(): return
	if _aim_status and is_instance_valid(_aim_status):
		_aim_status.text = _t("cac_status_dl")
		_aim_status.add_theme_color_override("font_color", Color(0.9, 0.85, 0.3))
	_http_aimodel = HTTPRequest.new()
	_http_aimodel.timeout = 30.0
	add_child(_http_aimodel)
	_http_aimodel.request_completed.connect(func(result: int, code: int, _h: PackedStringArray, body: PackedByteArray) -> void:
		if _http_aimodel and is_instance_valid(_http_aimodel):
			_http_aimodel.queue_free(); _http_aimodel = null
		if result != HTTPRequest.RESULT_SUCCESS or code != 200:
			if _aim_status and is_instance_valid(_aim_status):
				_aim_status.text = _t("aim_status_err")
				_aim_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))
			return
		_save_ai_model(body.get_string_from_utf8())
	)
	if _http_aimodel.request(url) != OK:
		_http_aimodel.queue_free(); _http_aimodel = null
		if _aim_status and is_instance_valid(_aim_status):
			_aim_status.text = _t("aim_status_err")
			_aim_status.add_theme_color_override("font_color", Color(1.0, 0.5, 0.5))

func _import_ai_model_file() -> void:
	var dlg := FileDialog.new()
	dlg.title     = _t("cac_btn_import")
	dlg.file_mode = FileDialog.FILE_MODE_OPEN_FILE
	dlg.access    = FileDialog.ACCESS_FILESYSTEM
	dlg.filters   = ["*.json ; JSON files"]
	dlg.file_selected.connect(func(p: String) -> void:
		var text := FileAccess.get_file_as_string(p)
		if not text.is_empty(): _save_ai_model(text)
		dlg.queue_free()
	)
	dlg.canceled.connect(func(): dlg.queue_free())
	get_editor_interface().get_base_control().add_child(dlg)
	dlg.popup_centered_ratio(0.6)

func _clear_ai_model() -> void:
	if FileAccess.file_exists(_ai_model_path()):
		DirAccess.remove_absolute(_ai_model_path())
	_refresh_aim_status()


# ══════════════════════════════════════════════════════════════════════════
#  GODOTLUAU IA — Capa 0
#
#  El analizador NO es un modelo: parsea con el Luau real del motor y cruza
#  cada simbolo contra lo que el puente implementa de verdad. Por eso da un
#  veredicto exacto en milisegundos, sin descargar nada y en cualquier PC.
#  Las capas de chat y reescritura con modelo se apoyaran sobre esto.
# ══════════════════════════════════════════════════════════════════════════

func _gia_analizador() -> Object:
	if not ClassDB.class_exists("GLCompat"):
		return null
	return ClassDB.instantiate("GLCompat")


# Ejecuta el analisis y devuelve [hallazgos, resumen, ms]. `donde` es
# "proyecto" (los .lua/.luau de res://) o "escena" (los nodos de script de la
# escena abierta, que es donde vive el codigo de un place importado).
func _gia_ejecutar(donde: String) -> Array:
	var c := _gia_analizador()
	if c == null:
		return []
	var t0 := Time.get_ticks_msec()
	var h : Array = []
	if donde == "escena":
		var raiz := EditorInterface.get_edited_scene_root()
		if raiz != null:
			h = c.analizar_nodo(raiz)
	else:
		h = c.analizar_carpeta("res://")
	return [h, c.resumen(h), Time.get_ticks_msec() - t0]


func _gia_analizar(donde: String) -> void:
	if _gia_status != null:
		_gia_status.text = _t("gia_working")
	var r := _gia_ejecutar(donde)
	if r.is_empty():
		if _gia_status != null:
			_gia_status.text = "❌ GLCompat no disponible (recompila la extensión)."
		return
	var h : Array = r[0]
	var res : Dictionary = r[1]
	var ms : int = r[2]
	if _gia_status != null:
		if h.is_empty():
			_gia_status.text = _t("gia_clean")
			_gia_status.add_theme_color_override("font_color", Color(0.5, 0.9, 0.5))
		else:
			_gia_status.text = _t("gia_found") % [res["total"], res["graves"], res["avisos"], ms]
			_gia_status.add_theme_color_override("font_color", Color(1.0, 0.75, 0.3))
	_gia_ult_analisis = h
	_gia_abrir_chat()
	_gia_volcar_informe(donde, h, res, ms)


# ── La ventana del chat ──────────────────────────────────────────────────
func _gia_abrir_chat() -> void:
	if _gia_win != null and is_instance_valid(_gia_win):
		_gia_chat_refrescar_estado()
		_gia_win.popup_centered(Vector2i(940, 680))
		return

	_gia_win = Window.new()
	_gia_win.title = "GodotLuau IA"
	_gia_win.min_size = Vector2i(640, 460)
	_gia_win.close_requested.connect(func() -> void: _gia_win.hide())

	# Fondo propio: sin esto la ventana sale desnuda sobre el gris del editor.
	var marco := PanelContainer.new()
	marco.set_anchors_preset(Control.PRESET_FULL_RECT)
	var fondo := StyleBoxFlat.new()
	fondo.bg_color = _col_card
	fondo.set_content_margin_all(14)
	marco.add_theme_stylebox_override("panel", fondo)
	_gia_win.add_child(marco)

	var raiz := VBoxContainer.new()
	raiz.add_theme_constant_override("separation", 10)
	marco.add_child(raiz)

	# ── Cabecera: que hay instalado, de un vistazo ───────────────────
	var cab := HBoxContainer.new()
	cab.add_theme_constant_override("separation", 10)
	raiz.add_child(cab)
	var tit := Label.new()
	tit.text = "GodotLuau IA"
	tit.add_theme_font_size_override("font_size", _fs(16))
	tit.add_theme_color_override("font_color", _accent(Color(0.75, 0.55, 1.0)))
	cab.add_child(tit)
	_gia_estado_l = Label.new()
	_gia_estado_l.add_theme_font_size_override("font_size", _fs(11))
	_gia_estado_l.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	cab.add_child(_gia_estado_l)

	# ── Acciones instantaneas (no necesitan modelo) ──────────────────
	var acciones := HBoxContainer.new()
	acciones.add_theme_constant_override("separation", 6)
	raiz.add_child(acciones)
	for par in [["📂  " + _t("gia_scan_project"), "proyecto"], ["🎬  " + _t("gia_scan_scene"), "escena"]]:
		var b := Button.new()
		b.text = par[0]
		var donde : String = par[1]
		b.pressed.connect(func() -> void: _gia_analizar(donde))
		acciones.add_child(b)
	var bconv := Button.new()
	bconv.text = "🔧  " + _t("gia_convert")
	bconv.tooltip_text = "Selecciona un script en la escena y pulsa aqui."
	bconv.pressed.connect(_gia_c2_arreglar_seleccion)
	acciones.add_child(bconv)

	raiz.add_child(HSeparator.new())

	# ── La conversacion ──────────────────────────────────────────────
	_gia_log = RichTextLabel.new()
	_gia_log.bbcode_enabled = true
	_gia_log.selection_enabled = true
	_gia_log.scroll_following = true
	_gia_log.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_gia_log.custom_minimum_size = Vector2(0, 320)
	_gia_log.add_theme_constant_override("line_separation", 3)
	raiz.add_child(_gia_log)

	# ── Sugerencias: se pulsan y se envian ───────────────────────────
	_gia_chips = HBoxContainer.new()
	_gia_chips.add_theme_constant_override("separation", 6)
	raiz.add_child(_gia_chips)
	_gia_chat_poner_chips(GIA_SUGERENCIAS)

	# ── La caja de escribir, con fantasma para Tab ───────────────────
	var fila := HBoxContainer.new()
	fila.add_theme_constant_override("separation", 6)
	raiz.add_child(fila)

	_gia_nivel = OptionButton.new()
	_gia_nivel.add_item("⚡ Rápido", 0)
	_gia_nivel.add_item("⚖ Normal", 1)
	_gia_nivel.add_item("🧠 Profundo", 2)
	_gia_nivel.select(1)
	_gia_nivel.tooltip_text = "Cuanto más alto, mejor responde y más tarda.\nLa reescritura de código usa siempre el modelo más potente."
	fila.add_child(_gia_nivel)

	# El fantasma es un Label GRIS puesto DEBAJO del LineEdit (mismo sitio,
	# misma fuente): se ve el resto de la frase en gris y Tab lo acepta. Es la
	# forma de tener autocompletado en linea sin reescribir un editor de texto.
	var pila := Control.new()
	pila.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	pila.custom_minimum_size = Vector2(0, 30)
	fila.add_child(pila)

	_gia_fantasma = Label.new()
	_gia_fantasma.set_anchors_preset(Control.PRESET_FULL_RECT)
	_gia_fantasma.add_theme_color_override("font_color", Color(1, 1, 1, 0.28))
	_gia_fantasma.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	_gia_fantasma.mouse_filter = Control.MOUSE_FILTER_IGNORE
	pila.add_child(_gia_fantasma)

	_gia_entrada = LineEdit.new()
	_gia_entrada.set_anchors_preset(Control.PRESET_FULL_RECT)
	_gia_entrada.placeholder_text = "Pregunta sobre GodotLuau, Godot o Roblox…   (Tab completa)"
	_gia_entrada.flat = true
	_gia_entrada.text_changed.connect(_gia_chat_al_escribir)
	_gia_entrada.text_submitted.connect(func(_t2: String) -> void: _gia_enviar_mensaje())
	_gia_entrada.gui_input.connect(_gia_chat_input)
	pila.add_child(_gia_entrada)

	_gia_enviar = Button.new()
	_gia_enviar.text = "Enviar"
	_gia_enviar.pressed.connect(_gia_enviar_mensaje)
	fila.add_child(_gia_enviar)

	EditorInterface.get_base_control().add_child(_gia_win)
	_gia_win.popup_centered(Vector2i(940, 680))
	_gia_chat_refrescar_estado()
	_gia_log.append_text("[color=#888]Las revisiones de arriba son instantáneas y no necesitan ningún modelo.[/color]\n\n")


# Frases de arranque: se enseñan como botones y ademas alimentan el Tab.
const GIA_SUGERENCIAS := [
	"¿Qué es un ModuleScript?",
	"¿Puedo usar Tool.Enabled aquí?",
	"¿Cómo guardo datos como en Roblox?",
	"¿Por qué mi BillboardGui no se ve?",
	"¿Qué diferencias hay con Roblox?",
	"¿Cómo hago que un Part sea tocable?",
]


func _gia_chat_poner_chips(lista: Array) -> void:
	if _gia_chips == null or not is_instance_valid(_gia_chips):
		return
	for c in _gia_chips.get_children():
		c.queue_free()
	var n := 0
	for t in lista:
		var b := Button.new()
		b.text = str(t)
		b.add_theme_font_size_override("font_size", _fs(10))
		var txt := str(t)
		b.pressed.connect(func() -> void:
			_gia_entrada.text = txt
			_gia_fantasma.text = ""
			_gia_sug_actual = ""
			_gia_enviar_mensaje())
		_gia_chips.add_child(b)
		n += 1
		if n >= 3:
			break


# Cabecera con el estado real, para que se vea POR QUE no responde el chat.
func _gia_chat_refrescar_estado() -> void:
	if _gia_estado_l == null or not is_instance_valid(_gia_estado_l):
		return
	var hay_motor := _gia_motor_disponible()
	var modelo := _gia_mejor_modelo()
	if not hay_motor:
		_gia_estado_l.text = "· el chat necesita el motor de IA (descárgalo en el panel)"
		_gia_estado_l.add_theme_color_override("font_color", Color(1.0, 0.75, 0.3))
	elif modelo == "":
		_gia_estado_l.text = "· motor listo, falta descargar un modelo"
		_gia_estado_l.add_theme_color_override("font_color", Color(1.0, 0.75, 0.3))
	else:
		_gia_estado_l.text = "· listo (%s) — todo corre en tu PC" % modelo
		_gia_estado_l.add_theme_color_override("font_color", Color(0.5, 0.9, 0.5))
	var puede := hay_motor and modelo != ""
	if _gia_entrada != null and is_instance_valid(_gia_entrada):
		_gia_entrada.editable = puede
	if _gia_enviar != null and is_instance_valid(_gia_enviar):
		_gia_enviar.disabled = not puede


# ── Autocompletado en linea ──────────────────────────────────────────────
# Mientras escribes, se busca la primera sugerencia que empiece igual y el
# resto se pinta en gris detras del cursor. Tab la acepta, como en un chat
# moderno; Escape la descarta.
func _gia_chat_al_escribir(txt: String) -> void:
	_gia_sug_actual = ""
	if _gia_fantasma == null or not is_instance_valid(_gia_fantasma):
		return
	if txt.strip_edges().is_empty():
		_gia_fantasma.text = ""
		_gia_chat_poner_chips(GIA_SUGERENCIAS)
		return
	var bajo := txt.to_lower()
	var filtradas: Array = []
	for sug in GIA_SUGERENCIAS:
		if str(sug).to_lower().begins_with(bajo):
			if _gia_sug_actual == "":
				_gia_sug_actual = str(sug)
			filtradas.append(sug)
		elif str(sug).to_lower().contains(bajo):
			filtradas.append(sug)
	# El fantasma: lo ya escrito en transparente + el resto en gris, para que
	# la parte gris caiga justo donde toca sin medir la fuente a mano.
	if _gia_sug_actual != "":
		_gia_fantasma.text = txt + _gia_sug_actual.substr(txt.length())
		_gia_fantasma.add_theme_color_override("font_color", Color(1, 1, 1, 0.25))
	else:
		_gia_fantasma.text = ""
	_gia_chat_poner_chips(filtradas if not filtradas.is_empty() else [])


func _gia_chat_input(e: InputEvent) -> void:
	if not (e is InputEventKey and e.pressed):
		return
	var k := e as InputEventKey
	if k.keycode == KEY_TAB and _gia_sug_actual != "":
		_gia_entrada.text = _gia_sug_actual
		_gia_entrada.caret_column = _gia_entrada.text.length()
		_gia_fantasma.text = ""
		_gia_sug_actual = ""
		_gia_entrada.accept_event()
	elif k.keycode == KEY_ESCAPE:
		_gia_fantasma.text = ""
		_gia_sug_actual = ""


# ── El informe ───────────────────────────────────────────────────────────
func _gia_volcar_informe(donde: String, h: Array, res: Dictionary, ms: int) -> void:
	if _gia_log == null:
		return
	var titulo := "todo el proyecto" if donde == "proyecto" else "la escena abierta"
	_gia_log.append_text("\n[b]▸ Revisión de %s[/b]  [color=#888](%d ms)[/color]\n" % [titulo, ms])
	if h.is_empty():
		_gia_log.append_text("[color=#7fdc7f]%s[/color]\n" % _t("gia_clean"))
		return
	_gia_log.append_text("[color=#ffbf55]%d hallazgo(s) — %d graves[/color]\n\n"
		% [res["total"], res["graves"]])

	# Agrupado por tipo: primero lo que rompe el juego seguro.
	var etiquetas := {
		"sintaxis": "Errores de sintaxis",
		"clase":    "Instance.new() de clases que aquí no existen",
		"servicio": "GetService() que no resuelven",
		"enum":     "Enums que faltan (las comparaciones dan SIEMPRE falso)",
		"miembro":  "Propiedades y métodos que el puente no implementa",
	}
	for tipo in ["sintaxis", "clase", "servicio", "enum", "miembro"]:
		var grupo := []
		for d in h:
			if d["tipo"] == tipo:
				grupo.append(d)
		if grupo.is_empty():
			continue
		_gia_log.append_text("[b][color=#c8a2ff]%s[/color][/b]  (%d)\n" % [etiquetas[tipo], grupo.size()])
		var n := 0
		for d in grupo:
			if n >= 40:
				_gia_log.append_text("   [color=#888]… y %d más[/color]\n" % (grupo.size() - n))
				break
			var arch : String = str(d["archivo"]).get_file()
			_gia_log.append_text("   [color=#ff8a7a]%s[/color]  [color=#888]%s:%d[/color]\n%s\n"
				% [d["simbolo"], arch, d["linea"], d["mensaje"]])
			n += 1
		_gia_log.append_text("\n")


# ══════════════════════════════════════════════════════════════════════════
#  GODOTLUAU IA — Capa 1: el chat local
#
#  El modelo corre con llama.cpp DENTRO de una extension aparte (opcional).
#  Lo importante no es que hable, sino que NO SE INVENTE la compatibilidad:
#  antes de preguntar, la Capa 0 resuelve los simbolos que menciona el usuario
#  y el veredicto se le entrega ya masticado en el contexto. El modelo redacta;
#  quien sabe qué soporta el motor somos nosotros.
# ══════════════════════════════════════════════════════════════════════════

# ── LOS MODELOS NO VAN EN user:// ────────────────────────────────────────
#
# `user://` es UNA CARPETA POR PROYECTO. Con los modelos ahi, cada proyecto
# nuevo no veia los que ya estaban bajados y se los volvia a descargar entero.
# Medido en la maquina del usuario: 11,6 GB en CUATRO copias de dos modelos que
# ocupan 5,8 GB, repartidas entre app_userdata/Prueba 11/, app_userdata/Prueba3/
# y la carpeta compartida antigua.
#
# Ahora viven en un sitio compartido, FUERA de cualquier proyecto:
#     Windows   %APPDATA%\GodotLuau\ia_modelos
#     Linux     ~/.local/share/GodotLuau/ia_modelos
#     macOS     ~/Library/Application Support/GodotLuau/ia_modelos
# Se bajan UNA vez y los ven todos los proyectos. Ademas el proyecto no engorda:
# lo unico que se instala dentro es el motor (unos 7 MB), porque una GDExtension
# tiene que vivir en el proyecto por narices.
func _gia_dir_modelos() -> String:
	return OS.get_data_dir().path_join("GodotLuau").path_join("ia_modelos") + "/"


# Sitios donde los modelos vivieron antes. De aqui se RECUPERAN (moviendolos,
# que en el mismo disco es instantaneo) en vez de volver a descargarlos.
func _gia_dirs_antiguos() -> Array:
	var out: Array = []
	# a) La carpeta del proyecto actual.
	out.append(ProjectSettings.globalize_path("user://godotluau_ia/"))
	# b) La carpeta compartida de una version anterior.
	out.append(OS.get_data_dir().path_join("Godot").path_join("godotluau_ia_modelos") + "/")
	# c) La carpeta de CUALQUIER otro proyecto de Godot de esta maquina: ahi es
	#    donde estan las copias que sobran y ocupan gigas.
	var base := OS.get_data_dir().path_join("Godot").path_join("app_userdata")
	var d := DirAccess.open(base)
	if d != null:
		d.list_dir_begin()
		var nombre := d.get_next()
		while nombre != "":
			if d.current_is_dir() and nombre != "." and nombre != "..":
				out.append(base.path_join(nombre).path_join("godotluau_ia") + "/")
			nombre = d.get_next()
		d.list_dir_end()
	return out


func _gia_tam_fichero(ruta: String) -> int:
	var f := FileAccess.open(ruta, FileAccess.READ)
	if f == null:
		return -1
	var n := f.get_length()
	f.close()
	return n


# Recupera los modelos que ya estan bajados y suelta los duplicados. Se llama al
# arrancar el plugin: es barato (mira si existen unos pocos ficheros) y ahorra
# volver a bajarse gigabytes.
func _gia_migrar_modelos() -> void:
	var dir := _gia_dir_modelos()
	DirAccess.make_dir_recursive_absolute(dir)
	var viejos := _gia_dirs_antiguos()
	var movidos := 0
	var liberados := 0
	for m in GIA_MODELOS:
		var arch := str(m["archivo"])
		var destino := dir + arch
		if not FileAccess.file_exists(destino):
			# Todavia no esta en su sitio: traerselo del primero que lo tenga.
			for v in viejos:
				var origen: String = str(v) + arch
				if origen == destino or not FileAccess.file_exists(origen):
					continue
				if DirAccess.rename_absolute(origen, destino) == OK:
					movidos += 1
					break
				# Otro disco: copiar y luego soltar el original.
				if DirAccess.copy_absolute(origen, destino) == OK:
					DirAccess.remove_absolute(origen)
					movidos += 1
					break
		if not FileAccess.file_exists(destino):
			continue
		# Ya esta en su sitio: las copias sobrantes se borran, pero SOLO si son
		# del mismo tamaño exacto. Un fichero a medias o distinto no se toca.
		var bueno := _gia_tam_fichero(destino)
		for v in viejos:
			var otro: String = str(v) + arch
			if otro == destino or not FileAccess.file_exists(otro):
				continue
			if _gia_tam_fichero(otro) == bueno and bueno > 0:
				DirAccess.remove_absolute(otro)
				liberados += bueno
	if movidos > 0 or liberados > 0:
		print("[GodotLuau IA] modelos: %d recuperados, %.2f GB liberados de copias repetidas -> %s"
			% [movidos, liberados / 1073741824.0, dir])

# Los dos modelos son Apache-2.0 (verificado en la API de HuggingFace), asi que
# se pueden redistribuir y usar en proyectos comerciales sin ataduras.
const GIA_MODELOS := [
	{"id": "chat", "nombre": "Qwen2.5-Coder 1.5B (chat)", "archivo": "chat-1.5b.gguf",
	 "tam": "1,04 GB", "bytes": 1117320768,
	 "url": "https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/main/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf"},
	{"id": "codigo", "nombre": "Qwen2.5-Coder 7B (reescritura)", "archivo": "codigo-7b.gguf",
	 "tam": "4,36 GB", "bytes": 4683073536,
	 "url": "https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/main/qwen2.5-coder-7b-instruct-q4_k_m.gguf"},
]


func _gia_ruta_modelo(id: String) -> String:
	for m in GIA_MODELOS:
		if m["id"] == id:
			return _gia_dir_modelos() + str(m["archivo"])
	return ""


func _gia_modelo_disponible(id: String) -> bool:
	var r := _gia_ruta_modelo(id)
	return r != "" and FileAccess.file_exists(r)


func _gia_motor_disponible() -> bool:
	return ClassDB.class_exists("GLLocalAI")


# El modelo mas potente que el usuario tenga: la reescritura de codigo SIEMPRE
# usa este, sea cual sea el nivel de pensamiento elegido.
func _gia_mejor_modelo() -> String:
	if _gia_modelo_disponible("codigo"):
		return "codigo"
	if _gia_modelo_disponible("chat"):
		return "chat"
	return ""


func _gia_asegurar_ia(id: String) -> bool:
	if not _gia_motor_disponible():
		return false
	if _gia_ia == null or not is_instance_valid(_gia_ia):
		_gia_ia = ClassDB.instantiate("GLLocalAI")
		if _gia_ia == null:
			return false
		EditorInterface.get_base_control().add_child(_gia_ia)
		_gia_ia.trozo.connect(_gia_on_trozo)
		_gia_ia.terminado.connect(_gia_on_fin)
		_gia_ia.fallo.connect(_gia_on_fallo)
	if _gia_ia.esta_listo():
		return true
	var ruta := _gia_ruta_modelo(id)
	if ruta == "" or not FileAccess.file_exists(ruta):
		return false
	return bool(_gia_ia.cargar_modelo(ruta, 4096, 0))


# Resuelve con la Capa 0 los simbolos que menciona una pregunta. Es la unica
# fuente de verdad sobre compatibilidad: el modelo solo redacta.
func _gia_hechos_de(pregunta: String) -> Array:
	var out: Array = []
	var c := _gia_analizador()
	if c == null:
		return out
	var vistos := {}
	var re := RegEx.new()
	re.compile("[A-Za-z_][A-Za-z0-9_]*([.][A-Za-z0-9_]+)?")
	for m in re.search_all(pregunta):
		var sim := m.get_string()
		if sim.length() < 4 or vistos.has(sim):
			continue
		# Solo lo que parece un simbolo de API (empieza en mayuscula)
		if sim[0].to_upper() != sim[0] or sim[0].to_lower() == sim[0].to_upper():
			continue
		vistos[sim] = true
		var d: Dictionary = c.consultar(sim)
		if bool(d["soportado"]) or bool(d["existe_en_roblox"]):
			out.append(d)
		if out.size() >= 8:
			break
	return out


# ── El contexto que se le da al modelo ───────────────────────────────────
func _gia_sistema(pregunta: String) -> String:
	var t := """Eres GodotLuau IA, el asistente de GodotLuau: una extension que hace correr juegos de Roblox (Luau) dentro de Godot 4.
Respondes en espanol, breve y tecnico, sin rodeos ni saludos.

REGLA ABSOLUTA: si mas abajo aparece una seccion DATOS COMPROBADOS, esos datos
mandan sobre lo que tu creas saber. Si un dato dice que algo NO esta implementado,
tu respuesta TIENE que decir que NO se puede usar todavia. Nunca contradigas un dato.
"""
	# Lo que de verdad diferencia a este chat: los hechos de compatibilidad NO
	# los pone el modelo, los pone el analizador (Capa 0).
	var hechos := ""
	for d in _gia_hechos_de(pregunta):
		hechos += "- %s (%s): %s\n" % [d["simbolo"], d["tipo"], d["veredicto"]]
	if hechos != "":
		t += "\nDATOS COMPROBADOS del motor (son ciertos, mandan sobre tu conocimiento):\n" + hechos
	if not _gia_ult_analisis.is_empty():
		t += "\nUltimo analisis del proyecto: %d incompatibilidades encontradas.\n" % _gia_ult_analisis.size()
		var n := 0
		for d in _gia_ult_analisis:
			t += "- %s (%s) en %s:%d\n" % [d["simbolo"], d["tipo"], str(d["archivo"]).get_file(), d["linea"]]
			n += 1
			if n >= 8:
				break
	# Cerrar con una orden: si el contexto acaba en una lista de datos, el
	# modelo a veces da el turno por terminado y no responde nada.
	t += "\nResponde ahora a la pregunta del usuario usando esos datos.\n"
	return t


# ── Enviar / recibir ─────────────────────────────────────────────────────
func _gia_enviar_mensaje() -> void:
	if _gia_entrada == null:
		return
	var texto := _gia_entrada.text.strip_edges()
	if texto == "":
		return
	if not _gia_motor_disponible():
		_gia_log.append_text("\n[color=#ff8a7a]El motor de IA no esta instalado. Descárgalo desde el panel.[/color]\n")
		return
	var id := _gia_mejor_modelo()
	if id == "":
		_gia_log.append_text("\n[color=#ff8a7a]No hay ningún modelo descargado todavía.[/color]\n")
		return
	if _gia_ia != null and is_instance_valid(_gia_ia) and _gia_ia.esta_generando():
		return
	_gia_entrada.text = ""
	_gia_log.append_text("\n[b][color=#8ecbff]Tú:[/color][/b] %s\n" % texto)
	if not _gia_asegurar_ia(id):
		_gia_log.append_text("[color=#ff8a7a]No se pudo cargar el modelo.[/color]\n")
		return
	# La verdad NO puede depender de que el modelo haga caso: los veredictos de
	# compatibilidad se imprimen aqui, medidos por la Capa 0, ANTES de que el
	# modelo diga nada. Un modelo pequeño ignora el contexto mas de lo que
	# gustaria (medido: con 1,5B llego a afirmar lo contrario del dato), asi que
	# lo que no puede fallar no se le delega.
	var hechos_ui := _gia_hechos_de(texto)
	if not hechos_ui.is_empty():
		_gia_log.append_text("[b][color=#7fdc7f]Datos comprobados del motor:[/color][/b]\n")
		for h in hechos_ui:
			var icono := "✅" if bool(h["soportado"]) else ("⛔" if bool(h["existe_en_roblox"]) else "•")
			_gia_log.append_text("  %s [b]%s[/b] — %s\n" % [icono, h["simbolo"], h["veredicto"]])
	_gia_log.append_text("[b][color=#c8a2ff]GodotLuau IA:[/color][/b] ")
	var nivel := _gia_nivel.get_selected_id() if _gia_nivel != null else 1
	_gia_enviar.disabled = true
	_gia_ia.preguntar(_gia_sistema(texto), texto, nivel)


func _gia_on_trozo(t: String) -> void:
	if _gia_c2_en_marcha():
		return   # el parche se enseña ya verificado, no mientras se escribe
	if _gia_log != null:
		_gia_log.append_text(t)


func _gia_on_fin(texto: String) -> void:
	# Si la respuesta era un parche de la Capa 2 no se escribe en el chat: se
	# manda a verificar y solo se enseña si pasa los tres filtros.
	if _gia_c2_en_marcha():
		_gia_c2_respuesta(texto)
		return
	if _gia_log != null:
		_gia_log.append_text("\n")
	if _gia_enviar != null:
		_gia_enviar.disabled = false


func _gia_on_fallo(msg: String) -> void:
	if _gia_log != null:
		_gia_log.append_text("\n[color=#ff8a7a]Error: %s[/color]\n" % msg)
	if _gia_enviar != null:
		_gia_enviar.disabled = false

# ==========================================================================
#  DESCARGADOR DE GODOTLUAU IA
#
#  Dos cosas distintas, las dos OPCIONALES:
#    - el MOTOR   : la extension del chat (zip de ~5,5 MB con llama.cpp dentro)
#    - los MODELOS: .gguf de 1 a 5 GB, que es lo que de verdad piensa
#
#  Con su propio boton de descargar/actualizar, aparte de la version de
#  GodotLuau: se puede mejorar la IA sin publicar una version nueva del motor,
#  y quien no la quiera no descarga ni un byte.
#
#  Los archivos grandes se bajan en STREAMING a disco (download_file) y a un
#  ".part" que solo se renombra al terminar: una descarga cortada nunca deja
#  un modelo a medias que luego reviente al cargarlo.
# ==========================================================================

const GIA_MOTOR_URL := "https://github.com/Pimpoli/GodotLuau/raw/main/GodotLuau%20IA.zip"


func _gia_dl_activa() -> bool:
	return _gia_dl != null and is_instance_valid(_gia_dl)


func _gia_fmt_bytes(b: float) -> String:
	if b >= 1073741824.0:
		return "%.2f GB" % (b / 1073741824.0)
	if b >= 1048576.0:
		return "%.1f MB" % (b / 1048576.0)
	return "%.0f KB" % (b / 1024.0)


# Arranca una descarga. `que` es "motor" o el id de un modelo del catalogo.
func _gia_descargar(que: String) -> void:
	if _gia_dl_activa():
		return
	var url := ""
	var destino := ""
	var etiqueta := ""
	if que == "motor":
		url = GIA_MOTOR_URL
		destino = OS.get_user_data_dir() + "/godotluau_ia_motor.zip"
		etiqueta = "motor de IA"
	else:
		for m in GIA_MODELOS:
			if m["id"] == que:
				url = str(m["url"])
				destino = _gia_dir_modelos() + str(m["archivo"])   # ya es absoluta
				etiqueta = str(m["nombre"])
		if url == "":
			return
	DirAccess.make_dir_recursive_absolute(destino.get_base_dir())
	var parcial := destino + ".part"
	DirAccess.remove_absolute(parcial)

	_gia_dl_que     = que
	_gia_dl_destino = destino
	_gia_dl_t0      = Time.get_ticks_msec()
	# El tamaño REAL, del catalogo. No se le puede preguntar a HTTPRequest:
	# get_body_size() es de 32 bits y con un modelo de 4,36 GB devuelve
	# 4683073536 mod 2^32 = 388106240 (370 MB). Ver _gia_dl_tick.
	_gia_dl_total = 0
	for m2 in GIA_MODELOS:
		if m2["id"] == que:
			_gia_dl_total = int(m2.get("bytes", 0))

	_gia_dl = HTTPRequest.new()
	_gia_dl.use_threads         = true      # sin esto el editor se congela
	_gia_dl.download_file       = parcial
	_gia_dl.body_size_limit     = -1        # sin tope: son gigabytes
	_gia_dl.download_chunk_size = 1048576   # 1 MB por trozo
	_gia_dl.max_redirects       = 8         # HuggingFace redirige a su CDN
	_gia_dl.timeout             = 0.0       # 0 = sin limite: la descarga es larga
	add_child(_gia_dl)
	_gia_dl.request_completed.connect(_gia_dl_completada)

	if _gia_dl.request(url) != OK:
		_gia_dl_limpiar()
		_gia_dl_estado("No se pudo iniciar la descarga de " + etiqueta, Color(1.0, 0.45, 0.45))
		return

	# La barra se refresca 4 veces por segundo: con un _process en el editor se
	# pagaria cada frame para nada.
	if _gia_dl_timer == null or not is_instance_valid(_gia_dl_timer):
		_gia_dl_timer = Timer.new()
		_gia_dl_timer.wait_time = 0.25
		_gia_dl_timer.timeout.connect(_gia_dl_tick)
		add_child(_gia_dl_timer)
	_gia_dl_timer.start()
	_gia_dl_ui_visible(true)
	_gia_dl_estado("Descargando " + etiqueta + "...", Color(0.4, 0.8, 1.0))


# Cuanto se lleva descargado DE VERDAD.
#
# NO se le pregunta a HTTPRequest: get_downloaded_bytes() y get_body_size()
# son de 32 BITS. Con un modelo de 4,36 GB el total salia como 370 MB
# (4683073536 mod 2^32) y lo descargado se iba a NEGATIVO al pasar de 2 GB —
# de ahi los "-1701888 KB de 370.1 MB (-449%)" que se veian en pantalla.
#
# El tamaño del .part en disco es un int64 de verdad y es exacto.
func _gia_dl_bytes_hechos() -> int:
	var f := FileAccess.open(_gia_dl_destino + ".part", FileAccess.READ)
	if f == null:
		return 0
	var n := f.get_length()
	f.close()
	return n


func _gia_dl_tick() -> void:
	if not _gia_dl_activa():
		return
	var hechos := float(_gia_dl_bytes_hechos())
	# El total sale del catalogo (64 bits). Para el motor, que no esta en el
	# catalogo y pesa poco, vale el del servidor.
	var total := float(_gia_dl_total)
	if total <= 0.0:
		var bs := _gia_dl.get_body_size()
		if bs > 0:
			total = float(bs)
	var seg := maxf(0.001, float(Time.get_ticks_msec() - _gia_dl_t0) / 1000.0)
	var vel := hechos / seg
	if _gia_dl_bar != null and is_instance_valid(_gia_dl_bar):
		if total > 0.0:
			_gia_dl_bar.max_value = total
			_gia_dl_bar.value = minf(hechos, total)
		else:
			_gia_dl_bar.max_value = 1.0
			_gia_dl_bar.value = 0.0
	if _gia_dl_txt != null and is_instance_valid(_gia_dl_txt):
		var pct := ""
		var de := ""
		var falta := ""
		if total > 0.0:
			pct = " (%d%%)" % int(clampf(hechos * 100.0 / total, 0.0, 100.0))
			de = " de " + _gia_fmt_bytes(total)
			if vel > 1024.0:
				var seg_rest := int((total - hechos) / vel)
				if seg_rest > 0:
					falta = "  ·  faltan %d:%02d" % [seg_rest / 60, seg_rest % 60]
		_gia_dl_txt.text = "%s%s%s  ·  %s/s%s" % [_gia_fmt_bytes(hechos), de, pct, _gia_fmt_bytes(vel), falta]


func _gia_dl_completada(result: int, code: int, _h: PackedStringArray, _b: PackedByteArray) -> void:
	var que := _gia_dl_que
	var destino := _gia_dl_destino
	var parcial := destino + ".part"
	_gia_dl_limpiar()

	if result != HTTPRequest.RESULT_SUCCESS or (code != 200 and code != 0):
		DirAccess.remove_absolute(parcial)
		if code == 404 and que == "motor":
			# Caso real mientras la IA no este publicada: decirlo tal cual en
			# vez de soltar un codigo HTTP que no explica nada.
			_gia_dl_estado("El motor de IA todavia no esta publicado en el repositorio.",
				Color(1.0, 0.75, 0.3))
		else:
			_gia_dl_estado("Descarga fallida (HTTP %d, result %d)" % [code, result], Color(1.0, 0.45, 0.45))
		return

	if que == "motor":
		_gia_instalar_motor(parcial)
		return

	# Un .gguf empieza SIEMPRE por la marca "GGUF". Comprobacion barata que
	# pilla el caso tipico: que el servidor devolviera una pagina de error de
	# 2 KB en vez del modelo, y que si no reventaria al cargarlo.
	var f := FileAccess.open(parcial, FileAccess.READ)
	var marca := ""
	var tam := 0.0
	if f != null:
		marca = f.get_buffer(4).get_string_from_ascii()
		tam = float(f.get_length())
		f.close()
	if marca != "GGUF":
		DirAccess.remove_absolute(parcial)
		_gia_dl_estado("Lo descargado no es un modelo .gguf valido", Color(1.0, 0.45, 0.45))
		return

	DirAccess.remove_absolute(destino)
	if DirAccess.rename_absolute(parcial, destino) != OK:
		_gia_dl_estado("No se pudo guardar el modelo (¿espacio en disco?)", Color(1.0, 0.45, 0.45))
		return
	_gia_dl_estado("Modelo listo (%s). Ya puedes usar el chat." % _gia_fmt_bytes(tam), Color(0.5, 0.9, 0.5))
	_gia_dl_ui_visible(false)
	_rebuild_panel()


# El motor viene en un zip: se valida ENTERO antes de tocar el proyecto.
func _gia_instalar_motor(zip_path: String) -> void:
	var zip := ZIPReader.new()
	if zip.open(zip_path) != OK:
		DirAccess.remove_absolute(zip_path)
		_gia_dl_estado("El paquete del motor de IA esta corrupto", Color(1.0, 0.45, 0.45))
		return
	var archivos := zip.get_files()
	var hay_ext := false
	var hay_lib := false
	for rel in archivos:
		if rel.ends_with(".gdextension"):
			hay_ext = true
		if rel.get_extension().to_lower() in DLL_EXTENSIONS:
			hay_lib = true
	if not (hay_ext and hay_lib):
		zip.close()
		DirAccess.remove_absolute(zip_path)
		_gia_dl_estado("El paquete del motor de IA esta incompleto", Color(1.0, 0.45, 0.45))
		return

	var proj := ProjectSettings.globalize_path("res://")
	var ya_cargado := _gia_motor_disponible()
	var pendientes: Array[Dictionary] = []
	var staging := OS.get_user_data_dir() + "/godotluau_ia_staging/"
	DirAccess.make_dir_recursive_absolute(staging)

	for rel in archivos:
		if rel.ends_with("/"):
			continue
		var limpio: String = rel.replace("\\", "/")
		# Anti zip-slip, igual que el instalador del motor principal.
		if limpio.begins_with("/") or limpio.contains("..") or limpio.contains(":"):
			continue
		var datos: PackedByteArray = zip.read_file(rel)
		var dst := proj + limpio
		DirAccess.make_dir_recursive_absolute(dst.get_base_dir())
		# Una DLL YA CARGADA no se puede sobrescribir: se deja aparte y se
		# cambia al reiniciar (mismo truco que el actualizador principal).
		if ya_cargado and rel.get_extension().to_lower() in DLL_EXTENSIONS:
			var st := staging + rel.get_file()
			var sf := FileAccess.open(st, FileAccess.WRITE)
			if sf:
				sf.store_buffer(datos)
				sf.close()
				pendientes.append({"src": st, "dst": dst})
		else:
			var df := FileAccess.open(dst, FileAccess.WRITE)
			if df:
				df.store_buffer(datos)
				df.close()
	zip.close()
	DirAccess.remove_absolute(zip_path)
	_gia_dl_ui_visible(false)

	if pendientes.is_empty():
		_gia_dl_estado("Motor de IA instalado. Reinicia el editor para activarlo.", Color(0.5, 0.9, 0.5))
	else:
		_gia_dl_estado("Motor de IA actualizado: se aplicara al reiniciar el editor.", Color(1.0, 0.75, 0.3))
	_rebuild_panel()


func _gia_dl_cancelar() -> void:
	if not _gia_dl_activa():
		return
	# La UI se apaga PRIMERO. cancel_request() con use_threads espera a que el
	# hilo de descarga termine, y eso puede tardar un momento: haciendolo antes,
	# el boton parecia no responder. Ahora el clic se nota al instante y el
	# corte real ocurre justo despues.
	var parcial := _gia_dl_destino + ".part"
	_gia_dl_ui_visible(false)
	_gia_dl_estado("Cancelando…", Color(0.8, 0.8, 0.8))
	if _gia_dl_cancel != null and is_instance_valid(_gia_dl_cancel):
		_gia_dl_cancel.disabled = true
	if _gia_dl_timer != null and is_instance_valid(_gia_dl_timer):
		_gia_dl_timer.stop()

	var http := _gia_dl
	_gia_dl = null              # a partir de aqui _gia_dl_activa() ya es false
	_gia_dl_que = ""
	_gia_dl_destino = ""
	_gia_dl_total = 0
	if http != null and is_instance_valid(http):
		http.cancel_request()
		http.queue_free()
	DirAccess.remove_absolute(parcial)
	_gia_dl_estado("Descarga cancelada.", Color(0.8, 0.8, 0.8))
	_rebuild_panel()


func _gia_dl_limpiar() -> void:
	if _gia_dl_timer != null and is_instance_valid(_gia_dl_timer):
		_gia_dl_timer.stop()
	if _gia_dl_activa():
		_gia_dl.queue_free()
	_gia_dl = null
	_gia_dl_que = ""
	_gia_dl_destino = ""
	_gia_dl_total = 0


func _gia_dl_estado(msg: String, color: Color) -> void:
	if _gia_status != null and is_instance_valid(_gia_status):
		_gia_status.text = msg
		_gia_status.add_theme_color_override("font_color", color)


func _gia_dl_ui_visible(v: bool) -> void:
	for n in [_gia_dl_bar, _gia_dl_txt, _gia_dl_cancel]:
		if n != null and is_instance_valid(n):
			n.visible = v

# ==========================================================================
#  CAPA 2 - REESCRITURA VERIFICADA
#
#  Lo que la hace fiable no es el modelo: es que NADA se le enseña al usuario
#  sin pasar tres filtros, y los tres son deterministas:
#
#    1. COMPILA         - con el mismo Luau que corre el juego (GLCompat.compilar)
#    2. ARREGLA lo suyo - el hallazgo que motivo el parche ya no aparece
#    3. NO ROMPE nada   - no aparecen hallazgos NUEVOS que antes no estaban
#
#  Si un intento falla, el error del compilador se le devuelve al modelo para
#  que lo corrija (hasta GIA_C2_INTENTOS veces). Un modelo pequeño se deja
#  comas y cierra mal los bloques constantemente; asi eso se detecta en 1 ms
#  en vez de acabar en el proyecto.
#
#  Y no se reescriben archivos enteros a proposito: se piden PARCHES MINIMOS
#  (una linea) por hallazgo. Un modelo al que le pides "reescribe este archivo
#  de 400 lineas" se deja trozos por el camino sin avisar.
# ==========================================================================

const GIA_C2_INTENTOS := 3


func _gia_c2_en_marcha() -> bool:
	return _gia_c2_activo


# Punto de entrada: arregla el script del nodo seleccionado en la escena.
func _gia_c2_arreglar_seleccion() -> void:
	var sel := EditorInterface.get_selection().get_selected_nodes()
	if sel.is_empty():
		_gia_log_linea("[color=#ffbf55]Selecciona en la escena el script que quieres convertir.[/color]")
		return
	var n: Node = sel[0]
	var cls := n.get_class()
	if not (cls == "ServerScript" or cls == "LocalScript" or cls == "ModuleScript"):
		_gia_log_linea("[color=#ffbf55]Eso no es un script de GodotLuau (%s).[/color]" % cls)
		return
	var src := ""
	var rec = n.get("codigo_luau")
	if rec != null:
		var fu = rec.get("gl_fuente")
		if typeof(fu) == TYPE_STRING:
			src = fu
	if src.is_empty():
		_gia_log_linea("[color=#ffbf55]Ese script esta vacio.[/color]")
		return
	_gia_c2_empezar(src, str(n.name), "")


func _gia_c2_empezar(fuente: String, nombre: String, ruta: String) -> void:
	if _gia_c2_activo:
		return
	if not _gia_motor_disponible() or _gia_mejor_modelo() == "":
		_gia_log_linea("[color=#ff8a7a]La reescritura necesita el motor de IA y un modelo descargado.[/color]")
		return
	var c := _gia_analizador()
	if c == null:
		return
	var hallazgos: Array = c.analizar_texto(fuente, nombre)
	# Los de sintaxis no se tocan: si el script ya no compila, arreglarlo es
	# otro problema y reescribirlo a ciegas lo empeoraria.
	var utiles: Array = []
	for h in hallazgos:
		if h["tipo"] != "sintaxis" and h["severidad"] == "grave":
			utiles.append(h)

	_gia_abrir_chat()
	_gia_log_linea("\n[b]▸ Convertir \"%s\"[/b]" % nombre)
	if utiles.is_empty():
		_gia_log_linea("[color=#7fdc7f]No hace falta: no usa nada que GodotLuau no soporte.[/color]")
		return
	_gia_log_linea("[color=#888]%d incompatibilidad(es). Cada arreglo se comprueba antes de enseñarlo.[/color]" % utiles.size())

	_gia_c2_fuente  = fuente
	_gia_c2_nombre  = nombre
	_gia_c2_ruta    = ruta
	_gia_c2_cola    = utiles
	_gia_c2_i       = 0
	_gia_c2_parches = []
	_gia_c2_intento = 0
	_gia_c2_activo  = true
	if not _gia_asegurar_ia(_gia_mejor_modelo()):
		_gia_c2_activo = false
		_gia_log_linea("[color=#ff8a7a]No se pudo cargar el modelo.[/color]")
		return
	_gia_c2_pedir("")


# Pide al modelo la linea de recambio para el hallazgo actual.
# `error_previo` no vacio = el intento anterior no compilo y se le dice por que.
func _gia_c2_pedir(error_previo: String) -> void:
	if _gia_c2_i >= _gia_c2_cola.size():
		_gia_c2_terminar()
		return
	var h: Dictionary = _gia_c2_cola[_gia_c2_i]
	var c := _gia_analizador()
	if c == null:
		_gia_c2_terminar()
		return
	var lineas := _gia_c2_fuente.split("\n")
	var idx: int = int(h["linea"]) - 1
	if idx < 0 or idx >= lineas.size():
		_gia_c2_siguiente()
		return
	var original: String = lineas[idx]

	# Contexto corto: la linea y las de alrededor. Mas que eso empeora el
	# resultado de un modelo pequeño y ademas tarda mas.
	var desde: int = maxi(0, idx - 4)
	var hasta: int = mini(lineas.size() - 1, idx + 4)
	var alrededor := ""
	for i in range(desde, hasta + 1):
		alrededor += ("%s%s\n" % ["  >> " if i == idx else "     ", lineas[i]])

	# EL ORDEN DE LAS REGLAS IMPORTA, y mucho. Con "si no hay equivalente exacto,
	# comenta la linea" ARRIBA, el modelo comentaba TODO — hasta cuando la
	# solucion estaba en la lista de nombres soportados que le damos. Poniendo
	# "usa uno de la lista" primero y comentar como ULTIMO recurso, los mismos
	# casos salen bien: .unit -> .Unit, :inverse() -> :Inverse(),
	# :vectorToWorldSpace(dir) -> :VectorToWorldSpace(dir).
	var sistema := """Eres un experto en Luau que adapta scripts de Roblox para que funcionen en GodotLuau.
Te doy UNA linea que usa algo que GodotLuau no implementa. Devuelve SOLO la linea de recambio.

REGLAS:
- Devuelve UNICAMENTE codigo Luau, sin explicaciones, sin markdown, sin ```.
- UNA sola linea, la que sustituye a la marcada con >>.
- Conserva la indentacion original.
- Si te doy una lista de nombres soportados, USA UNO DE ESOS: casi siempre es
  el mismo nombre escrito con otras mayusculas.
- Conserva TODOS los argumentos de la llamada original.
- SOLO si ningun nombre de la lista sirve, comenta la linea con -- y una nota
  corta. Es preferible renunciar a cambiar el comportamiento del juego.
"""
	# Las sugerencias son LO QUE HACE QUE ACIERTE: muchos hallazgos son alias
	# en minuscula que Roblox arrastra (.unit, :inverse(), :vectorToWorldSpace())
	# y cuya version PascalCase SI existe aqui. Sin decirselo, ni el modelo de
	# 7B daba con ello: se dedicaba a comentar las lineas.
	var sug: Array = c.sugerir(str(h["simbolo"]))
	var pista := ""
	if not sug.is_empty():
		pista = "GodotLuau SI soporta estos nombres, casi seguro es uno de ellos: %s" % ", ".join(sug)
	var mensaje := """Contexto:
%s
Problema: %s
Linea a sustituir:
%s""" % [alrededor, str(h["mensaje"]) + " " + pista, original]
	if error_previo != "":
		mensaje += "\n\nTu intento anterior NO compila. Error del compilador: %s\nCorrigelo." % error_previo

	# Temperatura baja (nivel 0) para transcribir codigo: aqui no se quiere
	# creatividad, se quiere exactitud.
	_gia_ia.preguntar(sistema, mensaje, 0)


# Llega cuando el modelo termina de escribir el parche.
func _gia_c2_respuesta(texto: String) -> void:
	var h: Dictionary = _gia_c2_cola[_gia_c2_i]
	var lineas := _gia_c2_fuente.split("\n")
	var idx: int = int(h["linea"]) - 1
	var propuesta := _gia_c2_limpiar(texto)
	if propuesta.strip_edges().is_empty():
		_gia_c2_fallo("el modelo no devolvio nada")
		return

	# --- Filtro 1: ¿COMPILA? ---
	var candidato := lineas.duplicate()
	candidato[idx] = propuesta
	var nuevo_src := "\n".join(candidato)
	var c := _gia_analizador()
	var comp: Dictionary = c.compilar(nuevo_src)
	if not bool(comp["ok"]):
		_gia_c2_intento += 1
		if _gia_c2_intento < GIA_C2_INTENTOS:
			# Se le devuelve el error del compilador y que lo intente otra vez.
			_gia_c2_pedir(str(comp["mensaje"]))
			return
		_gia_c2_fallo("no compila tras %d intentos: %s" % [GIA_C2_INTENTOS, str(comp["mensaje"])])
		return

	# --- Filtro 2 y 3: ¿arregla lo suyo y no rompe nada? ---
	var antes: Array = c.analizar_texto(_gia_c2_fuente, _gia_c2_nombre)
	var despues: Array = c.analizar_texto(nuevo_src, _gia_c2_nombre)
	var clave_antes := {}
	for a in antes:
		clave_antes["%s|%s" % [a["tipo"], a["simbolo"]]] = true
	var sigue := false
	var nuevos: Array = []
	for d in despues:
		var k := "%s|%s" % [d["tipo"], d["simbolo"]]
		if k == "%s|%s" % [h["tipo"], h["simbolo"]]:
			sigue = true
		if not clave_antes.has(k):
			nuevos.append(k)
	if sigue:
		_gia_c2_fallo("el cambio no resuelve \"%s\"" % str(h["simbolo"]))
		return
	if not nuevos.is_empty():
		_gia_c2_fallo("introduce problemas nuevos: %s" % ", ".join(nuevos))
		return

	# --- Filtro 4: ¿conserva la FORMA de la llamada? ---
	# Los tres filtros de arriba dicen que compila y que el simbolo malo ya no
	# esta, pero NO que haga lo mismo. Medido con un modelo de 1,5B:
	# `CFrame:vectorToWorldSpace(dir)` se convirtio en `CFrame:worldToLocal()`
	# — compila, el simbolo desaparece... y se dejo el argumento por el camino.
	# Si la linea original pasaba argumentos, la nueva tiene que pasarlos.
	if _gia_c2_pierde_argumentos(lineas[idx], propuesta):
		_gia_c2_fallo("pierde argumentos: la llamada nueva no recibe lo que recibia la vieja")
		return

	# Verificado por los cuatro filtros.
	_gia_c2_parches.append({
		"linea": idx + 1, "simbolo": str(h["simbolo"]),
		"antes": lineas[idx], "despues": propuesta,
	})
	_gia_c2_fuente = nuevo_src   # se encadena: el siguiente parche ve este ya aplicado
	_gia_log_linea("  [color=#7fdc7f]✔[/color] linea %d (%s) — compila y no rompe nada" % [idx + 1, str(h["simbolo"])])
	_gia_c2_siguiente()


func _gia_c2_fallo(motivo: String) -> void:
	var h: Dictionary = _gia_c2_cola[_gia_c2_i]
	_gia_log_linea("  [color=#ff8a7a]✘[/color] linea %s (%s) — descartado: %s"
		% [str(h["linea"]), str(h["simbolo"]), motivo])
	_gia_c2_siguiente()


func _gia_c2_siguiente() -> void:
	_gia_c2_i += 1
	_gia_c2_intento = 0
	if _gia_c2_i >= _gia_c2_cola.size():
		_gia_c2_terminar()
	else:
		_gia_c2_pedir("")


func _gia_c2_terminar() -> void:
	_gia_c2_activo = false
	if _gia_c2_parches.is_empty():
		_gia_log_linea("[color=#ffbf55]No hubo ningun cambio que superase la verificacion.[/color]")
		return
	_gia_log_linea("\n[b][color=#7fdc7f]%d cambio(s) verificados[/color][/b]  [color=#888](compilan, arreglan lo suyo y no rompen nada)[/color]"
		% _gia_c2_parches.size())
	for pch in _gia_c2_parches:
		_gia_log_linea("[color=#888]  linea %d[/color]" % int(pch["linea"]))
		_gia_log_linea("    [color=#ff8a7a]- %s[/color]" % str(pch["antes"]).strip_edges())
		_gia_log_linea("    [color=#7fdc7f]+ %s[/color]" % str(pch["despues"]).strip_edges())
	_gia_log_linea("[color=#888]El codigo resultante esta verificado, pero repasalo antes de aplicarlo.[/color]")


# Un modelo pequeño envuelve el codigo en ``` casi siempre, y a veces añade una
# frase antes. Se queda solo con la primera linea de codigo de verdad.
func _gia_c2_limpiar(texto: String) -> String:
	var t := texto.strip_edges()
	if t.contains("```"):
		var trozos := t.split("```")
		for tr in trozos:
			var x := str(tr).strip_edges()
			if x.is_empty():
				continue
			if x.begins_with("lua") or x.begins_with("luau"):
				x = x.substr(4).strip_edges() if x.begins_with("luau") else x.substr(3).strip_edges()
			if not x.is_empty():
				t = x
				break
	# Solo la primera linea no vacia: se pidio UNA linea.
	for ln in t.split("\n"):
		if not str(ln).strip_edges().is_empty():
			return str(ln)
	return ""


# ¿La linea original llamaba con argumentos y la nueva ya no? Es la señal mas
# clara de que el modelo cambio la funcion por otra que no hace lo mismo.
func _gia_c2_pierde_argumentos(antes: String, despues: String) -> bool:
	# Si la propuesta esta comentada, es una renuncia honesta: se acepta.
	if despues.strip_edges().begins_with("--"):
		return false
	var re := RegEx.new()
	re.compile("[(]([^()]*)[)]")
	var con_args := func(t: String) -> int:
		var n := 0
		for m in re.search_all(t):
			if not m.get_string(1).strip_edges().is_empty():
				n += 1
		return n
	return con_args.call(antes) > 0 and con_args.call(despues) == 0


func _gia_log_linea(txt: String) -> void:
	if _gia_log != null and is_instance_valid(_gia_log):
		_gia_log.append_text(txt + "\n")
