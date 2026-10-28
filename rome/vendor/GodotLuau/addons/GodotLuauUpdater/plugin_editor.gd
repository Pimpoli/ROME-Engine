@tool
extends "res://addons/GodotLuauUpdater/plugin_base.gd"

# Plugin de GodotLuau, parte 2 de 5: integracion con el editor. Enabled
# en el inspector, colores de Luau, multijugador local junto al Play,
# papelera de scripts, plugin.cfg, migracion de carpetas y ajustes.

# ── Propiedad "Enabled" de los scripts con texto True/False (como Roblox) ────
var _inspector_plugin: EditorInspectorPlugin = null

class EnabledProperty extends EditorProperty:
	var check := CheckBox.new()
	func _init() -> void:
		check.text = "True"
		add_child(check)
		add_focusable(check)
		check.toggled.connect(_on_toggled)
	func _on_toggled(v: bool) -> void:
		check.text = "True" if v else "False"
		emit_changed("Enabled", v)
	func _update_property() -> void:
		var v: bool = bool(get_edited_object().get("Enabled"))
		check.set_pressed_no_signal(v)
		check.text = "True" if v else "False"

class ScriptEnabledInspector extends EditorInspectorPlugin:
	func _can_handle(object: Object) -> bool:
		return object != null and object.get_class() in ["LocalScript", "ServerScript", "ModuleScript"]
	func _parse_property(object: Object, type: Variant.Type, name: String, hint: PropertyHint,
			hint_string: String, usage: int, wide: bool) -> bool:
		if name == "Enabled":
			add_property_editor(name, EnabledProperty.new())
			return true   # reemplaza el checkbox por defecto ("Activado")
		return false

# ── Colores de sintaxis Luau (como Roblox Studio, tema oscuro) ────────────────
# Mecanismo OFICIAL: un EditorSyntaxHighlighter registrado en el ScriptEditor;
# el editor lo activa solo para scripts cuyo lenguaje sea "Luau".
# (La version anterior reemplazaba el syntax_highlighter interno del CodeEdit
# del editor de scripts y CRASHEABA el editor al abrir cualquier script.)
var _luau_hl: EditorSyntaxHighlighter = null

class LuauHighlighter extends EditorSyntaxHighlighter:
	const C_TEXT := Color("#cccccc")
	const C_KW   := Color("#f86d7c")
	const C_BI   := Color("#84d6f7")
	const C_STR  := Color("#adf195")
	const C_NUM  := Color("#ffc600")
	const C_COM  := Color("#666666")
	const C_FN   := Color("#fdfbac")
	const KEYWORDS := {"and":1,"break":1,"do":1,"else":1,"elseif":1,"end":1,"for":1,
		"function":1,"if":1,"in":1,"local":1,"not":1,"or":1,"repeat":1,"return":1,
		"then":1,"until":1,"while":1,"continue":1,"export":1,"self":1}
	const CONSTS := {"true":1,"false":1,"nil":1}
	const BUILTINS := {"game":1,"workspace":1,"script":1,"print":1,"warn":1,"wait":1,
		"task":1,"spawn":1,"delay":1,"require":1,"pairs":1,"ipairs":1,"next":1,
		"select":1,"tostring":1,"tonumber":1,"typeof":1,"type":1,"pcall":1,"xpcall":1,
		"error":1,"assert":1,"unpack":1,"math":1,"string":1,"table":1,"os":1,
		"coroutine":1,"bit32":1,"utf8":1,"Instance":1,"Vector2":1,"Vector3":1,
		"CFrame":1,"Color3":1,"UDim":1,"UDim2":1,"Enum":1,"Ray":1,"Region3":1,
		"TweenInfo":1,"NumberRange":1,"Random":1}

	func _get_name() -> String:
		return "Luau"
	func _get_supported_languages() -> PackedStringArray:
		return PackedStringArray(["Luau"])

	static func _is_word_char(c: String) -> bool:
		return (c >= "a" and c <= "z") or (c >= "A" and c <= "Z") or (c >= "0" and c <= "9") or c == "_"

	func _get_line_syntax_highlighting(p_line: int) -> Dictionary:
		var te := get_text_edit()
		if te == null or p_line >= te.get_line_count(): return {}
		var text := te.get_line(p_line)
		var n := text.length()
		var d := {}
		d[0] = {"color": C_TEXT}
		var i := 0
		while i < n:
			var c := text[i]
			# Comentario: -- hasta el final de la linea
			if c == "-" and i + 1 < n and text[i + 1] == "-":
				d[i] = {"color": C_COM}
				break
			# Strings "..." y '...'
			if c == "\"" or c == "'":
				d[i] = {"color": C_STR}
				var q := c
				i += 1
				while i < n and text[i] != q:
					i += 2 if text[i] == "\\" else 1
				i += 1
				if i < n: d[i] = {"color": C_TEXT}
				continue
			# Numeros (enteros, decimales, hex)
			if c >= "0" and c <= "9":
				var start := i
				while i < n and (_is_word_char(text[i]) or text[i] == "."):
					i += 1
				d[start] = {"color": C_NUM}
				if i < n: d[i] = {"color": C_TEXT}
				continue
			# Identificadores: keyword / builtin / constante / llamada a funcion
			if (c >= "a" and c <= "z") or (c >= "A" and c <= "Z") or c == "_":
				var start := i
				while i < n and _is_word_char(text[i]):
					i += 1
				var w := text.substr(start, i - start)
				var col := C_TEXT
				if KEYWORDS.has(w):   col = C_KW
				elif CONSTS.has(w):   col = C_NUM
				elif BUILTINS.has(w): col = C_BI
				else:
					var j := i
					while j < n and text[j] == " ": j += 1
					if j < n and text[j] == "(": col = C_FN
				if col != C_TEXT:
					d[start] = {"color": col}
					if i < n: d[i] = {"color": C_TEXT}
				continue
			i += 1
		return d

