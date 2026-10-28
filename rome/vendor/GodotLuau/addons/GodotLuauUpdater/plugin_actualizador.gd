@tool
extends "res://addons/GodotLuauUpdater/plugin_editor.gd"

# Plugin de GodotLuau, parte 3 de 5: el actualizador. Comprobar version,
# descargar, verificar el SHA-256, aplicar, respaldo y vuelta atras,
# historial de versiones y reinicio del editor.

# ── Version status (inline) ───────────────────────────────────────────────────

func _set_ver_status(msg: String, btn_text: String, color: Color, action: String = "") -> void:
	if not (_ver_label and is_instance_valid(_ver_label)): return
	_ver_label.text = msg
	_ver_label.add_theme_color_override("font_color", color)
	_bar_action = action
	if not (_ver_btn and is_instance_valid(_ver_btn)): return
	if btn_text.is_empty():
		_ver_btn.visible = false
	else:
		_ver_btn.text    = btn_text
		_ver_btn.visible = true

func _reset_ver_idle() -> void:
	_set_ver_status(_get_local_version(), _t("btn_check_ver"), _col_dim)
	if _reinstall_btn and is_instance_valid(_reinstall_btn):
		_reinstall_btn.disabled = false

func _on_action_pressed() -> void:
	match _bar_action:
		"restart":  _do_editor_restart()
		"apply":    _apply_windows_update_legacy()
		"download": if not _downloading: _start_download()
		_:          _check_for_update()

# ── Version check ─────────────────────────────────────────────────────────────

func _check_for_update() -> void:
	if _http_version and is_instance_valid(_http_version): return
	# Buscar actualizacion mira SIEMPRE la ultima publicada, aunque antes se
	# hubiera instalado una version concreta del historial.
	_zip_url_override = ""
	_http_version = HTTPRequest.new()
	_http_version.timeout = 10.0
	add_child(_http_version)
	_http_version.request_completed.connect(_on_version_received)
	_set_ver_status(_t("bar_checking"), "", Color(0.5, 0.8, 1.0))
	if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = true
	if _http_version.request(VERSION_URL) != OK:
		_http_version.queue_free(); _http_version = null
		_reset_ver_idle()

func _on_version_received(result: int, code: int, _hdrs: PackedStringArray, body: PackedByteArray) -> void:
	if _http_version and is_instance_valid(_http_version):
		_http_version.queue_free(); _http_version = null
	if result != HTTPRequest.RESULT_SUCCESS or code != 200:
		_reset_ver_idle(); return
	_remote_version = body.get_string_from_utf8().strip_edges()
	var local := _get_local_version()
	if _version_to_num(_remote_version) <= _version_to_num(local):
		_stop_outdated_warning()
		_set_ver_status(_t("bar_uptodate") % local, "", Color(0.4, 0.9, 0.5))
		if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = false
		await get_tree().create_timer(4.0).timeout
		if _ver_label and is_instance_valid(_ver_label): _reset_ver_idle()
		return
	_setup_outdated_warning(local, _remote_version)
	if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = false
	_set_ver_status(
		_t("bar_newver") % [local, _remote_version],
		_t("bar_btn_update"),
		Color(1.0, 0.9, 0.3),
		"download"
	)

func _version_to_num(v: String) -> int:
	# Rellena SIEMPRE a 4 segmentos para que versiones con distinta cantidad
	# de numeros se comparen bien: "v1.12" -> [1,12,0,0] y "v1.11.6.1" ->
	# [1,11,6,1]. (El bug clasico: 1.12 con 2 segmentos daba 1012 y perdia
	# contra 1.11.6.1 con 4 segmentos = 1011006001 -> "al dia" eterno.)
	#
	# Sufijo de TEXTO tras un "-" (ej. "v1.14.4 - Rework"): cuenta como una
	# sub-version MAYOR que la misma sin sufijo, SIN subir el numero (como poner
	# .1 despues del .4). Si el sufijo trae un numero al final se usa ese (para
	# "- Rework 2" > "- Rework"); si no, 1 (solo por existir).
	v = v.strip_edges().trim_prefix("v").strip_edges()
	var suffix_val := 0
	var dash := v.find("-")
	if dash >= 0:
		var suffix := v.substr(dash + 1).strip_edges()
		v = v.substr(0, dash).strip_edges()
		var num := ""
		for i in range(suffix.length() - 1, -1, -1):
			if suffix[i] >= "0" and suffix[i] <= "9":
				num = suffix[i] + num
			elif num != "":
				break
		suffix_val = int(num) if num != "" else 1
	var parts := v.split(".")
	var n := 0
	for i in range(4):
		n = n * 1000 + (parts[i].to_int() if i < parts.size() else 0)
	n = n * 1000 + suffix_val
	return n

