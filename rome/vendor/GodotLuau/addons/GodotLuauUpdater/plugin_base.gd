@tool
extends EditorPlugin

# Plugin de GodotLuau, parte 1 de 5: constantes, textos (EN/ES/PT),
# estado, idioma, colores del tema, tamaño de letra y avisos.
# El plugin esta partido en una cadena de herencia por zonas (pasaba de
# 2.000 lineas): plugin.gd -> plugin_ia.gd -> plugin_actualizador.gd ->
# plugin_editor.gd -> plugin_base.gd -> EditorPlugin. Cada parte solo
# puede llamar a lo suyo y a lo de las partes de abajo.

const VERSION_URL    := "https://raw.githubusercontent.com/Pimpoli/GodotLuau/main/Version"
const ZIP_URL        := "https://raw.githubusercontent.com/Pimpoli/GodotLuau/main/GodotLuau.zip"
const HASH_URL       := "https://raw.githubusercontent.com/Pimpoli/GodotLuau/main/GodotLuau.zip.sha256"
# Desde la 1.17 TODO lo de la extension vive en res://GodotLuau/ para que el
# proyecto del usuario solo muestre esa carpeta (mas res://addons/, que Godot
# obliga a que este ahi para descubrir el plugin de editor).
const VERSION_FILE   := "res://GodotLuau/Version"
# Sitios de la 1.16 y anteriores. Se leen para migrar y se borran cuando el
# archivo nuevo ya existe (ver _migrate_old_layout).
const VERSION_FILE_LEGACY := "res://Version"
const GDEXT_LEGACY        := "res://godot_luau.gdextension"
const IDS_LEGACY          := "res://.luau_ids.cfg"
const TRASH_DIR_LEGACY    := "res://.luau_trash"
const BACKUP_DIR_LEGACY   := "res://.luau_backup"
const GITHUB_URL     := "https://github.com/Pimpoli/GodotLuau"
# Historial de versiones: cada tag del repo lleva su GodotLuau.zip commiteado,
# asi que se puede instalar CUALQUIER version publicada, no solo la ultima ni el
# respaldo local (estilo lanzador de Minecraft).
const TAGS_URL       := "https://api.github.com/repos/Pimpoli/GodotLuau/tags?per_page=100"
const RAW_BASE       := "https://raw.githubusercontent.com/Pimpoli/GodotLuau"
# Se listan los COMMITS que tocaron el archivo Version, no los tags: hay ~100
# frente a 13, asi salen TODAS las versiones publicadas (incluidas las que nunca
# llegaron a tener tag, como las 1.15). Un SHA sirve de referencia para bajar su
# GodotLuau.zip exactamente igual que un tag.
const COMMITS_URL    := "https://api.github.com/repos/Pimpoli/GodotLuau/commits?path=Version&per_page=100"
const VER_MAX_PAGES  := 4
const DATA_FILE      := "user://godotluau_usage.json"
const CUSTOM_AC_FILE := "user://godotluau_custom_autocomplete.json"
const DLL_EXTENSIONS := ["dll", "so", "dylib", "framework"]
const WM_META_KEY    := "_godotluau_wm"
const TRASH_DIR      := "res://GodotLuau/.luau_trash"
const BACKUP_DIR     := "res://GodotLuau/.luau_backup"   # respaldo de la version anterior (rollback)
const SCRIPT_NODE_CLASSES := ["LocalScript", "ServerScript", "ModuleScript"]

# ── Catálogo de modelos de IA para el autocompletado ─────────────────────────
# params: nº de pesos del modelo — más parámetros = mejor predicción,
# pero puede tardar un poco más. Familia LuauIA (cada nivel ≈ x2.5).
const AI_MODELS := [
	{"id": "mini",    "name": "LuauIA-Mini",    "params": "10.000",
	 "url": "https://github.com/Pimpoli/GodotLuau/raw/main/models/LuauIA-Mini.json"},
	{"id": "medium",  "name": "LuauIA-Medium",  "params": "25.000",
	 "url": "https://github.com/Pimpoli/GodotLuau/raw/main/models/LuauIA-Medium.json"},
	{"id": "high",    "name": "LuauIA-High",    "params": "62.500",
	 "url": "https://github.com/Pimpoli/GodotLuau/raw/main/models/LuauIA-High.json"},
	{"id": "highpro", "name": "LuauIA-HighPRO", "params": "~96.000",
	 "url": "https://github.com/Pimpoli/GodotLuau/raw/main/models/LuauIA-HighPRO.json"},
	{"id": "custom",  "name": "Personalizado / Custom", "params": "?", "custom": true},
]