func _setup_luau_highlighting() -> void:
	var se := EditorInterface.get_script_editor()
	if se == null: return
	_luau_hl = LuauHighlighter.new()
	se.register_syntax_highlighter(_luau_hl)

func _teardown_luau_highlighting() -> void:
	if _luau_hl != null:
		var se := EditorInterface.get_script_editor()
		if se != null:
			se.unregister_syntax_highlighter(_luau_hl)
		_luau_hl = null

# ── Local multiplayer controls next to the NATIVE Play button ────────────────
# A "Players" SpinBox (1-8) + a PC/Mobile/Console/VR device selector, placed to
# the LEFT of Godot's own Play button (no custom Play button is created). When
# the user presses the native Play with Players >= 2, extra game windows are
# launched and everyone connects into a shared session (Player1..N) — they see,
# collide and move around each other like Roblox Studio's local test.
var _mp_bar: HBoxContainer = null
var _mp_count: SpinBox = null
var _mp_device: OptionButton = null
var _mp_child_pids: Array[int] = []
var _mp_run_bar: Node = null          # EditorRunBar (para desconectar señales)
var _mp_used_container: bool = false  # true si caímos al fallback CONTAINER_TOOLBAR

func _find_node_by_class(root: Node, cls: String) -> Node:
	if root == null: return null
	if root.get_class() == cls: return root
	for c in root.get_children():
		var r := _find_node_by_class(c, cls)
		if r: return r
	return null

func _build_mp_toolbar() -> void:
	_mp_bar = HBoxContainer.new()
	_mp_bar.add_theme_constant_override("separation", 5)

	_mp_bar.add_child(VSeparator.new())

	# Icono de multijugador (del theme del editor, si existe)
	var base := EditorInterface.get_base_control()
	if base.has_theme_icon("MultiplayerSynchronizer", "EditorIcons"):
		var ico := TextureRect.new()
		ico.texture = base.get_theme_icon("MultiplayerSynchronizer", "EditorIcons")
		ico.stretch_mode = TextureRect.STRETCH_KEEP_CENTERED
		ico.tooltip_text = "GodotLuau local multiplayer"
		_mp_bar.add_child(ico)

	var lbl := Label.new()
	lbl.text = "Players"
	lbl.add_theme_color_override("font_color", base.get_theme_color("font_color", "Editor"))
	_mp_bar.add_child(lbl)

	_mp_count = SpinBox.new()
	_mp_count.min_value = 1
	_mp_count.max_value = 8
	_mp_count.step = 1
	_mp_count.value = 1
	# Un solo digito (max 8): angosto para no ocupar barra de mas
	_mp_count.custom_minimum_size = Vector2(44, 0)
	_mp_count.tooltip_text = "How many players to emulate.\n1 = single player.\n2-8 = pressing Play opens one window per player (Player1..N) that connect to each other automatically, PLUS the Server View window (this game view), like Roblox Studio."
	_mp_bar.add_child(_mp_count)

	_mp_device = OptionButton.new()
	_mp_device.add_item("PC")
	_mp_device.add_item("Mobile")
	_mp_device.add_item("Console")
	_mp_device.add_item("VR")
	_mp_device.selected = 0
	_mp_device.flat = true
	# El boton toma el ancho de la opcion elegida (no el de la mas larga)
	_mp_device.fit_to_longest_item = false
	_mp_device.tooltip_text = "Emulated device (PC by default):\n- Mobile: phone-shaped window + on-screen joystick and jump button (mouse acts as your finger).\n- Console: 16:9 TV window, play with a gamepad.\n- VR: split-screen preview with one view per eye.\nScripts can read it with net:GetDevice()."
	_mp_bar.add_child(_mp_device)

	_mp_bar.add_child(VSeparator.new())

	# Keep the host run-args in sync BEFORE Play is pressed (the native instance
	# is launched by Godot itself, so its args must already be configured).
	_mp_count.value_changed.connect(func(_v): _sync_mp_session())
	_mp_device.item_selected.connect(func(_i): _sync_mp_session())

	# Place the controls to the LEFT of Godot's native Play button and hook its
	# signal — we do NOT create our own Play button.
	var run_bar := _find_node_by_class(EditorInterface.get_base_control(), "EditorRunBar")
	if run_bar == null:
		run_bar = _find_node_by_class(get_tree().get_root(), "EditorRunBar")
	if run_bar and run_bar.get_parent():
		_mp_run_bar = run_bar
		var parent := run_bar.get_parent()
		parent.add_child(_mp_bar)
		parent.move_child(_mp_bar, run_bar.get_index())   # justo antes del Play
		if not run_bar.is_connected("play_pressed", _on_native_play):
			run_bar.connect("play_pressed", _on_native_play)
		if not run_bar.is_connected("stop_pressed", _on_native_stop):
			run_bar.connect("stop_pressed", _on_native_stop)
	else:
		# Fallback: standard plugin toolbar (still next to Play)
		_mp_used_container = true
		add_control_to_container(EditorPlugin.CONTAINER_TOOLBAR, _mp_bar)

	# Botón Server View / Client View en la barra del panel Game del editor
	# (funciona con 1-8 jugadores; cambia la perspectiva de la instancia
	# host/única SIN mover al personaje, como Roblox Studio).
	_build_view_button()

	# Clean slate: reparar los run args rotos de 1.9.5/1.9.6, sincronizar la
	# sesion (default 1 + PC → sin archivo) y limpiar comandos de vista viejos.
	_repair_main_run_args()
	_sync_mp_session()
	_remove_res_file("GodotLuau/.gl_view_cmd")
	# Apagar el sistema nativo de multiples instancias para que no duplique ventanas
	_neutralize_native_instances()