# ── Download & reinstall ──────────────────────────────────────────────────────

func _start_reinstall() -> void:
	if _downloading: return
	if _remote_version.is_empty(): _remote_version = _get_local_version()
	_set_ver_status(_t("bar_reinstalling") % _get_local_version(), "", Color(0.4, 0.8, 1.0))
	if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = true
	_start_download()

func _start_download() -> void:
	_downloading = true
	if not (_ver_label and is_instance_valid(_ver_label) and _ver_label.text.begins_with("↺")):
		_set_ver_status(_t("bar_downloading") % _remote_version, "", Color(0.4, 0.8, 1.0))
	# Borra cualquier temporal a medias de un intento anterior: si quedó abierto
	# o bloqueado, HTTPRequest fallaría al abrir el archivo de descarga.
	DirAccess.remove_absolute(OS.get_user_data_dir() + "/godotluau_update.zip")
	_http_download = HTTPRequest.new()
	_http_download.use_threads   = true
	_http_download.download_file = OS.get_user_data_dir() + "/godotluau_update.zip"
	add_child(_http_download)
	_http_download.request_completed.connect(_on_download_completed)
	# Con override se instala una version concreta del historial; sin el, la
	# ultima publicada.
	var url := _zip_url_override if not _zip_url_override.is_empty() else ZIP_URL
	if _http_download.request(url) != OK:
		_set_ver_status(_t("bar_dl_err"), _t("bar_retry"), Color(1.0, 0.4, 0.4), "download")
		_downloading = false
		if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = false
		_http_download.queue_free(); _http_download = null

func _on_download_completed(result: int, code: int, _hdrs: PackedStringArray, _body: PackedByteArray) -> void:
	if _http_download and is_instance_valid(_http_download):
		_http_download.queue_free(); _http_download = null
	_downloading = false
	if result != HTTPRequest.RESULT_SUCCESS or (code != 200 and code != 0):
		# 'code' es el HTTP (0 cuando ni hubo respuesta); 'result' es el motivo real
		# de transporte (2=no conecta, 3=no resuelve DNS, 10/11=no pudo escribir el
		# archivo, etc.) — se muestra para poder diagnosticar.
		var msg: String = (_t("bar_dl_failed") % code) + (" [result=%d]" % result)
		_set_ver_status(msg, _t("bar_retry"), Color(1.0, 0.4, 0.4), "download")
		if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = false
		return
	_start_hash_verify()

# ── Verificación de integridad (SHA-256) ─────────────────────────────────────

func _start_hash_verify() -> void:
	if _http_hash and is_instance_valid(_http_hash): return
	_set_ver_status(_t("bar_verifying"), "", Color(0.4, 0.8, 1.0))
	_http_hash = HTTPRequest.new()
	_http_hash.timeout = 15.0
	add_child(_http_hash)
	_http_hash.request_completed.connect(_on_hash_received)
	# El .sha256 vive junto al .zip: al instalar una version del historial hay
	# que pedir EL SUYO, o el checksum de main no cuadraria y abortaria siempre.
	var hurl := (_zip_url_override + ".sha256") if not _zip_url_override.is_empty() else HASH_URL
	if _http_hash.request(hurl) != OK:
		# Sin verificación de integridad NO instalamos (evita aplicar un ZIP
		# manipulado o corrupto). El repo oficial siempre publica el .sha256.
		_http_hash.queue_free(); _http_hash = null
		DirAccess.remove_absolute(OS.get_user_data_dir() + "/godotluau_update.zip")
		_set_ver_status(_t("bar_hash_err"), _t("bar_retry"), Color(1.0, 0.4, 0.4), "download")
		if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = false