# ── Translation dictionary — EN / ES / PT-BR ─────────────────────────────────
const TR := {
	"en": {
		"panel_title":           "GodotLuau Config",
		"sec_autocomplete":      "🤖  Autocomplete",
		"sec_data":              "📊  Usage Data",
		"sec_debug":             "🔧  Debug",
		"sec_updates":           "🔄  Updates",
		"sec_appearance":        "🎨  Appearance",
		"sec_brand":             "🖼  Branding",
		"sec_import":            "📦  Import .rbxl / .gdlu",
		"brand_logo":            "Game logo",
		"brand_desc":            "Shown in the Escape menu; once you pick your own, it is also the window icon. Any PNG/JPG works; it is copied into the project.",
		"brand_pick":            "Choose image…",
		"brand_reset":           "Restore default",
		"brand_ok":              "Logo updated.",
		"brand_reset_ok":        "Default logo restored.",
		"brand_fail":            "Could not read that image.",
		"import_desc":           "Pick a .rbxl and it becomes a scene in this project, with its parts, GUIs and scripts.",
		"import_pick":           "Choose .rbxl…",
		"font_size_label":       "Panel text size",
		"ai_title":              "AI Autocomplete",
		"gia_title":             "GodotLuau AI",
		"gia_dl_title":          "Engine and models (optional)",
		"gia_convert":           "Convert selected script",
		"gia_engine":            "AI engine (local chat)",
		"gia_installed":         "installed",
		"gia_not_installed":     "not downloaded",
		"gia_download":          "Download",
		"gia_update":            "Update",
		"gia_cancel":            "Cancel",
		"gia_note":              "The reviews above never need any of this. Only the chat does. Everything runs on your PC — nothing is sent anywhere.",
		"gia_desc":              "Tells you which lines of your Roblox scripts will NOT work here, before running anything. Instant and offline: it parses your code with the same Luau the game runs and checks it against what this engine really implements.",
		"gia_open":              "Open Chat",
		"gia_scan_project":      "Review my whole project",
		"gia_scan_scene":        "Review the open scene",
		"gia_ready":             "Ready — no model needed.",
		"gia_working":           "Analysing…",
		"gia_clean":             "✅ Nothing to report: every symbol used is supported.",
		"gia_found":             "%d issue(s): %d serious, %d notice(s) — in %d ms",
		"ai_desc":               "Predicts your next code using the AI model selected below.\nBigger models predict better but may take longer.\nOnly one of AI / Instant Autocomplete can be active.",
		"aim_select_label":      "AI model:",
		"aim_params_suffix":     "parameters",
		"aim_btn_dl":            "Download ⏬",
		"aim_builtin_note":      "Built-in — always ready, instant.",
		"aim_missing":           "Not downloaded yet — press Download ⏬",
		"aim_need_download":     "⚠ Download the selected AI model first",
		"aim_header":            "Custom AI model",
		"aim_note":              "Load your own model (JSON with \"bigrams\") from a file or URL.",
		"aim_status_none":       "Using built-in model \"LuauIA-Mini\".",
		"aim_status_ok":         "✅ Custom model loaded (%d entries).",
		"aim_status_err":        "❌ Download failed — check the URL.",
		"aim_status_bad":        "❌ Invalid model (expected JSON with \"bigrams\").",
		"share_title":           "Share anonymous usage data",
		"share_desc":            "Collects anonymous LOCAL statistics about which APIs you use most\nto improve suggestion ranking. Your code is never stored or shared.\nEnabled by default — you can turn it off anytime.",
		"debug_title":           "Script output (print / warn)",
		"debug_desc":            "Shows print() and warn() messages from YOUR Luau scripts in the Output panel.\nEnabled by default — turn OFF for a clean Output.",
		"dbg_title":             "Debug Mode (system scripts)",
		"anim_title":            "Test character animation",
		"anim_desc":             "TEST ONLY: plays a placeholder code animation (idle/walk bob) on the default character. Off by default; real animations come from rigged models.",
		"dbg_desc":              "Shows internal output from built-in scripts (PlayerModule, Health, Chat...)\nand engine messages. Only the GodotLuau banner is shown when OFF.",
		"notif_outdated_title":  "Notify outdated version in Output",
		"notif_outdated_desc":   "Prints a warning in the Output panel when running an outdated GodotLuau version.",
		"speed_title":           "Instant Autocomplete",
		"speed_desc":            "Triggers suggestions on every keystroke without waiting.\nSets the editor completion delay to near-zero.",
		"cac_header":            "Custom Autocomplete",
		"cac_toggle_title":      "Use custom autocomplete",
		"cac_toggle_desc":       "Replace or extend built-in suggestions with your own JSON API list.\nLoad from a local file or a GitHub raw URL.",
		"cac_url_label":         "Source URL (GitHub raw or direct link):",
		"cac_url_hint":          "https://raw.githubusercontent.com/.../autocomplete.json",
		"cac_btn_download":      "⬇ Download",
		"cac_btn_import":        "📂 Import JSON",
		"cac_btn_clear":         "🗑 Clear",
		"cac_status_none":       "No custom autocomplete loaded.",
		"cac_status_ok":         "✅ %d custom items loaded.",
		"cac_status_dl":         "⏳ Downloading...",
		"cac_status_err":        "❌ Download failed — check the URL.",
		"cac_status_bad_json":   "❌ Invalid JSON format (expected an array).",
		"stats_header":          "Collected data",
		"stats_none":            "No data collected yet.",
		"stats_size":            "Size: %.1f KB  |  Entries: ~%d",
		"stats_err":             "Could not read data file.",
		"btn_refresh":           "↺ Refresh",
		"btn_open":              "📂 Open folder",
		"btn_clear":             "🗑 Delete data",
		"dlg_clear_title":       "Delete usage data",
		"dlg_clear_text":        "Delete all collected usage data?\nThis does not affect plugin functionality.",
		"bar_checking":          "🔍 Checking...",
		"bar_uptodate":          "✅ %s — up to date.",
		"bar_newver":            "%s  →  %s",
		"bar_btn_update":        "⬇ Update",
		"bar_downloading":       "⬇ Downloading %s...",
		"bar_dl_failed":         "❌ Download failed (code %d).",
		"bar_extracting":        "📦 Extracting...",
		"bar_ok_no_restart":     "✅ %s installed.",
		"bar_ok_restart":        "✅ Updated — click to restart.",
		"bar_restart_in":        "✅ Updated — restarting in %d s (Godot will reopen by itself)",
		"bar_apply_close":       "✅ Ready. Godot will close and apply the DLL.",
		"btn_rollback":          "↺ Back to %s",
		"bar_rollback_done":     "✅ Restored %s. Restart to apply.",
		"btn_versions":          "🗂 Versions",
		"bar_versions_loading":  "🕐 Loading version list...",
		"bar_versions_err":      "❌ Could not fetch the version list",
		"bar_installing_ver":    "⬇ Installing %s...",
		"menu_versions_title":   "Install another version",
		"menu_versions_current": "%s  (installed)",
		"bar_restart":           "🔄 Restart editor",
		"bar_restarting":        "🔄 Restarting...",
		"bar_data_cleared":      "🗑 Data deleted.",
		"bar_dl_err":            "❌ Could not start download.",
		"bar_retry":             "Retry",
		"bar_script_err":        "❌ Could not write update script.",
		"bar_verifying":         "🔒 Verifying download...",
		"bar_hash_err":          "❌ Integrity check failed — update aborted.",
		"ver_unknown":           "unknown",
		"btn_check_ver":         "🔍",
		"btn_reinstall":         "↺ Reinstall",
		"bar_reinstalling":      "↺ Reinstalling %s...",
		"github_label":          "Official source:",
		"footer_data":           "Data file: user://godotluau_usage.json",
		"warn_watermark":        "⚠ GodotLuau core not detected (DLL missing or failed to load). If you compiled it yourself, ignore this. Otherwise reinstall from: " + GITHUB_URL,
	},
	"es": {
		"panel_title":           "GodotLuau Config",
		"sec_autocomplete":      "🤖  Autocompletado",
		"sec_data":              "📊  Datos de Uso",
		"sec_debug":             "🔧  Debug",
		"sec_updates":           "🔄  Actualizaciones",
		"sec_appearance":        "🎨  Apariencia",
		"sec_brand":             "🖼  Marca",
		"sec_import":            "📦  Importar .rbxl / .gdlu",
		"brand_logo":            "Logo del juego",
		"brand_desc":            "Sale en el menú de Escape; si eliges uno tuyo, también es el icono de la ventana. Vale cualquier PNG/JPG; se copia dentro del proyecto.",
		"brand_pick":            "Elegir imagen…",
		"brand_reset":           "Volver a la de fábrica",
		"brand_ok":              "Logo cambiado.",
		"brand_reset_ok":        "Logo de fábrica restaurado.",
		"brand_fail":            "No se pudo leer esa imagen.",
		"import_desc":           "Elige un .rbxl y se convierte en una escena de este proyecto, con sus piezas, interfaces y scripts.",
		"import_pick":           "Elegir .rbxl…",
		"font_size_label":       "Tamaño del texto del panel",
		"ai_title":              "Autocompletado con IA",
		"gia_title":             "GodotLuau IA",
		"gia_dl_title":          "Motor y modelos (opcional)",
		"gia_convert":           "Convertir el script seleccionado",
		"gia_engine":            "Motor de IA (chat local)",
		"gia_installed":         "instalado",
		"gia_not_installed":     "sin descargar",
		"gia_download":          "Descargar",
		"gia_update":            "Actualizar",
		"gia_cancel":            "Cancelar",
		"gia_note":              "Las revisiones de arriba no necesitan nada de esto. Solo el chat. Todo corre en tu PC: no se envía nada a ningún sitio.",
		"gia_desc":              "Te dice qué líneas de tus scripts de Roblox NO van a funcionar aquí, antes de ejecutar nada. Instantáneo y sin conexión: analiza tu código con el mismo Luau que corre el juego y lo cruza con lo que este motor implementa de verdad.",
		"gia_open":              "Abrir Chat",
		"gia_scan_project":      "Revisar todo mi proyecto",
		"gia_scan_scene":        "Revisar la escena abierta",
		"gia_ready":             "Listo — no hace falta ningún modelo.",
		"gia_working":           "Analizando…",
		"gia_clean":             "✅ Nada que reportar: todo lo que usas está soportado.",
		"gia_found":             "%d hallazgo(s): %d graves, %d aviso(s) — en %d ms",
		"ai_desc":               "Predice tu siguiente código usando el modelo de IA elegido abajo.\nLos modelos más grandes predicen mejor pero pueden tardar más.\nSolo uno de IA / Autocompletado Instantáneo puede estar activo.",
		"aim_select_label":      "Modelo de IA:",
		"aim_params_suffix":     "parámetros",
		"aim_btn_dl":            "Descargar ⏬",
		"aim_builtin_note":      "Integrado — siempre listo, instantáneo.",
		"aim_missing":           "Aún no descargado — pulsa Descargar ⏬",
		"aim_need_download":     "⚠ Descarga primero el modelo de IA seleccionado",
		"aim_header":            "Modelo de IA personalizado",
		"aim_note":              "Carga tu propio modelo (JSON con \"bigrams\") desde archivo o URL.",
		"aim_status_none":       "Usando el modelo integrado \"LuauIA-Mini\".",
		"aim_status_ok":         "✅ Modelo personalizado cargado (%d entradas).",
		"aim_status_err":        "❌ Descarga fallida — verifica la URL.",
		"aim_status_bad":        "❌ Modelo inválido (se esperaba JSON con \"bigrams\").",
		"share_title":           "Compartir datos de uso anónimos",
		"share_desc":            "Recolecta estadísticas anónimas LOCALES de qué APIs usas más\npara mejorar el orden de las sugerencias. Tu código nunca se guarda ni se comparte.\nActivado por defecto — puedes desactivarlo cuando quieras.",
		"debug_title":           "Salida de scripts (print / warn)",
		"debug_desc":            "Muestra los print() y warn() de TUS scripts Luau en el panel Output.\nActivado por defecto — desactívalo para mantener el Output limpio.",
		"dbg_title":             "Modo Debug (scripts del sistema)",
		"anim_title":            "Animación de prueba del personaje",
		"anim_desc":             "SOLO PRUEBA: reproduce una animación por código (rebote al estar quieto/caminar) en el personaje por defecto. Apagado por defecto; las animaciones reales vienen de modelos riggeados.",
		"dbg_desc":              "Muestra la salida interna de los scripts integrados (PlayerModule, Health, Chat...)\ny mensajes del motor. Desactivado solo se ve el banner de GodotLuau.",
		"notif_outdated_title":  "Notificar versión desactualizada en Output",
		"notif_outdated_desc":   "Imprime una advertencia en el panel Output cuando se ejecuta una versión desactualizada de GodotLuau.",
		"speed_title":           "Autocompletado Instantáneo",
		"speed_desc":            "Activa sugerencias con cada tecla, sin esperar.\nEstablece el retraso del editor a casi cero.",
		"cac_header":            "Autocompletado Personalizado",
		"cac_toggle_title":      "Usar autocompletado personalizado",
		"cac_toggle_desc":       "Reemplaza o extiende las sugerencias incorporadas con tu propia lista JSON.\nCarga desde un archivo local o una URL raw de GitHub.",
		"cac_url_label":         "URL de origen (GitHub raw o enlace directo):",
		"cac_url_hint":          "https://raw.githubusercontent.com/.../autocomplete.json",
		"cac_btn_download":      "⬇ Descargar",
		"cac_btn_import":        "📂 Importar JSON",
		"cac_btn_clear":         "🗑 Limpiar",
		"cac_status_none":       "Sin autocompletado personalizado cargado.",
		"cac_status_ok":         "✅ %d entradas personalizadas cargadas.",
		"cac_status_dl":         "⏳ Descargando...",
		"cac_status_err":        "❌ Descarga fallida — verifica la URL.",
		"cac_status_bad_json":   "❌ Formato JSON inválido (se esperaba un array).",
		"stats_header":          "Datos recolectados",
		"stats_none":            "Sin datos recolectados todavía.",
		"stats_size":            "Tamaño: %.1f KB  |  Entradas: ~%d",
		"stats_err":             "No se pudo leer el archivo de datos.",
		"btn_refresh":           "↺ Actualizar",
		"btn_open":              "📂 Abrir carpeta",
		"btn_clear":             "🗑 Borrar datos",
		"dlg_clear_title":       "Borrar datos de uso",
		"dlg_clear_text":        "¿Eliminar todos los datos de uso recolectados?\nEsto no afecta el funcionamiento del plugin.",
		"bar_checking":          "🔍 Verificando...",
		"bar_uptodate":          "✅ %s — al día.",
		"bar_newver":            "%s  →  %s",
		"bar_btn_update":        "⬇ Actualizar",
		"bar_downloading":       "⬇ Descargando %s...",
		"bar_dl_failed":         "❌ Descarga fallida (código %d).",
		"bar_extracting":        "📦 Extrayendo...",
		"bar_ok_no_restart":     "✅ %s instalado.",
		"bar_ok_restart":        "✅ Actualizado — clic para reiniciar.",
		"bar_restart_in":        "✅ Actualizado — reiniciando en %d s (Godot se reabrirá solo)",
		"bar_apply_close":       "✅ Listo. Godot se cerrará y aplicará la DLL.",
		"btn_rollback":          "↺ Volver a %s",
		"bar_rollback_done":     "✅ Restaurado %s. Reinicia para aplicar.",
		"btn_versions":          "🗂 Versiones",
		"bar_versions_loading":  "🕐 Cargando lista de versiones...",
		"bar_versions_err":      "❌ No se pudo obtener la lista de versiones",
		"bar_installing_ver":    "⬇ Instalando %s...",
		"menu_versions_title":   "Instalar otra version",
		"menu_versions_current": "%s  (instalada)",
		"bar_restart":           "🔄 Reiniciar editor",
		"bar_restarting":        "🔄 Reiniciando...",
		"bar_data_cleared":      "🗑 Datos eliminados.",
		"bar_dl_err":            "❌ No se pudo iniciar la descarga.",
		"bar_retry":             "Reintentar",
		"bar_script_err":        "❌ No se pudo escribir el script de actualización.",
		"bar_verifying":         "🔒 Verificando descarga...",
		"bar_hash_err":          "❌ Falló la verificación de integridad — actualización cancelada.",
		"ver_unknown":           "desconocida",
		"btn_check_ver":         "🔍",
		"btn_reinstall":         "↺ Reinstalar",
		"bar_reinstalling":      "↺ Reinstalando %s...",
		"github_label":          "Fuente oficial:",
		"footer_data":           "Archivo de datos: user://godotluau_usage.json",
		"warn_watermark":        "⚠ Núcleo de GodotLuau no detectado (DLL ausente o no cargó). Si lo compilaste tú mismo, ignora esto. Si no, reinstala desde: " + GITHUB_URL,
	},
	"pt": {
		"panel_title":           "GodotLuau Config",
		"sec_autocomplete":      "🤖  Autocompletar",
		"sec_data":              "📊  Dados de Uso",
		"sec_debug":             "🔧  Debug",
		"sec_updates":           "🔄  Atualizações",
		"sec_appearance":        "🎨  Aparência",
		"sec_brand":             "🖼  Marca",
		"sec_import":            "📦  Importar .rbxl / .gdlu",
		"brand_logo":            "Logo do jogo",
		"brand_desc":            "Aparece no menu de Escape; se escolher um seu, também é o ícone da janela. Vale qualquer PNG/JPG; é copiado para dentro do projeto.",
		"brand_pick":            "Escolher imagem…",
		"brand_reset":           "Voltar ao padrão",
		"brand_ok":              "Logo alterado.",
		"brand_reset_ok":        "Logo padrão restaurado.",
		"brand_fail":            "Não foi possível ler essa imagem.",
		"import_desc":           "Escolha um .rbxl e ele vira uma cena deste projeto, com peças, interfaces e scripts.",
		"import_pick":           "Escolher .rbxl…",
		"font_size_label":       "Tamanho do texto do painel",
		"ai_title":              "Autocompletar com IA",
		"gia_title":             "GodotLuau IA",
		"gia_desc":              "Diz quais linhas dos seus scripts de Roblox NÃO vão funcionar aqui, antes de executar nada. Instantâneo e offline.",
		"gia_open":              "Abrir Chat",
		"gia_scan_project":      "Revisar todo o meu projeto",
		"gia_scan_scene":        "Revisar a cena aberta",
		"gia_ready":             "Pronto — não precisa de modelo.",
		"gia_working":           "Analisando…",
		"gia_clean":             "✅ Nada a reportar.",
		"gia_found":             "%d achado(s): %d graves, %d aviso(s) — em %d ms",
		"ai_desc":               "Prevê seu próximo código usando o modelo de IA escolhido abaixo.\nModelos maiores preveem melhor mas podem demorar mais.\nApenas um de IA / Autocompletar Instantâneo pode estar ativo.",
		"aim_select_label":      "Modelo de IA:",
		"aim_params_suffix":     "parâmetros",
		"aim_btn_dl":            "Baixar ⏬",
		"aim_builtin_note":      "Integrado — sempre pronto, instantâneo.",
		"aim_missing":           "Ainda não baixado — clique em Baixar ⏬",
		"aim_need_download":     "⚠ Baixe primeiro o modelo de IA selecionado",
		"aim_header":            "Modelo de IA personalizado",
		"aim_note":              "Carregue seu próprio modelo (JSON com \"bigrams\") de um arquivo ou URL.",
		"aim_status_none":       "Usando o modelo integrado \"LuauIA-Mini\".",
		"aim_status_ok":         "✅ Modelo personalizado carregado (%d entradas).",
		"aim_status_err":        "❌ Download falhou — verifique a URL.",
		"aim_status_bad":        "❌ Modelo inválido (era esperado JSON com \"bigrams\").",
		"share_title":           "Compartilhar dados de uso anônimos",
		"share_desc":            "Coleta estatísticas anônimas LOCAIS de quais APIs você mais usa\npara melhorar a ordem das sugestões. Seu código nunca é salvo ou compartilhado.\nAtivado por padrão — você pode desativar quando quiser.",
		"debug_title":           "Saída de scripts (print / warn)",
		"debug_desc":            "Exibe os print() e warn() dos SEUS scripts Luau no painel Output.\nAtivado por padrão — desative para manter o Output limpo.",
		"dbg_title":             "Modo Debug (scripts do sistema)",
		"anim_title":            "Animação de teste do personagem",
		"anim_desc":             "SÓ TESTE: reproduz uma animação por código (quicar parado/andando) no personagem padrão. Desligado por padrão; as animações reais vêm de modelos com esqueleto.",
		"dbg_desc":              "Exibe a saída interna dos scripts integrados (PlayerModule, Health, Chat...)\ne mensagens do motor. Desativado, apenas o banner do GodotLuau aparece.",
		"notif_outdated_title":  "Notificar versão desatualizada no Output",
		"notif_outdated_desc":   "Exibe um aviso no painel Output quando uma versão desatualizada do GodotLuau está em uso.",
		"speed_title":           "Autocompletar Instantâneo",
		"speed_desc":            "Ativa sugestões a cada tecla, sem esperar.\nDefine o atraso do editor para quase zero.",
		"cac_header":            "Autocompletar Personalizado",
		"cac_toggle_title":      "Usar autocompletar personalizado",
		"cac_toggle_desc":       "Substitua ou estenda as sugestões embutidas com sua própria lista JSON.\nCarregue de um arquivo local ou de uma URL raw do GitHub.",
		"cac_url_label":         "URL de origem (GitHub raw ou link direto):",
		"cac_url_hint":          "https://raw.githubusercontent.com/.../autocomplete.json",
		"cac_btn_download":      "⬇ Baixar",
		"cac_btn_import":        "📂 Importar JSON",
		"cac_btn_clear":         "🗑 Limpar",
		"cac_status_none":       "Nenhum autocompletar personalizado carregado.",
		"cac_status_ok":         "✅ %d itens personalizados carregados.",
		"cac_status_dl":         "⏳ Baixando...",
		"cac_status_err":        "❌ Download falhou — verifique a URL.",
		"cac_status_bad_json":   "❌ Formato JSON inválido (era esperado um array).",
		"stats_header":          "Dados coletados",
		"stats_none":            "Nenhum dado coletado ainda.",
		"stats_size":            "Tamanho: %.1f KB  |  Entradas: ~%d",
		"stats_err":             "Não foi possível ler o arquivo de dados.",
		"btn_refresh":           "↺ Atualizar",
		"btn_open":              "📂 Abrir pasta",
		"btn_clear":             "🗑 Apagar dados",
		"dlg_clear_title":       "Apagar dados de uso",
		"dlg_clear_text":        "Excluir todos os dados de uso coletados?\nIsso não afeta a funcionalidade do plugin.",
		"bar_checking":          "🔍 Verificando...",
		"bar_uptodate":          "✅ %s — atualizado.",
		"bar_newver":            "%s  →  %s",
		"bar_btn_update":        "⬇ Atualizar",
		"bar_downloading":       "⬇ Baixando %s...",
		"bar_dl_failed":         "❌ Download falhou (código %d).",
		"bar_extracting":        "📦 Extraindo...",
		"bar_ok_no_restart":     "✅ %s instalado.",
		"bar_ok_restart":        "✅ Atualizado — clique para reiniciar.",
		"bar_restart_in":        "✅ Atualizado — reiniciando em %d s (o Godot reabrirá sozinho)",
		"bar_apply_close":       "✅ Pronto. O Godot fechará e aplicará a DLL.",
		"btn_rollback":          "↺ Voltar para %s",
		"bar_rollback_done":     "✅ Restaurado %s. Reinicie para aplicar.",
		"btn_versions":          "🗂 Versoes",
		"bar_versions_loading":  "🕐 Carregando lista de versoes...",
		"bar_versions_err":      "❌ Nao foi possivel obter a lista de versoes",
		"bar_installing_ver":    "⬇ Instalando %s...",
		"menu_versions_title":   "Instalar outra versao",
		"menu_versions_current": "%s  (instalada)",
		"bar_restart":           "🔄 Reiniciar editor",
		"bar_restarting":        "🔄 Reiniciando...",
		"bar_data_cleared":      "🗑 Dados excluídos.",
		"bar_dl_err":            "❌ Não foi possível iniciar o download.",
		"bar_retry":             "Tentar novamente",
		"bar_script_err":        "❌ Não foi possível escrever o script de atualização.",
		"bar_verifying":         "🔒 Verificando download...",
		"bar_hash_err":          "❌ Falha na verificação de integridade — atualização cancelada.",
		"ver_unknown":           "desconhecida",
		"btn_check_ver":         "🔍",
		"btn_reinstall":         "↺ Reinstalar",
		"bar_reinstalling":      "↺ Reinstalando %s...",
		"github_label":          "Fonte oficial:",
		"footer_data":           "Arquivo de dados: user://godotluau_usage.json",
		"warn_watermark":        "⚠ Núcleo do GodotLuau não detectado (DLL ausente ou não carregou). Se você mesmo compilou, ignore isto. Caso contrário, reinstale em: " + GITHUB_URL,
	}
}