# ── Botón de perspectiva Server/Client en la barra del panel Game ────────────
var _view_btn: Button = null
var _view_counter: int = 0

func _build_view_button() -> void:
	_view_btn = Button.new()
	_view_btn.toggle_mode = true
	_view_btn.flat = true
	_view_btn.text = "Server"
	var base := EditorInterface.get_base_control()
	if base.has_theme_icon("Camera3D", "EditorIcons"):
		_view_btn.icon = base.get_theme_icon("Camera3D", "EditorIcons")
	_view_btn.tooltip_text = "Switch this game view between perspectives, like Roblox Studio:\n- Server: free camera — WASD + right mouse to fly, wheel = speed, E/Q up-down. Your player does NOT move.\n- Client: back to the player camera.\nAvailable with 1-8 players. In multiplayer tests this window is always the server."
	_view_btn.toggled.connect(_on_view_btn_toggled)
	# Barra del panel Game (la de Entrada / 2D / 3D...): su primer HBoxContainer
	var game_view := _find_node_by_class(EditorInterface.get_base_control(), "GameView")
	var bar: Node = _find_node_by_class(game_view, "HBoxContainer") if game_view else null
	if bar:
		bar.add_child(VSeparator.new())
		bar.add_child(_view_btn)
	elif _mp_bar:
		# Sin panel Game: junto a nuestra barra de Players
		_mp_bar.add_child(_view_btn)

func _update_view_btn_label(pressed: bool) -> void:
	if _view_btn == null: return
	_view_btn.text = "Client" if pressed else "Server"

func _on_view_btn_toggled(pressed: bool) -> void:
	_view_counter += 1
	_update_view_btn_label(pressed)
	var f := FileAccess.open("res://GodotLuau/.gl_view_cmd", FileAccess.WRITE)
	if f:
		f.store_string("%s|%d" % ["server" if pressed else "client", _view_counter])
		f.close()

func _reset_view_button() -> void:
	if _view_btn and is_instance_valid(_view_btn):
		_view_btn.set_pressed_no_signal(false)
		_update_view_btn_label(false)
	_remove_res_file("GodotLuau/.gl_view_cmd")

func _remove_mp_toolbar() -> void:
	if _mp_run_bar and is_instance_valid(_mp_run_bar):
		if _mp_run_bar.is_connected("play_pressed", _on_native_play):
			_mp_run_bar.disconnect("play_pressed", _on_native_play)
		if _mp_run_bar.is_connected("stop_pressed", _on_native_stop):
			_mp_run_bar.disconnect("stop_pressed", _on_native_stop)
	_mp_run_bar = null
	if _mp_bar and is_instance_valid(_mp_bar):
		if _mp_used_container:
			remove_control_from_container(EditorPlugin.CONTAINER_TOOLBAR, _mp_bar)
		elif _mp_bar.get_parent():
			_mp_bar.get_parent().remove_child(_mp_bar)
		_mp_bar.queue_free()
		_mp_bar = null

func _mp_device_name() -> String:
	var devices := ["PC", "Mobile", "Console", "VR"]
	return devices[_mp_device.selected] if _mp_device else "PC"

# La sesion de prueba viaja por res://GodotLuau/.gl_mp_session ("count|device|stamp").
# res:// es la MISMA carpeta para el editor y para el juego lanzado desde el
# editor, asi que la ventana nativa (que sera la VISTA DEL SERVIDOR) siempre la
# encuentra. NUNCA se tocan los run args nativos: inyectar argumentos en
# "editor/run/main_run_args" impedia que el Play nativo lanzara el juego.
const MP_ARGS_MARKER := "-- --glindex"

func _strip_mp_args(s: String) -> String:
	var i := s.find(MP_ARGS_MARKER)
	if i >= 0:
		s = s.substr(0, i)
	return s.strip_edges()

# REPARACION: versiones 1.9.5/1.9.6 dejaron args nuestros en main_run_args, lo
# que rompia el Play nativo. Se limpian una vez al cargar el plugin.
func _repair_main_run_args() -> void:
	var cur := str(ProjectSettings.get_setting("editor/run/main_run_args", ""))
	var base := _strip_mp_args(cur)
	if cur != base:
		ProjectSettings.set_setting("editor/run/main_run_args", base)
		ProjectSettings.save()
		print("[GodotLuau] Repaired editor/run/main_run_args (removed leftover multiplayer args).")

func _sync_mp_session() -> void:
	var count: int = int(_mp_count.value) if _mp_count else 1
	var device := _mp_device_name()
	if count >= 2 or device != "PC":
		var f := FileAccess.open("res://GodotLuau/.gl_mp_session", FileAccess.WRITE)
		if f:
			f.store_string("%d|%s|%d" % [count, device, int(Time.get_unix_time_from_system())])
			f.close()
	else:
		_remove_res_file("GodotLuau/.gl_mp_session")