func _on_hash_received(result: int, code: int, _hdrs: PackedStringArray, body: PackedByteArray) -> void:
	if _http_hash and is_instance_valid(_http_hash):
		_http_hash.queue_free(); _http_hash = null
	var zip_path := OS.get_user_data_dir() + "/godotluau_update.zip"
	if result != HTTPRequest.RESULT_SUCCESS or code != 200:
		# No se pudo obtener el checksum → abortar (no instalar sin verificar).
		DirAccess.remove_absolute(zip_path)
		_set_ver_status(_t("bar_hash_err"), _t("bar_retry"), Color(1.0, 0.4, 0.4), "download")
		if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = false
		return
	var expected := body.get_string_from_utf8().strip_edges().to_lower()
	if expected.contains(" "): expected = expected.split(" ")[0]
	var actual := FileAccess.get_sha256(zip_path).to_lower()
	if expected != actual:
		DirAccess.remove_absolute(zip_path)
		_set_ver_status(_t("bar_hash_err"), _t("bar_retry"), Color(1.0, 0.4, 0.4), "download")
		if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = false
		return
	_set_ver_status(_t("bar_extracting"), "", Color(0.4, 0.8, 1.0))
	_apply_update.call_deferred()

# ── Apply update ──────────────────────────────────────────────────────────────

var _windows_staged_dlls : Array[Dictionary] = []

func _apply_update() -> void:
	var zip_path  := OS.get_user_data_dir() + "/godotluau_update.zip"
	var proj_path := ProjectSettings.globalize_path("res://")

	# ── 1. Validar el ZIP COMPLETO antes de tocar el proyecto ──────────────────
	var zip := ZIPReader.new()
	if zip.open(zip_path) != OK:
		_fail_apply(); return
	var zfiles := zip.get_files()
	var has_ext := false
	var has_dll := false
	for rel in zfiles:
		if rel.ends_with("godot_luau.gdextension"): has_ext = true
		if rel.get_extension().to_lower() in DLL_EXTENSIONS: has_dll = true
	if not (has_ext and has_dll):
		# ZIP incompleto o corrupto → abortar SIN haber tocado nada.
		zip.close(); _fail_apply(); return

	# ── 2. Respaldar la version actual (solo lo que se va a sobrescribir) ───────
	var old_ver := _get_local_version()
	_backup_current(zfiles, old_ver)

	# ── 3. Extraer: archivos normales directo, librerias a staging. Si algo no
	#       se puede escribir, restauramos del respaldo y abortamos. ────────────
	var staged  : Array[Dictionary] = []
	var staging := OS.get_user_data_dir() + "/godotluau_staging/"
	DirAccess.make_dir_recursive_absolute(staging)
	var is_win  : bool = (OS.get_name() == "Windows")
	var write_ok := true

	for rel in zfiles:
		if rel.ends_with("/"): continue
		# Anti zip-slip: rechazar rutas absolutas o con '..'
		var rel_clean : String = rel.replace("\\", "/")
		if rel_clean.begins_with("/") or rel_clean.contains("..") or rel_clean.contains(":"):
			push_warning("[GodotLuau] Skipped suspicious ZIP entry: " + rel)
			continue
		var data    : PackedByteArray = zip.read_file(rel)
		var dst     : String          = proj_path + rel_clean
		var dst_dir : String          = dst.get_base_dir()
		if not DirAccess.dir_exists_absolute(dst_dir):
			DirAccess.make_dir_recursive_absolute(dst_dir)
		# Las librerias nativas estan CARGADAS mientras el editor corre: no se
		# sobrescriben en el sitio, se preparan aparte y se cambian con rename.
		if rel.get_extension().to_lower() in DLL_EXTENSIONS:
			var stage := staging + rel.get_file()
			var sf := FileAccess.open(stage, FileAccess.WRITE)
			if sf: sf.store_buffer(data); staged.append({"src": stage, "dst": dst})
			else: write_ok = false
		else:
			var f := FileAccess.open(dst, FileAccess.WRITE)
			if f: f.store_buffer(data)
			else: write_ok = false
	zip.close()

	if not write_ok:
		# No dejar el proyecto a medias: restaurar lo respaldado.
		_restore_from_backup()
		_fail_apply(); return

	DirAccess.remove_absolute(zip_path)

	# AQUI, no en _migrate_old_layout: el zip acaba de dejar el .gdextension
	# nuevo dentro de GodotLuau/ y el viejo sigue en la raiz. Si se llega al
	# reinicio con los dos puestos, Godot carga la misma libreria dos veces y
	# registra las clases repetidas ANTES de que corra una sola linea de este
	# plugin, asi que limpiarlo mas tarde llegaria tarde.
	_borrar_restos_raiz()

	# ── 4. Cambiar las DLL (la anterior ya quedo guardada en el respaldo) ──────
	var swapped := true
	if not staged.is_empty():
		_windows_staged_dlls = staged
		swapped = _try_rename_and_replace()

	# ── 5. Escribir la version SOLO ahora que todo salio bien ──────────────────
	var vf := FileAccess.open(VERSION_FILE, FileAccess.WRITE)
	if vf: vf.store_string(_remote_version + "\n")
	if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = false
	# La siguiente descarga vuelve a ser "la ultima version" salvo que se elija
	# otra del historial otra vez.
	_zip_url_override = ""
	_refresh_rollback_btn()

	if staged.is_empty():
		_set_ver_status(_t("bar_ok_no_restart") % _remote_version, "", Color(0.3, 0.9, 0.5))
		_refresh_stats(); return
	if swapped:
		_set_ver_status(_t("bar_ok_restart"), _t("bar_restart"), Color(0.4, 0.9, 1.0), "restart")
		_auto_restart_countdown()
	elif is_win:
		# La DLL estaba bloqueada: aplicar tras cerrar mediante script externo.
		_set_ver_status(_t("bar_apply_close"), "Apply & Close", Color(1.0, 0.7, 0.2), "apply")
	else:
		_set_ver_status(_t("bar_ok_restart"), _t("bar_restart"), Color(0.4, 0.9, 1.0), "restart")

