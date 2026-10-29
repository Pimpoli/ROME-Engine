@tool
extends EditorPlugin

# Lo conduce rome/qa/updater/probar_editor.py: lee el escenario de
# res://updater_qa.json y apunta lo que ve en res://updater_qa_result.json.

const ESCENARIO := "res://updater_qa.json"
const RESULTADO := "res://updater_qa_result.json"

var _datos: Dictionary = {}

func _enter_tree() -> void:
	if not FileAccess.file_exists(ESCENARIO):
		return
	_datos = JSON.parse_string(FileAccess.get_file_as_string(ESCENARIO))
	call_deferred("_run")

func _guardar(clave: String, valor: Variant) -> void:
	var resultado: Dictionary = {}
	if FileAccess.file_exists(RESULTADO):
		var previo = JSON.parse_string(FileAccess.get_file_as_string(RESULTADO))
		if previo is Dictionary:
			resultado = previo
	resultado[clave] = valor
	var archivo := FileAccess.open(RESULTADO, FileAccess.WRITE)
	archivo.store_string(JSON.stringify(resultado, "  "))
	archivo.close()

func _ahora() -> float:
	return Time.get_unix_time_from_system()

func _gestor() -> Object:
	return ROMEUpdateManager.get_singleton()

func _dialogo() -> Window:
	return _gestor().get_node("ROMEUpdateDialog") as Window

func _ventanas() -> Array:
	var lista := []
	for ventana: Window in EditorInterface.get_base_control().find_children("*", "Window", true, false):
		if ventana.visible and not ventana is PopupMenu:
			lista.append(ventana.title if not ventana.title.is_empty() else str(ventana.name))
	return lista

func _esperar(segundos: float, aceptar: Callable) -> Dictionary:
	var fin := Time.get_ticks_msec() + int(segundos * 1000.0)
	var estado: Dictionary = {}
	while Time.get_ticks_msec() < fin:
		if _gestor() != null:
			estado = _gestor().get_status()
			if aceptar.call(estado):
				return estado
		await get_tree().create_timer(0.25).timeout
	return estado

func _run() -> void:
	if _datos.has("guardar_cada_escena"):
		EditorInterface.get_editor_settings().set_setting("interface/editor/behavior/save_each_scene_on_quit", bool(_datos.guardar_cada_escena))
	await get_tree().create_timer(0.5).timeout
	var estado := await _esperar(90.0, func(e: Dictionary) -> bool: return not e.get("checking", true) and not str(e.get("rome_status", "idle")) in ["idle", "checking"])
	var version := str(estado.get("current_rome_version", ""))
	if version == str(_datos.get("version_nueva", "-")):
		_guardar("reinicio", {"version": version, "proyecto": ProjectSettings.globalize_path("res://"), "estado": estado, "ventanas": _ventanas(), "hora": _ahora()})
		# La limpieza de la caché corre 30 s después de arrancar.
		await get_tree().create_timer(40.0).timeout
		_guardar("reinicio_final", _gestor().get_status())
		get_tree().quit()
		return
	_guardar("inicio", {"version": version, "estado": estado, "ventanas": _ventanas(), "dialogo_visible": _dialogo().visible, "hora": _ahora()})
	match str(_datos.get("etapa", "")):
		"sin_novedad":
			await _sin_novedad()
		"con_novedad":
			await _con_novedad(false)
		"cancelar":
			await _con_novedad(true)
		"al_cerrar":
			await _al_cerrar()
			return
		"cierre_con_cambios":
			await _cierre_con_cambios()
			return
	get_tree().quit()

# El mismo cierre que usa «Actualizar y reiniciar», con la escena principal sin guardar.
func _cierre_con_cambios() -> void:
	await _abrir_escena_principal()
	EditorInterface.mark_scene_as_unsaved()
	_guardar("marcado", {"escena": EditorInterface.get_edited_scene_root() != null, "hora": _ahora()})
	EditorInterface.get_base_control().get_parent().notification(NOTIFICATION_WM_CLOSE_REQUEST)
	var fin := Time.get_ticks_msec() + 15000
	while Time.get_ticks_msec() < fin:
		await get_tree().create_timer(0.25).timeout
		var confirmacion := _confirmacion_visible()
		if confirmacion != null:
			_guardar("aviso_al_cerrar", {"texto": confirmacion.dialog_text, "hora": _ahora()})
			confirmacion.get_cancel_button().pressed.emit()
			await get_tree().create_timer(2.0).timeout
			_guardar("sigue_abierto_tras_cancelar", true)
			get_tree().quit()
			return
	_guardar("sin_aviso", {"hora": _ahora()})
	get_tree().quit()

func _al_cerrar() -> void:
	# Contra GitHub de verdad, un archivo recién subido puede tardar en bajar.
	var espera := float(_datos.get("espera_descarga", 240.0))
	var estado := await _esperar(espera, func(e: Dictionary) -> bool: return e.get("verified", false) or (not e.get("downloading", false) and not str(e.get("action_error", "")).is_empty()))
	_guardar("descarga", {"estado": estado, "hora": _ahora()})
	# Con una experiencia recién creada, la primera importación puede seguir en marcha.
	var fin_importacion := Time.get_ticks_msec() + 180000
	while EditorInterface.get_resource_filesystem().is_scanning() and Time.get_ticks_msec() < fin_importacion:
		await get_tree().create_timer(0.25).timeout
	_guardar("importado", {"escaneando": EditorInterface.get_resource_filesystem().is_scanning(), "hora": _ahora()})
	var antes_de_cerrar := float(_datos.get("espera_antes_de_cerrar", 0.0))
	if antes_de_cerrar > 0.0:
		await get_tree().create_timer(antes_de_cerrar).timeout
	EditorInterface.save_all_scenes()
	# Cierre normal del editor, como la X de la ventana (no SceneTree.quit()). En diferido: al
	# cerrar, el editor borra los plugins, y borrar este a mitad de una función corrompe el montón.
	EditorInterface.get_base_control().get_parent().call_deferred("notification", NOTIFICATION_WM_CLOSE_REQUEST)
	await _cerrar_sin_preguntas(30.0)
	_guardar("sigue_abierto", {"estado": _gestor().get_status(), "ventanas": _ventanas(), "hora": _ahora()})
	get_tree().quit()