func _remove_res_file(fname: String) -> void:
	var d := DirAccess.open("res://")
	if d and d.file_exists(fname):
		d.remove(fname)

# El dialogo nativo "Customize Run Instances" NO debe pelear con nuestra barra:
# si el usuario lo dejo activado con N instancias, el Play nativo lanzaria N
# ventanas nativas ADEMAS de nuestros clientes (cuentas de ventanas absurdas).
# Se apaga en vivo (widgets) y en su metadata persistida.
func _neutralize_native_instances() -> void:
	var es := EditorInterface.get_editor_settings()
	if es:
		es.set_project_metadata("debug_options", "multiple_instances_enabled", false)
	var dlg := _find_node_by_class(EditorInterface.get_base_control().get_tree().get_root(), "RunInstancesDialog")
	if dlg == null: return
	var cb := _find_node_by_class(dlg, "CheckBox")
	if cb and cb is CheckBox and (cb as CheckBox).button_pressed:
		(cb as CheckBox).button_pressed = false
	var sp := _find_node_by_class(dlg, "SpinBox")
	if sp and sp is SpinBox and (sp as SpinBox).value > 1:
		(sp as SpinBox).value = 1

# Native Play pressed → if Players >= 2, launch ONE window per player
# (glindex 2..N+1 → Player1..PlayerN). The native game window (embedded in the
# editor's Game panel) becomes the SERVER VIEW: it hosts, has no character and
# starts with the free camera — exactly like Roblox Studio's local test.
func _on_native_play() -> void:
	# Always close the windows we launched before (so a previous run's client
	# windows never linger and inflate the count on the next Play).
	_kill_mp_children()
	var count: int = int(_mp_count.value) if _mp_count else 1
	_sync_mp_session()   # sello fresco para la ventana nativa (el server)
	if count < 2:
		return
	var device := _mp_device_name()
	# Ensure the extra windows load the CURRENT project state from disk.
	EditorInterface.save_all_scenes()
	# Run the SAME scene the server runs (F5 main / F6 current / F7 custom) so
	# every window shares one world. Only a real scene path is forwarded.
	var scene := str(EditorInterface.get_playing_scene())
	var pass_scene := scene.begins_with("res://") and (scene.ends_with(".tscn") or scene.ends_with(".scn"))
	# One window per player: glindex 2..N+1 (they connect to 127.0.0.1).
	var exe := OS.get_executable_path()
	var proj := ProjectSettings.globalize_path("res://")
	for i in range(2, count + 2):
		var a: Array = ["--path", proj]
		if pass_scene: a.append(scene)
		a.append_array(["--", "--glindex", str(i), "--glcount", str(count), "--gldevice", device])
		var pid := OS.create_process(exe, a)
		if pid > 0: _mp_child_pids.append(pid)
		else: push_warning("[GodotLuau] Could not launch game window for Player%d." % (i - 1))
	# La ventana nativa (panel Game) arranca en Server View: reflejarlo en el boton
	if _view_btn and is_instance_valid(_view_btn):
		_view_btn.set_pressed_no_signal(true)
		_update_view_btn_label(true)

# Native Stop pressed → close the extra windows and reset the view toggle.
func _on_native_stop() -> void:
	_kill_mp_children()
	_reset_view_button()

# Kill every client window we launched (guarded: PIDs can be dead/recycled).
func _kill_mp_children() -> void:
	for pid in _mp_child_pids:
		if OS.is_process_running(pid):
			OS.kill(pid)
	_mp_child_pids.clear()

# ── Ciclo de vida de scripts: papelera + restauración con Ctrl+Z ─────────────
# Al borrar un nodo LocalScript/ServerScript/ModuleScript en el editor, su .lua
# se mueve a res://GodotLuau/.luau_trash/<script_id>.lua. Si el nodo vuelve (Ctrl+Z),
# el archivo se restaura automáticamente con todo su contenido.

func _connect_script_lifecycle() -> void:
	var tree := get_tree()
	if not tree.node_removed.is_connected(_on_any_node_removed):
		tree.node_removed.connect(_on_any_node_removed)
	if not tree.node_added.is_connected(_on_any_node_added):
		tree.node_added.connect(_on_any_node_added)

func _disconnect_script_lifecycle() -> void:
	var tree := get_tree()
	if tree.node_removed.is_connected(_on_any_node_removed):
		tree.node_removed.disconnect(_on_any_node_removed)
	if tree.node_added.is_connected(_on_any_node_added):
		tree.node_added.disconnect(_on_any_node_added)

func _node_script_id(n: Node) -> String:
	var v = n.get("script_id")
	return str(v) if v is String else ""

func _node_script_path(n: Node) -> String:
	var res = n.get("codigo_luau")
	if res is Resource: return (res as Resource).resource_path
	return ""

func _on_any_node_removed(n: Node) -> void:
	# Si se quita la raíz de la escena editada es un cierre/cambio de escena:
	# suprimir el trasheo de todo ese lote de nodos
	if n == EditorInterface.get_edited_scene_root():
		_suppress_trash_frame = Engine.get_process_frames() + 5
		return
	if n.get_class() in SCRIPT_NODE_CLASSES:
		# Se pasa el instance_id (int), no el Node: para cuando corre el deferred el
		# nodo puede estar ya liberado y marshalarlo como Node falla
		# ("Cannot convert argument 1 from Object to Object").
		_check_script_removed.call_deferred(n.get_instance_id(), _node_script_id(n), _node_script_path(n))
	elif n.get_child_count() > 0:
		# Al borrar un PADRE, sus scripts DESCENDIENTES también deben ir a la
		# papelera (antes sus .lua quedaban huérfanos en el proyecto).
		_queue_descendant_scripts(n)

