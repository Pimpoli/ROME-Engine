@tool
extends Node
## Shared by stock Godot + GodotLuau and ROME. Owner credentials stay in editor settings.

var plugin: EditorPlugin
var panel: VBoxContainer
var profiles: OptionButton
var mode: OptionButton
var fields := {}
var status: Label
var timer: Timer
var busy := false:
	set(value):
		busy = value
		if is_instance_valid(profiles):
			profiles.disabled = value
		for field in fields.values():
			field.editable = not value
var _profiles: Array = []
var _file_cache := {}
var _last_local := ""
var _last_remote := ""
var _remote_port := 25565
var _published := false
var _http: HTTPRequest
const CACHE := "res://.godot/luau-server"
const PREF := "_godot_luau_server_profiles"

func setup(owner_plugin: EditorPlugin, toolbar: Control) -> void:
	plugin = owner_plugin
	name = "GodotLuauServers"
	_profiles = EditorInterface.get_editor_settings().get_meta(PREF, [])
	panel = VBoxContainer.new()
	panel.name = "GodotLuauServerSettings"
	panel.custom_minimum_size = Vector2(400, 240)
	var heading := Label.new()
	heading.text = "Tus servidores · PC propia o máquina virtual"
	heading.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	panel.add_child(heading)
	var row := HBoxContainer.new()
	panel.add_child(row)
	profiles = OptionButton.new()
	profiles.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	profiles.item_selected.connect(_select)
	row.add_child(profiles)
	_button(row, "Añadir servidor", _add)
	for pair in [["name", "Nombre"], ["url", "URL de administración"], ["host", "Dirección del juego (IP o dominio)"], ["token", "Clave del propietario"]]:
		var field_row := HBoxContainer.new()
		panel.add_child(field_row)
		var label := Label.new()
		label.text = pair[1]
		label.custom_minimum_size.x = 240
		field_row.add_child(label)
		var field := LineEdit.new()
		field.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		field.secret = pair[0] == "token"
		field_row.add_child(field)
		fields[pair[0]] = field
	var buttons := HFlowContainer.new()
	panel.add_child(buttons)
	_button(buttons, "Guardar ajustes", _save_profile)
	_button(buttons, "Comprobar conexión", _check_connection)
	_button(buttons, "Guardar copia ahora", func() -> void: snapshot(true))
	_button(buttons, "Publicar / iniciar servidor", func() -> void: publish(false))
	_button(buttons, "Preparar y probar", func() -> void: publish(true))
	status = Label.new()
	status.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	status.text = "Guardado automático cada 5 minutos y al ejecutar. Local funciona sin conexión."
	panel.add_child(status)
	plugin.add_control_to_bottom_panel(panel, "Servidores")
	mode = OptionButton.new()
	mode.name = "GodotLuauExecutionMode"
	mode.add_item("Local")
	mode.add_item("Server Real")
	mode.tooltip_text = "Local: la PC ejecuta el juego. Server Real: conectar con la versión publicada en tu servidor. Para probar cambios nuevos usa Preparar y probar en Servidores."
	toolbar.add_child(mode)
	var settings_button := Button.new()
	settings_button.text = "Servidores"
	settings_button.pressed.connect(func() -> void: plugin.make_bottom_panel_item_visible(panel))
	toolbar.add_child(settings_button)
	_reload_profiles(int(EditorInterface.get_editor_settings().get_project_metadata("godot_luau", "server_profile", 0)))
	timer = Timer.new()
	timer.wait_time = 300
	timer.timeout.connect(func() -> void: snapshot(true))
	add_child(timer)
	timer.start()
	_http = HTTPRequest.new()
	_http.timeout = 240
	_http.use_threads = true
	add_child(_http)

func teardown() -> void:
	if is_instance_valid(panel):
		plugin.remove_control_from_bottom_panel(panel)
		panel.queue_free()
	queue_free()

func _button(parent: Node, text: String, callback: Callable) -> void:
	var button := Button.new()
	button.text = text
	button.pressed.connect(callback)
	parent.add_child(button)

func _add() -> void:
	if busy:
		return
	_profiles.append({"name": "Mi servidor", "url": "http://127.0.0.1:8088", "host": "127.0.0.1", "token": ""})
	_reload_profiles(_profiles.size() - 1)

func _reload_profiles(index := 0) -> void:
	profiles.clear()
	for profile in _profiles:
		profiles.add_item(str(profile.name))
	if not _profiles.is_empty():
		profiles.select(clampi(index, 0, _profiles.size() - 1))
		_select(profiles.selected)