func _fail_apply() -> void:
	DirAccess.remove_absolute(OS.get_user_data_dir() + "/godotluau_update.zip")
	_set_ver_status(_t("bar_dl_failed") % 0, _t("bar_retry"), Color(1.0, 0.4, 0.4), "download")
	if _reinstall_btn and is_instance_valid(_reinstall_btn): _reinstall_btn.disabled = false

# ── Respaldo y vuelta atras (rollback) ────────────────────────────────────────

# Guarda en res://GodotLuau/.luau_backup/files/ una copia de cada archivo del proyecto que
# la actualizacion va a sobrescribir, mas la version anterior. Permite deshacer.
func _backup_current(zfiles: PackedStringArray, old_ver: String) -> void:
	var proj := ProjectSettings.globalize_path("res://")
	var bdir := ProjectSettings.globalize_path(BACKUP_DIR) + "/"
	_rmdir_recursive(bdir)
	DirAccess.make_dir_recursive_absolute(bdir + "files")
	var gi := FileAccess.open(bdir + ".gdignore", FileAccess.WRITE)  # que Godot no lo importe
	if gi: gi.close()
	for rel in zfiles:
		if rel.ends_with("/"): continue
		var rel_clean : String = rel.replace("\\", "/")
		if rel_clean.begins_with("/") or rel_clean.contains("..") or rel_clean.contains(":"): continue
		var cur := proj + rel_clean
		if not FileAccess.file_exists(cur): continue
		var bpath := bdir + "files/" + rel_clean
		DirAccess.make_dir_recursive_absolute(bpath.get_base_dir())
		var bf := FileAccess.open(bpath, FileAccess.WRITE)
		if bf: bf.store_buffer(FileAccess.get_file_as_bytes(cur))
	var inf := FileAccess.open(bdir + "version.txt", FileAccess.WRITE)
	if inf: inf.store_string(old_ver + "\n")