func _queue_descendant_scripts(n: Node) -> void:
	for c in n.get_children():
		if c.get_class() in SCRIPT_NODE_CLASSES:
			_check_script_removed.call_deferred(c.get_instance_id(), _node_script_id(c), _node_script_path(c))
		if c.get_child_count() > 0:
			_queue_descendant_scripts(c)

func _check_script_removed(node_id: int, id: String, path: String) -> void:
	await get_tree().process_frame
	if Engine.get_process_frames() <= _suppress_trash_frame: return
	var n := instance_from_id(node_id) as Node
	if is_instance_valid(n) and n.is_inside_tree(): return   # reparent o undo inmediato
	if EditorInterface.get_edited_scene_root() == null: return
	_trash_script(id, path)

func _trash_script(id: String, path: String) -> void:
	if id.is_empty() or path.is_empty() or not path.begins_with("res://"): return
	if not FileAccess.file_exists(path): return
	# Nodo duplicado: si otro nodo de la escena usa el mismo ID, no tocar el archivo
	var root := EditorInterface.get_edited_scene_root()
	if root and _scene_has_script_id(root, id): return
	var g_trash := ProjectSettings.globalize_path(TRASH_DIR)
	DirAccess.make_dir_recursive_absolute(g_trash)
	var ignore_path := g_trash + "/.gdignore"
	if not FileAccess.file_exists(ignore_path):
		var f := FileAccess.open(ignore_path, FileAccess.WRITE)
		if f: f.store_string("")
	if DirAccess.rename_absolute(ProjectSettings.globalize_path(path), g_trash + "/" + id + ".lua") == OK:
		print("[GodotLuau] 🗑 Script en papelera: %s (Ctrl+Z para restaurar)" % path.get_file())
		EditorInterface.get_resource_filesystem().scan()

func _scene_has_script_id(n: Node, id: String) -> bool:
	if n.get_class() in SCRIPT_NODE_CLASSES and _node_script_id(n) == id:
		return true
	for c in n.get_children():
		if _scene_has_script_id(c, id): return true
	return false

func _on_any_node_added(n: Node) -> void:
	if not (n.get_class() in SCRIPT_NODE_CLASSES): return
	_check_script_restored.call_deferred(n)

func _check_script_restored(n: Node) -> void:
	if not is_instance_valid(n) or not n.is_inside_tree(): return
	var id := _node_script_id(n)
	if id.is_empty(): return
	var path := _node_script_path(n)
	if path.is_empty():
		# Ruta nueva (dentro de GodotLuau/); si el proyecto es viejo y esa
		# carpeta no existe pero la vieja si, usar la vieja.
		path = "res://GodotLuau/%ss/%s.lua" % [n.get_class(), id]
		var old_path := "res://%ss/%s.lua" % [n.get_class(), id]
		if not FileAccess.file_exists(path) and FileAccess.file_exists(old_path):
			path = old_path
	if FileAccess.file_exists(path): return
	var trash_file := ProjectSettings.globalize_path(TRASH_DIR + "/" + id + ".lua")
	if not FileAccess.file_exists(trash_file): return
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(path.get_base_dir()))
	if DirAccess.rename_absolute(trash_file, ProjectSettings.globalize_path(path)) == OK:
		print("[GodotLuau] ♻ Script restaurado: " + path.get_file())
		EditorInterface.get_resource_filesystem().scan()

func _purge_old_trash() -> void:
	# Los scripts llevan más de 7 días en la papelera → limpieza definitiva
	var g_trash := ProjectSettings.globalize_path(TRASH_DIR)
	if not DirAccess.dir_exists_absolute(g_trash): return
	var now := Time.get_unix_time_from_system()
	for f in DirAccess.get_files_at(g_trash):
		if not f.ends_with(".lua"): continue
		var p := g_trash + "/" + f
		if now - FileAccess.get_modified_time(p) > 7 * 86400:
			DirAccess.remove_absolute(p)

# ── Plugin.cfg auto-sync from res://Version ───────────────────────────────────

func _sync_plugin_cfg() -> void:
	for old in ["user://godotluau_version.txt",
				"res://addons/GodotLuauUpdater/installed_version.txt"]:
		if FileAccess.file_exists(old):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(old) \
				if old.begins_with("res://") \
				else OS.get_user_data_dir().path_join(old.get_file()))

	var ver := _get_local_version().trim_prefix("v")
	if ver == _t("ver_unknown"): return
	var cfg_path := "res://addons/GodotLuauUpdater/plugin.cfg"
	if not FileAccess.file_exists(cfg_path): return
	var f := FileAccess.open(cfg_path, FileAccess.READ)
	if not f: return
	var lines := f.get_as_text().split("\n")
	var changed := false
	# Quitar BOM UTF-8 si alguna herramienta externa lo metio (rompe el plugin)
	if lines.size() > 0 and lines[0].begins_with(char(0xFEFF)):
		lines[0] = lines[0].trim_prefix(char(0xFEFF))
		changed = true
	for i in lines.size():
		if lines[i].begins_with("version="):
			var expected := 'version="%s"' % ver
			if lines[i] != expected:
				lines[i] = expected
				changed = true
			break
	if not changed: return
	var wf := FileAccess.open(cfg_path, FileAccess.WRITE)
	if wf: wf.store_string("\n".join(lines))