# ── State ─────────────────────────────────────────────────────────────────────
var _http_version      : HTTPRequest    = null
var _http_download     : HTTPRequest    = null
var _http_autocomplete : HTTPRequest    = null
var _http_hash         : HTTPRequest    = null
var _http_aimodel      : HTTPRequest    = null
var _bar_action        : String         = ""
var _remote_version    : String         = ""
var _downloading       : bool           = false
var _settings_panel    : Control        = null
var _stats_label       : Label          = null
var _cac_status        : Label          = null

# ── GodotLuau IA (Capa 0: analizador de compatibilidad) ──────────────────────
var _gia_status : Label = null
var _gia_win    : Window = null
var _gia_log    : RichTextLabel = null
var _gia_entrada: LineEdit = null
var _gia_enviar : Button = null
var _gia_nivel  : OptionButton = null
var _gia_ia     : Node = null          # GLLocalAI (extension opcional)
var _gia_cargando := false
var _gia_ult_analisis : Array = []     # ultimo informe, para dar contexto al chat
# -- Descargador (motor de IA + modelos) --------------------------------------
var _gia_dl        : HTTPRequest = null
var _gia_dl_timer  : Timer       = null
var _gia_dl_que    : String      = ""
var _gia_dl_destino: String      = ""
var _gia_dl_bar    : ProgressBar = null
var _gia_dl_txt    : Label       = null
var _gia_dl_cancel : Button      = null
var _gia_dl_t0     : int         = 0
var _gia_dl_total  : int         = 0   # tamaño esperado (del catalogo, en 64 bits)
# -- Ventana del chat ---------------------------------------------------------
var _gia_fantasma : Label = null       # el texto gris que completa con Tab
var _gia_chips    : HBoxContainer = null
var _gia_estado_l : Label = null
var _gia_sug_actual := ""              # la sugerencia viva (lo que acepta Tab)
# -- Capa 2: reescritura verificada -------------------------------------------
var _gia_c2_cola    : Array = []       # hallazgos pendientes de arreglar
var _gia_c2_i       : int   = 0
var _gia_c2_fuente  : String = ""      # codigo original del script
var _gia_c2_nombre  : String = ""
var _gia_c2_ruta    : String = ""      # de donde salio (para poder guardar)
var _gia_c2_parches : Array = []       # los que pasaron la verificacion
var _gia_c2_intento : int   = 0
var _gia_c2_activo  : bool  = false
var _cac_url_field     : LineEdit       = null
var _aim_status        : Label          = null
var _aim_url_field     : LineEdit       = null
var _lang              : String         = "en"
var _font_scale        : float          = 1.0
var _font_pct          : int            = 50
var _first_build       : bool           = true
var _ver_label         : Label          = null
var _ver_btn           : Button         = null
var _reinstall_btn     : Button         = null
var _rollback_btn      : Button         = null
# ── Historial de versiones ────────────────────────────────────────────────────
var _versions_btn      : Button         = null
var _versions_menu     : PopupMenu      = null
var _http_tags         : HTTPRequest    = null
var _http_tagver       : HTTPRequest    = null
var _tag_list          : Array          = []   # [{ "tag": String, "ver": String }]
var _ver_raw           : Array          = []   # commits crudos mientras se pagina
var _ver_page          : int            = 1
var _pending_tag       : String         = ""
# Cuando NO esta vacio, la descarga usa esta URL en vez de la ultima version.
var _zip_url_override  : String         = ""
var _notif_bar         : PanelContainer = null
var _notif_label       : Label          = null
var _outdated_timer    : Timer          = null
var _suppress_trash_frame : int         = 0