func _can_rollback() -> bool:
	return FileAccess.file_exists(ProjectSettings.globalize_path(BACKUP_DIR) + "/version.txt")

func _backup_version() -> String:
	var p := ProjectSettings.globalize_path(BACKUP_DIR) + "/version.txt"
	if not FileAccess.file_exists(p): return ""
	var f := FileAccess.open(p, FileAccess.READ)
	return f.get_as_text().strip_edges() if f else ""

# Restaura los archivos respaldados a su sitio (lo usa tanto el rollback manual
# como el aborto de una extraccion fallida). Las DLL van por staging+rename.
func _restore_from_backup() -> bool:
	var proj := ProjectSettings.globalize_path("res://")
	var base := ProjectSettings.globalize_path(BACKUP_DIR) + "/files"
	if not DirAccess.dir_exists_absolute(base): return false
	var staged  : Array[Dictionary] = []
	var staging := OS.get_user_data_dir() + "/godotluau_rollback/"
	DirAccess.make_dir_recursive_absolute(staging)
	_collect_backup(base, "", proj, staging, staged)
	if not staged.is_empty():
		_windows_staged_dlls = staged
		_try_rename_and_replace()
	return true

func _collect_backup(base: String, rel: String, proj: String, staging: String, staged: Array) -> void:
	var dir_path := base + ("/" + rel if rel != "" else "")
	var d := DirAccess.open(dir_path)
	if not d: return
	d.list_dir_begin()
	var entry := d.get_next()
	while entry != "":
		var sub := (rel + "/" + entry) if rel != "" else entry
		if d.current_is_dir():
			_collect_backup(base, sub, proj, staging, staged)
		else:
			var src := base + "/" + sub
			var dst := proj + sub
			if src.get_extension().to_lower() in DLL_EXTENSIONS:
				var stage := staging + entry
				var sf := FileAccess.open(stage, FileAccess.WRITE)
				if sf: sf.store_buffer(FileAccess.get_file_as_bytes(src)); staged.append({"src": stage, "dst": dst})
			else:
				DirAccess.make_dir_recursive_absolute(dst.get_base_dir())
				var f := FileAccess.open(dst, FileAccess.WRITE)
				if f: f.store_buffer(FileAccess.get_file_as_bytes(src))
		entry = d.get_next()
	d.list_dir_end()

func _rmdir_recursive(path: String) -> void:
	var clean := path.rstrip("/")
	if not DirAccess.dir_exists_absolute(clean): return
	var d := DirAccess.open(clean)
	if not d: return
	d.include_hidden = true   # el .gdignore del respaldo
	d.list_dir_begin()
	var entry := d.get_next()
	while entry != "":
		var full := clean + "/" + entry
		if d.current_is_dir(): _rmdir_recursive(full)
		else: DirAccess.remove_absolute(full)
		entry = d.get_next()
	d.list_dir_end()
	DirAccess.remove_absolute(clean)

# ── Historial de versiones (instalar cualquier version publicada) ────────────
# Cada tag del repo lleva su GodotLuau.zip commiteado, asi que no hace falta que
# exista un Release ni tener un respaldo local: se puede saltar a cualquiera.

func _on_versions_pressed() -> void:
	if _downloading: return
	if not _tag_list.is_empty():
		_show_versions_menu()
		return
	_set_ver_status(_t("bar_versions_loading"), "", Color(0.4, 0.8, 1.0))
	_ver_raw.clear()
	_ver_page = 1
	_fetch_version_page()

func _fetch_version_page() -> void:
	if _http_tags and is_instance_valid(_http_tags): _http_tags.queue_free()
	_http_tags = HTTPRequest.new()
	add_child(_http_tags)
	_http_tags.request_completed.connect(_on_commits_received)
	# La API de GitHub exige User-Agent o responde 403.
	var url := "%s&page=%d" % [COMMITS_URL, _ver_page]
	if _http_tags.request(url, ["User-Agent: GodotLuauUpdater"]) != OK:
		_set_ver_status(_t("bar_versions_err"), "", Color(1.0, 0.4, 0.4))
		_http_tags.queue_free(); _http_tags = null