# ── Watermark integrity check ─────────────────────────────────────────────────

func _check_watermark() -> void:
	# Aviso único — sin timer repetitivo (quien compila su propia DLL puede ignorarlo)
	if Engine.has_meta(WM_META_KEY): return
	push_warning("[GodotLuau] " + _t("warn_watermark"))

# ── Outdated version console warning ─────────────────────────────────────────

func _setup_outdated_warning(local: String, remote: String) -> void:
	if not ProjectSettings.get_setting("godot_luau/notify_outdated_version", true): return
	push_warning("[GodotLuau] ⚠ Outdated version: %s — latest: %s | %s" % [local, remote, GITHUB_URL])
	if _outdated_timer and is_instance_valid(_outdated_timer): return
	_outdated_timer = Timer.new()
	_outdated_timer.wait_time = 30.0
	add_child(_outdated_timer)
	_outdated_timer.timeout.connect(func():
		if ProjectSettings.get_setting("godot_luau/notify_outdated_version", true):
			push_warning("[GodotLuau] ⚠ Still on outdated version: %s — latest: %s" % [local, remote])
	)
	_outdated_timer.start()

func _stop_outdated_warning() -> void:
	if _outdated_timer and is_instance_valid(_outdated_timer):
		_outdated_timer.stop()
		_outdated_timer.queue_free()
		_outdated_timer = null

# ── Autocomplete speed ────────────────────────────────────────────────────────

func _apply_autocomplete_speed() -> void:
	if not ProjectSettings.get_setting("godot_luau/instant_autocomplete", true): return
	var es := EditorInterface.get_editor_settings()
	es.set("text_editor/completion/code_complete_delay", 0.05)
	es.set("text_editor/completion/idle_parse_delay",    0.05)

# ── Cleanup old DLL backups ───────────────────────────────────────────────────

func _cleanup_old_dlls() -> void:
	# v1.11.2: la migracion se DIFIERE. Borrar carpetas y forzar scan() durante
	# el PRIMER escaneo del editor crasheaba el editor entero (signal 11 en el
	# hilo del escaner). Ahora espera a que el editor este tranquilo.
	if is_inside_tree():
		get_tree().create_timer(3.0).timeout.connect(_migrate_old_layout)
	var bin_path := ProjectSettings.globalize_path("res://GodotLuau/bin/")
	if not DirAccess.dir_exists_absolute(bin_path): return
	var dir := DirAccess.open(bin_path)
	if not dir: return
	dir.list_dir_begin()
	var f := dir.get_next()
	while f != "":
		# Restos del intercambio de DLL (.old / .new) y los temporales que deja
		# Windows al renombrar (~...TMP). El '~godotluau...dll' es la copia viva
		# que usa Godot mientras corre; si esta bloqueada el borrado falla solo,
		# y si quedo huerfana de una sesion anterior se limpia.
		if f.ends_with(".old") or f.ends_with(".new") or f.ends_with(".TMP") \
		or (f.begins_with("~") and f.contains("godot_luau")):
			DirAccess.remove_absolute(bin_path + f)
		f = dir.get_next()

# ── Migracion a la estructura nueva (todo dentro de res://GodotLuau/) ─────────
# Las instalaciones viejas tenian bin/, icons/, DefaultScripts/ y assets/ en la
# raiz. El zip nuevo ya extrae en GodotLuau/; aqui se limpian los restos viejos
# (solo carpetas DE LA EXTENSION, nunca contenido del usuario).
func _migrate_old_layout() -> void:
	if not is_inside_tree():
		return
	if not DirAccess.dir_exists_absolute(ProjectSettings.globalize_path("res://GodotLuau/bin")):
		return   # aun no se instalo la estructura nueva
	# ¿Hay algo viejo que borrar? (si no, salir sin tocar el filesystem)
	var pending := false
	for old in ["res://bin", "res://icons", "res://DefaultScripts", "res://assets/avatars/r6"]:
		if DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(old)):
			pending = true
	if not pending:
		for f in ["RobloxAvatarR6.obj", "RobloxAvatarR6.mtl", "RobloxAvatarR6.obj.import",
				  "RobloxAvatarR15.obj", "RobloxAvatarR15.mtl", "RobloxAvatarR15.obj.import",
				  "Rig1_diff.png", "Rig1_diff.png.import"]:
			if FileAccess.file_exists(ProjectSettings.globalize_path("res://assets/avatars/" + f)):
				pending = true
				break
	if not pending and not _legacy_root_files().is_empty():
		pending = true
	if not pending and _hay_internos_en_raiz():
		pending = true
	if not pending:
		return
	# CRITICO: jamas borrar mientras el escaner del editor esta corriendo
	# (crashea el editor). Si esta escaneando, reintentar en unos segundos.
	var fs := EditorInterface.get_resource_filesystem()
	if fs == null:
		return
	if fs.is_scanning():
		get_tree().create_timer(2.0).timeout.connect(_migrate_old_layout)
		return
	var removed := false
	for old in ["res://bin", "res://icons", "res://DefaultScripts", "res://assets/avatars/r6"]:
		var g := ProjectSettings.globalize_path(old)
		if DirAccess.dir_exists_absolute(g):
			_remove_dir_recursive(g)
			removed = true
	# archivos sueltos del avatar viejo en res://assets/avatars/
	for f in ["RobloxAvatarR6.obj", "RobloxAvatarR6.mtl", "RobloxAvatarR6.obj.import",
			  "RobloxAvatarR15.obj", "RobloxAvatarR15.mtl", "RobloxAvatarR15.obj.import",
			  "Rig1_diff.png", "Rig1_diff.png.import"]:
		var p := ProjectSettings.globalize_path("res://assets/avatars/" + f)
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(p)
			removed = true
	if _borrar_restos_raiz():
		removed = true
	if _mover_internos_raiz():
		removed = true
	if removed:
		print("[GodotLuau] Migrated to the new res://GodotLuau/ layout (old folders removed).")
		if not fs.is_scanning():
			fs.scan()