func _abrir_escena_principal() -> void:
	if EditorInterface.get_edited_scene_root() == null:
		EditorInterface.open_scene_from_path(str(ProjectSettings.get_setting("application/run/main_scene")))
	for intento in 200:
		if EditorInterface.get_edited_scene_root() != null:
			return
		await get_tree().create_timer(0.05).timeout

# Si al cerrar pregunta por cambios sin guardar, responde «No guardar».
func _cerrar_sin_preguntas(segundos: float) -> void:
	var fin := Time.get_ticks_msec() + int(segundos * 1000.0)
	while Time.get_ticks_msec() < fin:
		await get_tree().create_timer(0.25).timeout
		var confirmacion := _confirmacion_visible()
		if confirmacion != null:
			_guardar("aviso_al_cerrar", {"texto": confirmacion.dialog_text, "hora": _ahora()})
			# En diferido, por lo mismo que el cierre de _al_cerrar.
			confirmacion.call_deferred("emit_signal", "custom_action", "discard")

func _menu_ayuda() -> bool:
	for menu: PopupMenu in EditorInterface.get_base_control().get_parent().find_children("*", "PopupMenu", true, false):
		for i in menu.item_count:
			if menu.get_item_text(i) == "Actualizaciones de ROME...":
				menu.id_pressed.emit(menu.get_item_id(i))
				return true
	return false

func _textos(nodo: Node) -> Array:
	var textos := []
	for etiqueta: Label in nodo.find_children("*", "Label", true, false):
		textos.append(etiqueta.text)
	return textos

func _sin_novedad() -> void:
	var encontrado := _menu_ayuda()
	await get_tree().create_timer(1.0).timeout
	var estado := await _esperar(30.0, func(e: Dictionary) -> bool: return not e.get("checking", true))
	_guardar("menu", {"encontrado": encontrado, "dialogo_visible": _dialogo().visible, "textos": _textos(_dialogo()), "estado": estado})
	_dialogo().hide()

func _confirmacion_visible() -> ConfirmationDialog:
	# El aviso de guardar puede cambiar de padre a la última ventana exclusiva: se busca desde la raíz.
	for ventana: ConfirmationDialog in get_tree().root.find_children("*", "ConfirmationDialog", true, false):
		if ventana.visible and ventana.get_parent() != null and not ventana.name.begins_with("ROMEUpdate"):
			return ventana
	return null

func _con_novedad(cancelar: bool) -> void:
	var dialogo := _dialogo()
	var descargar: Button = dialogo.find_child("ROMEDownloadUpdate", true, false)
	var instalar: Button = dialogo.find_child("ROMEInstallUpdate", true, false)
	var estado: Dictionary = _gestor().get_status()
	_guardar("antes", {"boton_descargar": descargar.visible, "estado": estado, "textos": _textos(dialogo)})
	if not estado.get("verified", false) and not estado.get("downloading", false):
		descargar.pressed.emit()
	estado = await _esperar(240.0, func(e: Dictionary) -> bool: return e.get("verified", false) or (not e.get("downloading", false) and not str(e.get("action_error", "")).is_empty()))
	_guardar("descarga", {"estado": estado, "boton_instalar": instalar.visible, "textos": _textos(dialogo)})
	if not estado.get("verified", false):
		return
	if cancelar:
		# Sin escena abierta no hay nada que guardar y el editor se cerraría sin preguntar.
		await _abrir_escena_principal()
		EditorInterface.mark_scene_as_unsaved()
	else:
		# Sin cambios pendientes, el cierre no pide guardar (nadie lo contestaría).
		EditorInterface.save_all_scenes()
	_guardar("instalar", {"hora": _ahora(), "escena": EditorInterface.get_edited_scene_root() != null})
	instalar.pressed.emit()
	if not cancelar:
		# El editor se cierra solo; si en 60 s sigue abierto, se apunta.
		await _cerrar_sin_preguntas(60.0)
		_guardar("sigue_abierto", {"estado": _gestor().get_status(), "ventanas": _ventanas(), "hora": _ahora()})
		return
	var confirmacion: ConfirmationDialog = null
	var fin := Time.get_ticks_msec() + 45000
	while confirmacion == null and Time.get_ticks_msec() < fin:
		await get_tree().create_timer(0.25).timeout
		confirmacion = _confirmacion_visible()
	_guardar("aviso_guardar", {"visible": confirmacion != null, "texto": confirmacion.dialog_text if confirmacion else "", "estado": _gestor().get_status(), "hora": _ahora()})
	if confirmacion == null:
		return
	await get_tree().create_timer(2.0).timeout
	confirmacion.get_cancel_button().pressed.emit()
	await get_tree().create_timer(4.0).timeout
	_guardar("cancelado", {"estado": _gestor().get_status(), "textos": _textos(dialogo), "hora": _ahora()})
	await get_tree().create_timer(4.0).timeout