func _on_commits_received(result: int, code: int, _hdrs: PackedStringArray, body: PackedByteArray) -> void:
	if _http_tags and is_instance_valid(_http_tags):
		_http_tags.queue_free(); _http_tags = null
	if result != HTTPRequest.RESULT_SUCCESS or code != 200:
		if _ver_raw.is_empty():
			_set_ver_status(_t("bar_versions_err"), "", Color(1.0, 0.4, 0.4))
			return
		_build_version_list(); return          # nos quedamos con lo ya reunido
	var data = JSON.parse_string(body.get_string_from_utf8())
	if typeof(data) != TYPE_ARRAY:
		_set_ver_status(_t("bar_versions_err"), "", Color(1.0, 0.4, 0.4))
		return

	for c in data:
		var sha := str(c.get("sha", ""))
		var cm = c.get("commit", {})
		var msg := str(cm.get("message", "")) if typeof(cm) == TYPE_DICTIONARY else ""
		if sha.is_empty(): continue
		_ver_raw.append({ "sha": sha, "msg": msg.split("\n")[0].strip_edges() })

	# Los commits vienen de 100 en 100 y ordenados del mas nuevo al mas viejo.
	if data.size() >= 100 and _ver_page < VER_MAX_PAGES:
		_ver_page += 1
		_fetch_version_page()
		return
	_build_version_list()

# Convierte los commits en la lista del menu:
#  - solo entradas cuya version empiece por "v" seguida de numero
#  - sin repetidos: si dos comparten el mismo NUMERO (aunque una lleve texto
#    extra, como "v1.14.4" y "v1.14.4 - Rework"), se queda la mas reciente
#  - ordenadas de la mas nueva a la mas antigua
func _build_version_list() -> void:
	# Exige al menos vX.Y: asi se descarta la plantilla "v1.X.X - descripcion"
	# del repo, que con un solo segmento colaba como si fuera la version "v1".
	var re := RegEx.new()
	re.compile("v[0-9]+\\.[0-9]+(\\.[0-9]+)*")
	var seen := {}
	_tag_list.clear()
	for c in _ver_raw:
		var msg : String = c["msg"]
		var m := re.search(msg)
		if m == null: continue                       # sin "vX.Y" no es una version
		var num := m.get_string()                    # p.ej. "v1.14.4"
		var key := num.trim_prefix("v")              # dedupe por el NUMERO pelado
		if seen.has(key): continue                   # ya guardamos una mas reciente
		seen[key] = true
		# 'num' se guarda aparte para ordenar: hacerlo por la etiqueta completa
		# falla cuando el separador no es "-" ("v1.14.16: texto" -> 1.14.0).
		# 'ver' es la etiqueta CORTA del menu (el mensaje entero lo llenaba toda
		# la pantalla): numero + el nombre corto tras " - ", cortado en ":".
		_tag_list.append({ "tag": c["sha"], "ver": _short_label(msg, num), "num": num })

	if _tag_list.is_empty():
		_set_ver_status(_t("bar_versions_err"), "", Color(1.0, 0.4, 0.4))
		return
	_tag_list.sort_custom(func(a, b): return _version_to_num(a["num"]) > _version_to_num(b["num"]))
	_set_ver_status(_get_local_version(), "", _col_text)
	_show_versions_menu()

# Etiqueta CORTA para el menu (el mensaje de commit entero ocupaba toda la
# pantalla con 78 versiones). Toma el numero y, si hay un nombre de release tras
# " - ", lo anade recortado (cortando en ":", que empieza la descripcion larga).
#   "v1.14.9.2: sync editor-placed..."          -> "v1.14.9.2"
#   "v1.14.4 - Rework: coordinator/matchmaker"  -> "v1.14.4 - Rework"
#   "v1.16.1 - Bug Fixes"                        -> "v1.16.1 - Bug Fixes"
func _short_label(msg: String, num: String) -> String:
	var rest := msg.substr(num.length())
	var dash := rest.find(" - ")
	if dash < 0: return num
	var name := rest.substr(dash + 3)
	var colon := name.find(":")
	if colon >= 0: name = name.substr(0, colon)
	name = name.strip_edges()
	# Solo se anade si es un NOMBRE de release corto ("Bug Fixes", "Rework"). Si
	# es largo es una descripcion de changelog: se deja solo el numero, para que
	# la lista quede limpia y uniforme como los tags de antes.
	if name.is_empty() or name.length() > 20: return num
	return num + " - " + name