func _select(index: int) -> void:
	if index < 0 or index >= _profiles.size():
		return
	EditorInterface.get_editor_settings().set_project_metadata("godot_luau", "server_profile", index)
	for key in fields:
		fields[key].text = str(_profiles[index].get(key, ""))
	_last_remote = ""
	_published = false
	_remote_port = int(_profiles[index].get("port", 25565))

func _save_profile() -> void:
	if busy:
		return
	var data := {}
	for key in fields:
		data[key] = fields[key].text.strip_edges()
	data["url"] = str(data.url).trim_suffix("/")
	if not _valid_profile(data):
		status.text = "Completa los campos. Usa HTTPS fuera de localhost y una IP o dominio sin puerto en Dirección del juego."
		return
	data["port"] = _remote_port
	if _profiles.is_empty():
		_profiles.append(data)
	else:
		_profiles[profiles.selected] = data
	EditorInterface.get_editor_settings().set_meta(PREF, _profiles)
	_reload_profiles(profiles.selected)
	status.text = "Servidor guardado en los ajustes privados del editor."

static func _valid_profile(data: Dictionary) -> bool:
	var url := str(data.get("url", ""))
	var host := str(data.get("host", ""))
	var local := url.begins_with("http://127.0.0.1:") or url.begins_with("http://localhost:")
	return (local or url.begins_with("https://")) and not host.is_empty() and not host.contains("/") and not host.contains(":") and not host.contains(" ") and not str(data.get("token", "")).is_empty()

func _profile() -> Dictionary:
	return _profiles[profiles.selected] if profiles.selected >= 0 and profiles.selected < _profiles.size() else {}

func is_remote() -> bool:
	return is_instance_valid(mode) and mode.selected == 1

func before_play() -> bool:
	if is_remote() and (not _valid_profile(_profile()) or not _published):
		status.text = "Primero usa Preparar y probar en Servidores para publicar y comprobar la conexión."
		plugin.make_bottom_panel_item_visible(panel)
		return false
	EditorInterface.save_all_scenes()
	# Native Play saves the scenes first; preserve scripts too and snapshot only changes.
	call_deferred("snapshot", true)
	return true

func run_arguments(args: PackedStringArray) -> PackedStringArray:
	var result := args.duplicate()
	if is_remote():
		if not result.has("--"):
			result.append("--")
		result.append_array(["--glconnect", str(_profile().host) + ":" + str(_remote_port)])
	return result

func _project_id() -> String:
	var settings := EditorInterface.get_editor_settings()
	var id := str(settings.get_project_metadata("godot_luau", "server_project_id", ""))
	if id.is_empty():
		id = "project-" + ProjectSettings.globalize_path("res://").sha256_text().left(20)
		settings.set_project_metadata("godot_luau", "server_project_id", id)
	return id

func _request(path: String, method: HTTPClient.Method, data := PackedByteArray()) -> Dictionary:
	var profile := _profile()
	if not _valid_profile(profile):
		return {"error": "Configura y guarda un servidor primero."}
	var headers := PackedStringArray(["Authorization: Bearer " + str(profile.token), "Content-Type: application/zip"])
	var error := _http.request_raw(str(profile.url) + path, headers, method, data)
	if error != OK:
		return {"error": "No se pudo iniciar la conexión: " + error_string(error)}
	var response: Array = await _http.request_completed
	if response[0] != HTTPRequest.RESULT_SUCCESS:
		return {"error": "No se pudo conectar al servidor. Revisa la dirección, el certificado y que esté encendido."}
	var decoded: Variant = JSON.parse_string(response[3].get_string_from_utf8())
	if not decoded is Dictionary:
		return {"error": "Respuesta inválida del servidor."}
	if response[1] != 200:
		return {"error": str(decoded.get("error", "HTTP " + str(response[1])))}
	return decoded

func _check_connection() -> void:
	if busy:
		return
	busy = true
	var response := await _request("/health", HTTPClient.METHOD_GET)
	status.text = str(response.error) if response.has("error") else "Conectado a tu servidor."
	busy = false

func _collect(directory: String, output: Array[String]) -> void:
	var access := DirAccess.open(directory)
	if access == null:
		return
	for folder in access.get_directories():
		if not folder.begins_with(".") and not access.is_link(folder):
			_collect(directory.path_join(folder), output)
	for file in access.get_files():
		if not file.begins_with(".") and not access.is_link(file) and file.get_extension().to_lower() not in ["zip", "log", "tmp", "bak", "exe", "pdb", "lib", "o", "obj"]:
			output.append(directory.path_join(file))