# Colores adaptados al tema del editor (contraste correcto en claro y oscuro)
var _col_text  := Color(0.95, 0.95, 0.95)
var _col_dim   := Color(0.74, 0.74, 0.76)
var _col_faint := Color(0.55, 0.55, 0.58)
var _col_card  := Color(0.12, 0.14, 0.18, 0.9)
var _is_dark_theme := true

# ── Language ──────────────────────────────────────────────────────────────────

func _detect_editor_lang() -> void:
	var es := EditorInterface.get_editor_settings()
	# Preferir el idioma elegido por el usuario en el panel (persistente)
	if es.has_setting("godot_luau/lang"):
		var saved := str(es.get_setting("godot_luau/lang"))
		if saved in ["en", "es", "pt"]:
			_lang = saved
			return
	# Por defecto: ingles (internacional). El usuario puede cambiar a es/pt
	# desde el selector del panel y queda guardado en godot_luau/lang.
	_lang = "en"

func _t(key: String) -> String:
	var d : Dictionary = TR.get(_lang, TR["en"])
	return d.get(key, (TR["en"] as Dictionary).get(key, key))

# ── Colores según el tema del editor ─────────────────────────────────────────
# El texto usa el color contrario al fondo del motor para máxima legibilidad.

func _compute_theme_colors() -> void:
	var base : Color = Color(0.21, 0.24, 0.29)
	var es := EditorInterface.get_editor_settings()
	if es.has_setting("interface/theme/base_color"):
		base = es.get_setting("interface/theme/base_color")
	_is_dark_theme = base.get_luminance() < 0.5
	if _is_dark_theme:
		_col_text  = Color(0.96, 0.96, 0.97)
		_col_dim   = Color(0.76, 0.76, 0.79)
		_col_faint = Color(0.56, 0.56, 0.60)
		_col_card  = Color(0.10, 0.12, 0.16, 0.92)
	else:
		_col_text  = Color(0.09, 0.09, 0.11)
		_col_dim   = Color(0.27, 0.27, 0.31)
		_col_faint = Color(0.42, 0.42, 0.46)
		_col_card  = Color(0.93, 0.94, 0.96, 0.92)