# "GodotLuau-v1.14.4-Rework" -> "v1.14.4 - Rework"; el nombre exacto se confirma
# luego leyendo el archivo Version de ese tag.
func _pretty_tag(tag: String) -> String:
	var s := tag.trim_prefix("GodotLuau-")
	if s.is_empty() or s == "GodotLuau": return tag
	var i := s.find("-")
	if i > 0: s = s.substr(0, i) + " - " + s.substr(i + 1)
	return s

func _show_versions_menu() -> void:
	if not (_versions_menu and is_instance_valid(_versions_menu)): return
	_versions_menu.clear()
	# Se compara por el NUMERO, no por la etiqueta: la instalada trae el texto
	# completo ("v1.16.1 - Bug Fixes") y la del menu puede traer otro sufijo.
	var re := RegEx.new()
	re.compile("v[0-9]+\\.[0-9]+(\\.[0-9]+)*")
	var lm := re.search(_get_local_version())
	var local_num := lm.get_string() if lm else ""
	for i in range(_tag_list.size()):
		var v : String = _tag_list[i]["ver"]
		var same : bool = not local_num.is_empty() and str(_tag_list[i]["num"]) == local_num
		_versions_menu.add_item(_t("menu_versions_current") % v if same else v, i)
		if same: _versions_menu.set_item_disabled(_versions_menu.get_item_count() - 1, true)
	# Altura acotada: con 78 versiones el menu llenaba la pantalla entera. Con
	# max_size el PopupMenu se limita y muestra barra de desplazamiento.
	_versions_menu.max_size = Vector2i(300, 460)
	var pos := _versions_btn.get_screen_position() + Vector2(0, _versions_btn.size.y)
	_versions_menu.popup(Rect2i(Vector2i(pos), Vector2i(240, 0)))

func _on_version_chosen(id: int) -> void:
	if _downloading: return
	if id < 0 or id >= _tag_list.size(): return
	_pending_tag = _tag_list[id]["tag"]
	# Antes de bajar 14 MB pedimos el archivo Version del tag: asi res://Version
	# queda con el nombre EXACTO de esa release y no uno derivado del tag.
	if _http_tagver and is_instance_valid(_http_tagver): _http_tagver.queue_free()
	_http_tagver = HTTPRequest.new()
	add_child(_http_tagver)
	_http_tagver.request_completed.connect(_on_tag_version_received)
	if _http_tagver.request("%s/%s/Version" % [RAW_BASE, _pending_tag]) != OK:
		_install_pending_tag(_pretty_tag(_pending_tag))

func _on_tag_version_received(result: int, code: int, _hdrs: PackedStringArray, body: PackedByteArray) -> void:
	if _http_tagver and is_instance_valid(_http_tagver):
		_http_tagver.queue_free(); _http_tagver = null
	var ver := _pretty_tag(_pending_tag)
	if result == HTTPRequest.RESULT_SUCCESS and code == 200:
		var t := body.get_string_from_utf8().strip_edges()
		if not t.is_empty(): ver = t
	_install_pending_tag(ver)

func _install_pending_tag(ver: String) -> void:
	if _pending_tag.is_empty(): return
	_remote_version   = ver
	_zip_url_override = "%s/%s/GodotLuau.zip" % [RAW_BASE, _pending_tag]
	_pending_tag = ""
	_set_ver_status(_t("bar_installing_ver") % ver, "", Color(0.4, 0.8, 1.0))
	_start_download()