# ── Restos de la estructura vieja SUELTOS EN LA RAIZ ──────────────────────────
# Hasta la 1.16 estos cinco vivian en res://. Cada uno solo se puede borrar si su
# equivalente dentro de GodotLuau/ YA existe: mientras no este, el de la raiz es
# el bueno y borrarlo perderia datos (los contadores de id sobre todo, que si se
# pierden hacen que los ids de script nuevos choquen con los que ya hay).
const PARES_MIGRACION := {
	"res://godot_luau.gdextension":     "res://GodotLuau/godot_luau.gdextension",
	"res://godot_luau.gdextension.uid": "res://GodotLuau/godot_luau.gdextension.uid",
	"res://Version":                    "res://GodotLuau/Version",
	"res://LICENSE":                    "res://GodotLuau/LICENSE",
	"res://.luau_ids.cfg":              "res://GodotLuau/.luau_ids.cfg",
}

func _legacy_root_files() -> Array:
	var out := []
	for viejo in PARES_MIGRACION:
		if FileAccess.file_exists(viejo) and FileAccess.file_exists(PARES_MIGRACION[viejo]):
			out.append(viejo)
	return out

func _borrar_restos_raiz() -> bool:
	# El .gdextension duplicado es el urgente de los cinco: con dos archivos
	# apuntando a la misma libreria, Godot la carga dos veces al arrancar y
	# registra las clases repetidas.
	var borrados := false
	for viejo in _legacy_root_files():
		DirAccess.remove_absolute(ProjectSettings.globalize_path(viejo))
		print("[GodotLuau] Removed leftover: " + viejo)
		borrados = true
	return borrados

# ── Archivos internos que la 1.16 dejaba en la raiz ───────────────────────────
# Desde la 1.17 viven en res://GodotLuau/. Los temporales del multijugador se
# borran sin mas (se rehacen al dar a Play). La configuracion que pone el usuario
# (.gl_host, gl_match.cfg) se mueve solo si en el sitio nuevo no hay otra; si la
# hay, se deja la vieja quieta (el C++ lee primero la nueva).
const TEMPORALES_RAIZ := ["res://.gl_mp_session", "res://.gl_view_cmd"]
const CONFIG_RAIZ := ["res://.gl_host", "res://gl_match.cfg"]

func _hay_internos_en_raiz() -> bool:
	for p in TEMPORALES_RAIZ:
		if FileAccess.file_exists(p): return true
	for p in CONFIG_RAIZ:
		if FileAccess.file_exists(p) and not FileAccess.file_exists(p.replace("res://", "res://GodotLuau/")):
			return true
	for p in [TRASH_DIR_LEGACY, BACKUP_DIR_LEGACY]:
		if DirAccess.dir_exists_absolute(ProjectSettings.globalize_path(p)): return true
	return false

func _mover_internos_raiz() -> bool:
	var hecho := false
	for p in TEMPORALES_RAIZ:
		if FileAccess.file_exists(p):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(p))
			hecho = true
	for p in CONFIG_RAIZ:
		var nuevo: String = p.replace("res://", "res://GodotLuau/")
		if FileAccess.file_exists(p) and not FileAccess.file_exists(nuevo):
			if DirAccess.rename_absolute(ProjectSettings.globalize_path(p), ProjectSettings.globalize_path(nuevo)) == OK:
				print("[GodotLuau] Moved %s to %s" % [p, nuevo])
				hecho = true
	# Papelera: se juntan las dos (un script borrado ayer sigue recuperable).
	var g_trash_viejo := ProjectSettings.globalize_path(TRASH_DIR_LEGACY)
	if DirAccess.dir_exists_absolute(g_trash_viejo):
		_fusionar_dir(g_trash_viejo, ProjectSettings.globalize_path(TRASH_DIR))
		hecho = true
	# Respaldo: NO se mezclan, serian archivos de dos versiones distintas y el
	# "Volver a vX" restauraria una mezcla. Si ya hay uno nuevo, el viejo sobra.
	var g_backup_viejo := ProjectSettings.globalize_path(BACKUP_DIR_LEGACY)
	if DirAccess.dir_exists_absolute(g_backup_viejo):
		var g_backup := ProjectSettings.globalize_path(BACKUP_DIR)
		if DirAccess.dir_exists_absolute(g_backup):
			_remove_dir_recursive(g_backup_viejo)
		else:
			DirAccess.rename_absolute(g_backup_viejo, g_backup)
		hecho = true
	return hecho

# Mueve todo lo de origen a destino sin pisar lo que ya haya alli, y borra origen.
func _fusionar_dir(origen: String, destino: String) -> void:
	DirAccess.make_dir_recursive_absolute(destino)
	var dir := DirAccess.open(origen)
	if dir == null: return
	dir.include_hidden = true   # el .gdignore, si no la carpeta no queda vacia
	for f in dir.get_files():
		var o := origen.path_join(f)
		var d := destino.path_join(f)
		if FileAccess.file_exists(d): DirAccess.remove_absolute(o)
		else: DirAccess.rename_absolute(o, d)
	for sub in dir.get_directories():
		_fusionar_dir(origen.path_join(sub), destino.path_join(sub))
	DirAccess.remove_absolute(origen)

