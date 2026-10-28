-- > GodotLuau — PimpoliDev
-- Settings.lua — ModuleScript — Modules/Menu
--
-- "Settings" tab of the menu. EDITABLE: add/remove rows and sections.
-- Every visible text comes from MenuUi/Languages via ui:Tr(key).
-- // Pestaña "Settings". EDITABLE. Cada texto visible sale de MenuUi/Languages.
--
-- Mode rules, like the old native menu // Reglas por modo, como el menú viejo:
--   Automatic — the engine drives quality; Quality is dimmed (locked)
--   Manual    — the player picks the quality level (the squares)
--   Custom    — every setting separately; those rows ONLY appear in this mode

local Settings = {}
local Workspace = game:GetService("Workspace")

-- Cambia el idioma: lo guarda, recarga las tablas y reconstruye el menú entero.
function Settings._applyLang(ui, nombre, player)
	if not nombre then return end
	local MenuUiMod = require(script.Parent.MenuUi)
	MenuUiMod.SaveLanguage(nombre)
	MenuUiMod.LoadLanguage(nombre)
	local Menu = require(script.Parent)
	if Menu.Rebuild then Menu.Rebuild(player) end
end

function Settings.Build(ui, page, player)
	local MODES = { "Automatic", "Manual", "Custom" }
	local modeIdx = 1
	pcall(function() modeIdx = Workspace:GetGraphicsMode() + 1 end)
	local function modo() return MODES[modeIdx] end

	local quality = 8
	pcall(function() quality = Workspace:GetGraphicsQuality() end)
	local volume = 100
	pcall(function() volume = math.floor(Workspace:GetMasterVolume() + 0.5) end)
	local fpsOptions = { 30, 60, 120, 144, 165, 240, 300, 360, 480, 0 }
	local fpsIdx = 6
	pcall(function()
		local cur = Workspace:GetMaxFPS()
		for i, v in ipairs(fpsOptions) do if v == cur then fpsIdx = i end end
	end)
	local camLocked = false
	local dynJoy = true

	local soloCustom = {}
	local aplicarModo

	-- ── Language ─────────────────────────────────────────────────────
	ui:Section(page, ui:Tr("SecLanguage"))
	-- Desplegable: con muchos idiomas se elige de una lista, no a golpe de flecha.
	local _, langRow = ui:Dropdown(page, ui:Tr("RowLanguage"), ui.Languages(),
		function() return ui.LangName end,
		function(nombre) Settings._applyLang(ui, nombre, player) end)
	ui:SetTip(langRow, ui:Tr("TipLanguage"))

	-- ── Graphics ─────────────────────────────────────────────────────
	ui:Section(page, ui:Tr("SecGraphics"))

	local _, modeRow = ui:Stepper(page, ui:Tr("RowMode"),
		function() return modo() end,
		function()
			modeIdx = math.max(1, modeIdx - 1)
			Workspace:SetGraphicsMode(modeIdx - 1)
			aplicarModo()
		end,
		function()
			modeIdx = math.min(#MODES, modeIdx + 1)
			Workspace:SetGraphicsMode(modeIdx - 1)
			aplicarModo()
		end)
	ui:SetTip(modeRow, ui:Tr("TipMode"))

	-- Quality: solo se toca en Manual. Fuera de Manual la fila se atenúa y
	-- enseña el nivel real que el motor tenga en ese momento.
	local qbar = ui:QualityBar(page, ui:Tr("RowQuality"),
		function()
			if modo() == "Manual" then return quality end
			local lvl = quality
			pcall(function() lvl = Workspace:GetGraphicsQuality() end)
			return lvl
		end,
		function(v)
			if modo() ~= "Manual" then return end
			quality = v
			Workspace:SetGraphicsQuality(v)
		end, 10)
	ui:SetTip(qbar.Row, ui:Tr("TipQuality"))

	-- ── Custom (solo visibles en ese modo) ───────────────────────────
	local function customSlider(nombreKey, tipKey, key, vmin, vmax, fmt)
		local function get01()
			local v = vmin
			pcall(function() v = Workspace:GetGraphicsSetting(key) end)
			return (v - vmin) / (vmax - vmin)
		end
		local _, row = ui:Slider(page, ui:Tr(nombreKey),
			get01,
			function(f) Workspace:SetGraphicsSetting(key, vmin + f * (vmax - vmin)) end,
			fmt)
		table.insert(soloCustom, row)
		ui:SetTip(row, ui:Tr(tipKey))
		return row
	end
	local function customStepper(nombreKey, tipKey, key, values, fmt)
		local idx = 1
		pcall(function()
			local cur = Workspace:GetGraphicsSetting(key)
			for i, v in ipairs(values) do if math.abs(v - cur) < 0.001 then idx = i end end
		end)
		local _, row = ui:Stepper(page, ui:Tr(nombreKey),
			function() return fmt(values[idx]) end,
			function() idx = math.max(1, idx - 1); Workspace:SetGraphicsSetting(key, values[idx]) end,
			function() idx = math.min(#values, idx + 1); Workspace:SetGraphicsSetting(key, values[idx]) end)
		table.insert(soloCustom, row)
		ui:SetTip(row, ui:Tr(tipKey))
		return row
	end

	local function pctOf(vmin, vmax)
		return function(f) return math.floor((vmin + f * (vmax - vmin)) * 100 + 0.5) .. "%" end
	end

	customSlider("RowRenderRes", "TipRenderRes", "RenderScale", 0.25, 1.0, pctOf(0.25, 1.0))
	customStepper("RowUpscaler", "TipUpscaler", "Upscaler", { 0, 1, 2 }, function(v)
		return ({ [0] = "Bilinear", [1] = "FSR", [2] = "FSR 2" })[v] or "?"
	end)
	customSlider("RowSharpness", "TipSharpness", "Sharpness", 0.0, 1.0, pctOf(0.0, 1.0))
	-- Sombras por NOMBRE, no por número (los nombres viven en el idioma activo).
	customStepper("RowShadows", "TipShadows", "ShadowQuality", { 0, 1, 2, 3, 4, 5 }, function(v)
		local nombres = ui:Tr("ShadowNames")
		return (type(nombres) == "table" and nombres[v + 1]) or tostring(v)
	end)
	-- Distancia de vista en STUDS DIRECTOS (1 stud = 1 unidad de posicion).
	-- Antes era un slider 25%..200% de una base que nadie conocia; ahora el
	-- jugador elige exactamente cuanto ve, hasta el tope absoluto de 32768.
	customStepper("RowViewDist", "TipViewDist", "ViewDistanceStuds",
		{ 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 16384, 32768 },
		function(v) return v .. " studs" end)
	customStepper("RowPixelTransp", "TipPixelTransp", "PixelTransparency", { 0, 1 }, function(v)
		return v > 0.5 and "On" or "Off"
	end)
	customStepper("RowOcclusion", "TipOcclusion", "Occlusion", { 0, 1 }, function(v)
		return v > 0.5 and "On" or "Off"
	end)
	customStepper("RowOccSize", "TipOccSize", "OcclusionSize", { 4, 8, 16, 32 }, function(v)
		return v .. " studs"
	end)
	customStepper("RowOccQuality", "TipOccQuality", "OcclusionQuality", { 0, 1, 2 }, function(v)
		return ({ [0] = "Fast", [1] = "Balanced", [2] = "Precise" })[v] or "?"
	end)

	-- ── Display ──────────────────────────────────────────────────────
	local _, vsRow = ui:Toggle(page, ui:Tr("RowVSync"), ui:Tr("SubVSync"),
		function()
			local on = false
			pcall(function() on = Workspace:GetVSync() end)
			return on
		end,
		function()
			local on = false
			pcall(function() on = Workspace:GetVSync() end)
			Workspace:SetVSync(not on)
		end)
	ui:SetTip(vsRow, ui:Tr("TipVSync"))

	-- Transparencia del panel del menu (se guarda sola).
	local _, trRow = ui:Slider(page, ui:Tr("RowTransparency"),
		function() return ui:GetPanelTransparency() / 0.85 end,
		function(f) ui:SetPanelTransparency(f * 0.85) end,
		function(f) return math.floor(f * 100 + 0.5) .. "%" end)
	ui:SetTip(trRow, ui:Tr("TipTransparency"))

	-- Estadisticas de rendimiento (FPS / frame / ping) en pantalla.
	local perfOn = false
	local _, perfRow = ui:Toggle(page, ui:Tr("RowPerf"), nil,
		function() return perfOn end,
		function() perfOn = not perfOn; ui:SetPerfVisible(perfOn) end)
	ui:SetTip(perfRow, ui:Tr("TipPerf"))

	local _, fpsRow = ui:Stepper(page, ui:Tr("RowMaxFPS"),
		function()
			local f = fpsOptions[fpsIdx]
			return f == 0 and ui:Tr("Unlimited") or (f .. " FPS")
		end,
		function() fpsIdx = math.max(1, fpsIdx - 1); Workspace:SetMaxFPS(fpsOptions[fpsIdx]) end,
		function() fpsIdx = math.min(#fpsOptions, fpsIdx + 1); Workspace:SetMaxFPS(fpsOptions[fpsIdx]) end)
	ui:SetTip(fpsRow, ui:Tr("TipMaxFPS"))

	-- ── Audio ────────────────────────────────────────────────────────
	ui:Section(page, ui:Tr("SecAudio"))
	local _, volRow = ui:Slider(page, ui:Tr("RowVolume"),
		function() return volume / 100 end,
		function(f) volume = math.floor(f * 100 + 0.5); Workspace:SetMasterVolume(volume) end,
		function(f) return math.floor(f * 100 + 0.5) .. "%" end)
	ui:SetTip(volRow, ui:Tr("TipVolume"))

	-- ── View & Controls ──────────────────────────────────────────────
	ui:Section(page, ui:Tr("SecControls"))

	local _, camRow = ui:Toggle(page, ui:Tr("RowLockCam"), ui:Tr("SubLockCam"),
		function() return camLocked end,
		function()
			camLocked = not camLocked
			player.CameraMode = camLocked and Enum.CameraMode.LockFirstPerson or Enum.CameraMode.Classic
		end)
	ui:SetTip(camRow, ui:Tr("TipLockCam"))

	local _, joyRow = ui:Toggle(page, ui:Tr("RowDynJoy"), ui:Tr("SubDynJoy"),
		function() return dynJoy end,
		function()
			dynJoy = not dynJoy
			local char = player.Character
			if char then char.JoystickDynamic = dynJoy end
		end)
	ui:SetTip(joyRow, ui:Tr("TipDynJoy"))

	-- ── Reglas por modo ──────────────────────────────────────────────
	aplicarModo = function()
		local m = modo()
		for _, r in ipairs(soloCustom) do r.Visible = (m == "Custom") end
		ui:SetRowEnabled(qbar.Row, m == "Manual")
		qbar.Refresh()
	end
	aplicarModo()

	return Settings
end

return Settings