func _on_rollback_pressed() -> void:
	if not _can_rollback(): return
	var ver := _backup_version()
	_restore_from_backup()
	var vf := FileAccess.open(VERSION_FILE, FileAccess.WRITE)
	if vf: vf.store_string(ver + "\n")
	_set_ver_status(_t("bar_rollback_done") % ver, _t("bar_restart"), Color(0.4, 0.9, 1.0), "restart")
	_auto_restart_countdown()

func _refresh_rollback_btn() -> void:
	if not (_rollback_btn and is_instance_valid(_rollback_btn)): return
	if _can_rollback():
		_rollback_btn.visible = true
		_rollback_btn.text = _t("btn_rollback") % _backup_version()
	else:
		_rollback_btn.visible = false

func _auto_restart_countdown() -> void:
	for i in range(5, 0, -1):
		if _bar_action != "restart": return  # el usuario hizo otra cosa
		_set_ver_status(_t("bar_restart_in") % i, _t("bar_restart"), Color(0.4, 0.9, 1.0), "restart")
		await get_tree().create_timer(1.0).timeout
	if _bar_action == "restart":
		_do_editor_restart()

func _try_rename_and_replace() -> bool:
	for item in _windows_staged_dlls:
		var dst : String = item["dst"]
		var src : String = item["src"]
		if not FileAccess.file_exists(dst):
			var data := FileAccess.get_file_as_bytes(src)
			if data.is_empty(): return false
			var f := FileAccess.open(dst, FileAccess.WRITE)
			if not f: return false
			f.store_buffer(data); continue
		var old := dst + ".old"
		if DirAccess.rename_absolute(dst, old) != OK: return false
		var data := FileAccess.get_file_as_bytes(src)
		if data.is_empty(): DirAccess.rename_absolute(old, dst); return false
		var f := FileAccess.open(dst, FileAccess.WRITE)
		if not f: DirAccess.rename_absolute(old, dst); return false
		f.store_buffer(data)
	return true

func _do_editor_restart() -> void:
	_set_ver_status(_t("bar_restarting"), "", Color(0.4, 0.8, 1.0))
	await get_tree().create_timer(0.5).timeout
	get_editor_interface().restart_editor()

func _apply_windows_update_legacy() -> void:
	var ps_path  := OS.get_user_data_dir() + "/_godotluau_apply.ps1"
	var godot    := OS.get_executable_path().replace("/", "\\")
	var proj_dir := ProjectSettings.globalize_path("res://").rstrip("/\\").replace("/", "\\")
	var pid      := str(OS.get_process_id())

	var lines := PackedStringArray()
	lines.append("# GodotLuau auto-updater")
	lines.append("$ErrorActionPreference = 'Continue'")
	lines.append("$godotPid = %s" % pid)
	lines.append("$maxWait = 30; $waited = 0")
	lines.append("while ($waited -lt $maxWait) {")
	lines.append("    if (-not (Get-Process -Id $godotPid -ErrorAction SilentlyContinue)) { break }")
	lines.append("    Start-Sleep -Milliseconds 400; $waited++")
	lines.append("}")
	lines.append("Start-Sleep -Milliseconds 600")
	for item in _windows_staged_dlls:
		var s : String = item["src"].replace("/", "\\")
		var d : String = item["dst"].replace("/", "\\")
		lines.append("try { Copy-Item -Force -Path \"%s\" -Destination \"%s\" } catch { Write-Host $_.Exception.Message }" % [s, d])
	lines.append("Start-Sleep -Milliseconds 300")
	# -e: reabrir el EDITOR con el proyecto (sin -e se ejecutaría el juego)
	lines.append("& \"%s\" -e --path \"%s\"" % [godot, proj_dir])

	var pf := FileAccess.open(ps_path, FileAccess.WRITE)
	if not pf: _set_ver_status(_t("bar_script_err"), "", Color(1.0, 0.4, 0.4)); return
	pf.store_string("\r\n".join(lines)); pf.close()

	OS.create_process("powershell.exe", [
		"-WindowStyle", "Hidden", "-NonInteractive",
		"-ExecutionPolicy", "Bypass", "-File", ps_path.replace("/", "\\")
	])
	await get_tree().create_timer(0.3).timeout
	get_tree().quit()