# Acentos: en tema claro se oscurecen para que sigan leyéndose bien
func _accent(c: Color) -> Color:
	return c if _is_dark_theme else c.darkened(0.35)

# ── Font preference (per-user, stored in EditorSettings) ─────────────────────
# Escala 0-100 estilo barra de volumen; 50 = tamaño normal.

func _load_font_pref() -> void:
	var es := EditorInterface.get_editor_settings()
	var pct := 50
	if es.has_setting("godot_luau/panel_font_scale_pct"):
		pct = int(es.get_setting("godot_luau/panel_font_scale_pct"))
	elif es.has_setting("godot_luau/panel_font_scale"):
		# Migración desde el viejo selector S/M/L
		pct = [30, 50, 80][clampi(int(es.get_setting("godot_luau/panel_font_scale")), 0, 2)]
	_apply_font_pct(pct)

func _apply_font_pct(pct: int) -> void:
	_font_pct   = clampi(pct, 0, 100)
	_font_scale = 0.85 + (_font_pct / 100.0) * 0.45   # 0.85x – 1.30x

func _fs(base: int) -> int:
	return max(9, int(base * _font_scale))

# Mensaje corto en la barra del panel (verde si va bien, rojo si no).
func _notify(msg: String, color: Color) -> void:
	if not (_notif_bar and is_instance_valid(_notif_bar)): 
		print("[GodotLuau] ", msg)
		return
	_notif_label.text = msg
	_notif_label.add_theme_color_override("font_color", color)
	_notif_bar.visible = true