func _remove_dir_recursive(g_path: String) -> void:
	var d := DirAccess.open(g_path)
	if d == null: return
	d.include_hidden = true
	d.list_dir_begin()
	var f := d.get_next()
	while f != "":
		if f != "." and f != "..":
			var full := g_path.path_join(f)
			if d.current_is_dir(): _remove_dir_recursive(full)
			else: DirAccess.remove_absolute(full)
		f = d.get_next()
	d.list_dir_end()
	DirAccess.remove_absolute(g_path)

# ── Settings registration ─────────────────────────────────────────────────────

func _register_settings() -> void:
	_add_bool("godot_luau/ai_autocomplete_enabled",    false,
		"AI Autocomplete — predicts code with the lightweight LuauIA-Mini model.")
	_add_bool("godot_luau/share_data_enabled",         true,
		"Collect anonymous local usage statistics to improve suggestion ranking.")
	_add_bool("godot_luau/script_output",              true,
		"Show print() and warn() output from your Luau scripts in the Output panel.")
	_add_bool("godot_luau/debug_mode",                 false,
		"Debug Mode — show internal output from built-in system scripts and engine messages.")
	_add_bool("godot_luau/debug_test_animation",       false,
		"TEST: play a placeholder code animation on the default character (idle/walk bob). For testing only, until real rigged animations are added.")
	_add_bool("godot_luau/notify_outdated_version",    true,
		"Print a warning in Output when running an outdated GodotLuau version.")
	_add_bool("godot_luau/instant_autocomplete",       true,
		"Trigger autocomplete suggestions instantly on every keystroke.")
	_add_bool("godot_luau/custom_autocomplete_enabled", false,
		"Use a custom JSON autocomplete list.")
	_add_str("godot_luau/custom_autocomplete_url",     "",
		"URL to download a custom autocomplete JSON from.")
	_add_str("godot_luau/ai_model_url",                "",
		"URL to download a custom AI model JSON from.")
	_add_str("godot_luau/ai_model_selected",           "mini",
		"Selected AI model id for AI Autocomplete (mini, plus, custom...).")

# Godot limita Jolt Physics a 10240 cuerpos por defecto: un mundo tipo Roblox
# (voxels, tycoons) lo revienta y las Parts extra quedan SIN colision aunque se
# rendericen. Subimos los limites en project.godot (Jolt los lee al arrancar el
# juego). Solo se sube, nunca se baja lo que el usuario haya puesto.
func _ensure_physics_limits() -> void:
	var limits := {
		"physics/jolt_physics_3d/limits/max_bodies": 262144,
		"physics/jolt_physics_3d/limits/max_body_pairs": 131072,
		"physics/jolt_physics_3d/limits/max_contact_constraints": 40960,
		"physics/jolt_physics_3d/limits/temporary_memory_buffer_size": 128,
		"rendering/rendering_device/d3d12/max_resource_descriptors": 131072,
	}
	var changed := false
	for key in limits:
		var cur := int(ProjectSettings.get_setting(key, 0))
		if cur < limits[key]:
			ProjectSettings.set_setting(key, limits[key])
			changed = true
	if changed:
		ProjectSettings.save()

# Reparto de luces de Forward+: Godot reserva sitio para 512 luces de cada tipo
# EN PANTALLA y borra ese bufer entero cada frame, haya las que haya; el shader
# de las piezas, ademas, recorre esas 512 casillas por pixel. Medido en Build a
# Mountain (UHD 770, 1457x711, 9 luces visibles): con 128, 0,23 ms menos de GPU
# por frame (8,25 -> 8,02). 128 de cada tipo a la vez en pantalla sobra para un
# juego de Roblox; si alguno tuviera mas, Godot dejaria de dibujar las que
# pasen del tope, asi que el valor se pone UNA vez (marca godot_luau/...) y si
# el usuario lo cambia despues a mano, se respeta. Va en project.godot porque
# el renderer se crea antes de cargar la extension.
func _ensure_render_limits() -> void:
	var marca := "godot_luau/render_limits_applied"
	if ProjectSettings.has_setting(marca) and bool(ProjectSettings.get_setting(marca)):
		return
	var key := "rendering/limits/cluster_builder/max_clustered_elements"
	if int(ProjectSettings.get_setting(key, 512)) == 512:
		ProjectSettings.set_setting(key, 128)
	ProjectSettings.set_setting(marca, true)
	ProjectSettings.save()

func _add_bool(key: String, val: bool, hint: String) -> void:
	if not ProjectSettings.has_setting(key): ProjectSettings.set_setting(key, val)
	ProjectSettings.set_initial_value(key, val)
	ProjectSettings.add_property_info({"name":key,"type":TYPE_BOOL,"hint":PROPERTY_HINT_NONE,"hint_string":hint})

func _add_str(key: String, val: String, hint: String) -> void:
	if not ProjectSettings.has_setting(key): ProjectSettings.set_setting(key, val)
	ProjectSettings.set_initial_value(key, val)
	ProjectSettings.add_property_info({"name":key,"type":TYPE_STRING,"hint":PROPERTY_HINT_NONE,"hint_string":hint})
