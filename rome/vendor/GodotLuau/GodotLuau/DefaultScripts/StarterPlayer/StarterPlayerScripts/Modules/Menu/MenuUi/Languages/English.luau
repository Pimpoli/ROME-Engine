-- > GodotLuau — PimpoliDev
-- English.lua — ModuleScript — Modules/Menu/MenuUi/Languages
--
-- DEFAULT LANGUAGE. To add a new language: copy this module, translate every
-- string on the right side, and rename the copy to your language's name
-- ("Français", "Deutsch"...). It appears in the menu automatically.
-- // IDIOMA POR DEFECTO. Para añadir un idioma: copia este module, traduce cada
-- // texto de la derecha y renombra la copia al nombre del idioma. Aparece solo.

return {
	-- Tabs
	TitlePlayers = "Players",
	TitleSettings = "Settings",
	TitleGallery = "Gallery",
	TitleHelp = "Help",

	-- Bottom bar
	LeaveGame = "Leave Game",
	Reset = "Reset",
	Resume = "Resume",

	-- Settings: sections
	SecLanguage = "Language",
	SecGraphics = "Graphics",
	SecAudio = "Audio",
	SecControls = "View & Controls",

	-- Settings: rows
	RowLanguage = "Menu language",
	RowMode = "Mode",
	RowQuality = "Quality level",
	RowRenderRes = "Render resolution",
	RowUpscaler = "Upscaler",
	RowSharpness = "Sharpness",
	RowShadows = "Shadows",
	RowViewDist = "View distance",
	RowPixelTransp = "Pixel transparency",
	RowOcclusion = "Occlusion culling",
	RowOccSize = "Occluder min size",
	RowOccQuality = "Occlusion quality",
	RowVSync = "V-Sync",
	SubVSync = "Limits FPS to the monitor refresh rate (removes tearing)",
	RowMaxFPS = "Max Framerate",
	Unlimited = "Unlimited",
	RowVolume = "Volume",
	RowLockCam = "Lock Camera (1st Person)",
	SubLockCam = "Third person will look like first person",
	RowDynJoy = "Dynamic Joystick (Mobile)",
	SubDynJoy = "Appears where you touch, like Roblox",

	-- Shadow quality names (low to high)
	ShadowNames = { "Off", "Very Low", "Low", "Medium", "High", "Ultra" },

	-- Tooltips (shown when hovering a row)
	TipLanguage = "Changes the language of this menu. Add more by copying a module in MenuUi/Languages.",
	TipMode = "Automatic: the engine adjusts quality by itself. Manual: you pick one level. Custom: every setting separately.",
	TipQuality = "Overall graphics level, 1 to 10. Click a square or use - and +. Only editable in Manual mode.",
	TipRenderRes = "Renders the 3D world at a lower resolution and upscales it. Big FPS gain, slightly softer image.",
	TipUpscaler = "How the lowered resolution is scaled back up. FSR 2 keeps the most sharpness.",
	TipSharpness = "Extra sharpening applied by the upscaler.",
	TipShadows = "Shadow quality. Higher looks smoother but costs FPS.",
	TipViewDist = "How far you can see, in studs from your position. Lower is faster.",
	TipPixelTransp = "Draws transparency by discarding pixels: much faster, shows a fine dither up close.",
	TipOcclusion = "Skips drawing what is hidden behind walls. Helps on maps with few big walls; can cost CPU.",
	TipOccSize = "Minimum size a part needs to block the view behind it.",
	TipOccQuality = "Precision of the occlusion test. More precise costs more CPU.",
	TipVSync = "Syncs frames with your monitor. Removes tearing but caps and can halve your FPS.",
	TipMaxFPS = "Upper limit for frames per second. Unlimited lets the game run as fast as it can.",
	TipVolume = "Master volume of the whole game.",
	TipLockCam = "Locks the camera to first person even in third-person games.",
	TipDynJoy = "On phones, the joystick appears wherever you put your finger.",

	-- Gallery
	SecScreenshots = "Screenshots",
	RowCapture = "Take a screenshot",
	Capture = "Capture",
	NoShots = "No screenshots yet",
	Saved = "saved",
	TipCapture = "Closes the menu for an instant, captures the game and saves it here.",

	-- Help
	SecControlsHelp = "Controls",
	SecAbout = "About",
	RowEngine = "Engine",
	RowDevice = "Device",
	RowTransparency = "Menu transparency",
	TipTransparency = "How see-through the menu panel is.",
	RowPerf = "Performance stats",
	TipPerf = "Shows FPS, frame time and ping on screen.",
	Delete = "Delete",
	Close = "Close",
	TipEngine = "Click the button to open the GodotLuau project page.",

	NoRebind = "Key rebinding is not available yet",
}