# ── Usage stats ───────────────────────────────────────────────────────────────

func _refresh_stats() -> void:
	if not _stats_label or not is_instance_valid(_stats_label): return
	var path := OS.get_user_data_dir() + "/godotluau_usage.json"
	if not FileAccess.file_exists(path): _stats_label.text = _t("stats_none"); return
	var f := FileAccess.open(path, FileAccess.READ)
	if not f: _stats_label.text = _t("stats_err"); return
	_stats_label.text = _t("stats_size") % [f.get_length() / 1024.0, f.get_as_text().count("\n")]

func _confirm_clear_data() -> void:
	var dlg := ConfirmationDialog.new()
	dlg.title       = _t("dlg_clear_title")
	dlg.dialog_text = _t("dlg_clear_text")
	dlg.confirmed.connect(_clear_data)
	get_editor_interface().get_base_control().add_child(dlg)
	dlg.popup_centered()

func _clear_data() -> void:
	var path := OS.get_user_data_dir() + "/godotluau_usage.json"
	if FileAccess.file_exists(path): DirAccess.remove_absolute(path)
	_refresh_stats()
	_set_notif(_t("bar_data_cleared"), Color(0.5, 0.9, 0.5))

func _set_notif(msg: String, color: Color) -> void:
	if not (_notif_bar and is_instance_valid(_notif_bar)): return
	_notif_label.text = msg
	_notif_label.add_theme_color_override("font_color", color)
	_notif_bar.visible = true
	_notif_bar.modulate.a = 0.0
	var tw := _notif_bar.create_tween()
	tw.tween_property(_notif_bar, "modulate:a", 1.0, 0.2)
	get_tree().create_timer(4.0).timeout.connect(func():
		if not (_notif_bar and is_instance_valid(_notif_bar)): return
		var tw_out := _notif_bar.create_tween()
		tw_out.tween_property(_notif_bar, "modulate:a", 0.0, 0.3)
		tw_out.tween_callback(func():
			if _notif_bar and is_instance_valid(_notif_bar): _notif_bar.visible = false
		)
	)

func _get_local_version() -> String:
	# Si el proyecto viene de una version anterior, el archivo sigue en la raiz.
	# Sin esta caida la version saldria "desconocida" y el updater creeria que
	# hay que reinstalar desde cero.
	var path := VERSION_FILE
	if not FileAccess.file_exists(path):
		path = VERSION_FILE_LEGACY
	if not FileAccess.file_exists(path): return _t("ver_unknown")
	var f := FileAccess.open(path, FileAccess.READ)
	return f.get_as_text().strip_edges() if f else _t("ver_unknown")

# La implementa plugin.gd (rehace el panel de ajustes). Va vacia aqui para que
# la IA y el actualizador puedan pedirla desde sus partes: GDScript llama
# siempre a la version del script de mas arriba.
func _rebuild_panel() -> void:
	pass