func _make_snapshot() -> Dictionary:
	if EditorInterface.get_unsaved_scenes().has(""):
		return {"error": "Guarda las escenas nuevas con un nombre antes de crear una copia del proyecto."}
	var native := EditorInterface.get_base_control().get_parent()
	# Stock Save All stops the running game. ROME exposes the same save pipeline
	# without Stop; stock Godot defers unsaved scene buffers until play finishes.
	if native.has_method("rome_save_all_scenes"):
		native.call("rome_save_all_scenes")
	elif not EditorInterface.is_playing_scene():
		EditorInterface.save_all_scenes()
	elif not EditorInterface.get_unsaved_scenes().is_empty():
		return {"error": "Hay escenas sin guardar. La copia se aplaza hasta terminar la prueba; la partida sigue ejecutándose."}
	# Apply/flush script buffers as well as scene changes before reading files.
	var script_editor := EditorInterface.get_script_editor()
	if script_editor.has_method("save_all_scripts"):
		script_editor.call("save_all_scripts")
	var paths: Array[String] = []
	_collect("res://", paths)
	paths.sort()
	var manifest := PackedStringArray()
	for index in paths.size():
		var path := paths[index]
		var stamp := FileAccess.get_modified_time(path)
		var file := FileAccess.open(path, FileAccess.READ)
		if file == null:
			return {"error": "No se pudo leer " + path}
		var length := file.get_length()
		file.close()
		var cached: Array = _file_cache.get(path, [])
		if length < 4 * 1024 * 1024 or cached.is_empty() or cached[0] != stamp or cached[1] != length:
			cached = [stamp, length, FileAccess.get_sha256(path)]
			_file_cache[path] = cached
		manifest.append(path + ":" + str(cached[2]))
		if index % 32 == 0:
			await get_tree().process_frame
	var digest := "\n".join(manifest).sha256_text()
	DirAccess.make_dir_recursive_absolute(CACHE)
	var archive := CACHE.path_join(digest + ".zip")
	if digest == _last_local and FileAccess.file_exists(archive):
		return {"path": archive, "digest": digest}
	var packer := ZIPPacker.new()
	if packer.open(archive + ".tmp") != OK:
		return {"error": "No se pudo escribir la copia local."}
	for index in paths.size():
		var path := paths[index]
		if packer.start_file(path.trim_prefix("res://")) != OK or packer.write_file(FileAccess.get_file_as_bytes(path)) != OK or packer.close_file() != OK:
			packer.close()
			return {"error": "Error al crear la copia de " + path}
		if index % 16 == 0:
			await get_tree().process_frame
	packer.close()
	if DirAccess.rename_absolute(archive + ".tmp", archive) != OK:
		return {"error": "No se pudo finalizar la copia local."}
	_last_local = digest
	return {"path": archive, "digest": digest}

func snapshot(upload := true) -> Dictionary:
	if busy:
		return {"error": "Otra operación está en curso."}
	busy = true
	status.text = "Guardando los cambios…"
	var result := await _make_snapshot()
	if not result.has("error") and upload and _valid_profile(_profile()) and result.digest != _last_remote:
		status.text = "Enviando copia a tu servidor…"
		var response := await _request("/v1/projects/" + _project_id() + "/snapshot", HTTPClient.METHOD_PUT, FileAccess.get_file_as_bytes(result.path))
		if response.has("error"):
			result["error"] = "Copia local conservada. " + str(response.error)
		else:
			_last_remote = result.digest
	status.text = str(result.error) if result.has("error") else "Cambios guardados" + (" en tu servidor." if _last_remote == result.digest else " en una copia local.")
	busy = false
	return result

func publish(play_after: bool) -> void:
	if busy:
		return
	var saved := await snapshot(true)
	if saved.has("error"):
		return
	busy = true
	status.text = "Preparando el servidor dedicado…"
	var result := await _request("/v1/projects/" + _project_id() + "/publish", HTTPClient.METHOD_POST)
	busy = false
	if result.has("error"):
		status.text = str(result.error)
		return
	_remote_port = int(result.port)
	_published = true
	status.text = "Servidor activo en " + str(_profile().host) + ":" + str(_remote_port)
	if play_after:
		mode.select(1)
		EditorInterface.play_main_scene()
