-- > GodotLuau — PimpoliDev
-- Help.lua — ModuleScript — Modules/Menu
--
-- "Help" tab: the controls, shown for the device you are actually playing on.
-- // Pestaña "Ayuda": los controles, mostrados según el dispositivo real.
--
-- EDITABLE: add or remove lines in the tables below. The tab picks the table
-- that matches the device (PC / Mobile / Console) all by itself.
-- // EDITABLE: añade o quita líneas en las tablas de abajo. La pestaña elige
-- // sola la tabla que corresponde al dispositivo (PC / Móvil / Consola).

local Help = {}
local UserInputService = game:GetService("UserInputService")

local CONTROLES = {
	PC = {
		{ "Move",            "W A S D" },
		{ "Jump",             "Space" },
		{ "Rotate camera", "Right mouse button" },
		{ "Zoom",            "Mouse wheel" },
		{ "Chat",                       "/  or  Enter" },
		{ "Menu",                       "Esc" },
	},
	Mobile = {
		{ "Move",            "Joystick (left half)" },
		{ "Jump",             "Jump button" },
		{ "Rotate camera", "Drag on the right half" },
		{ "Zoom",            "Pinch" },
		{ "Menu",                       "Logo, top-left" },
	},
	Console = {
		{ "Move",            "Left stick" },
		{ "Jump",             "A / Cross" },
		{ "Rotate camera", "Right stick" },
		{ "Menu",                       "Start" },
	},
}

-- ¿En qué dispositivo estamos? Se pregunta al motor y se cae a PC si no se sabe.
local function dispositivo()
	local ok, MenuUiMod = pcall(function() return require(script.Parent.MenuUi) end)
	if ok and MenuUiMod.DeviceName then return MenuUiMod.DeviceName() end
	local d = "PC"
	pcall(function()
		if UserInputService.TouchEnabled and not UserInputService.KeyboardEnabled then
			d = "Mobile"
		elseif UserInputService.GamepadEnabled and not UserInputService.KeyboardEnabled then
			d = "Console"
		end
	end)
	return d
end

function Help.Build(ui, page, player)
	local dev = dispositivo()
	ui:Section(page, ui:Tr("SecControlsHelp") .. " — " .. dev)

	for _, par in ipairs(CONTROLES[dev] or CONTROLES.PC) do
		ui:Info(page, par[1], par[2])
	end

	ui:Section(page, ui:Tr("SecAbout"))
	-- El credito del motor viene del C++ (marca de agua: cambiarlo exige
	-- recompilar la DLL, no basta con editar este archivo).
	local credito = "GodotLuau"
	pcall(function() credito = game:GetService("Workspace"):GetEngineCredit() end)
	local credRow = select(2, nil, nil) -- placeholder
	-- Orden pedido: [texto del credito] [espacio] [boton GitHub], sin taparse.
	local cv = ui:Info(page, ui:Tr("RowEngine"), credito)
	local credFila = cv.Parent
	cv.Position = UDim2.new(1, -112, 0.5, 0)   -- el texto termina ANTES del boton
	cv.Size = UDim2.new(0, 330, 1, 0)
	ui:SetTip(credFila, ui:Tr("TipEngine"))
	local gh = Instance.new("TextButton")
	gh.BorderSizePixel = 0
	gh.AnchorPoint = Vector2.new(1, 0.5)
	gh.Position = UDim2.new(1, -16, 0.5, 0)
	gh.Size = UDim2.new(0, 80, 0, 28)
	gh.BackgroundColor3 = ui.Theme.AccentBg
	gh.TextColor3 = ui.Theme.Text
	gh.Font = Enum.Font.GothamBold
	gh.TextSize = 13
	gh.Text = "GitHub"
	gh.Parent = credFila
	ui.corner(gh, 8)
	gh.MouseButton1Click:Connect(function()
		pcall(function() game:GetService("Workspace"):OpenEngineLink() end)
	end)

	local devVal = ui:Info(page, ui:Tr("RowDevice"), dev)
	-- EN VIVO: si el jugador conecta un mando o toca la pantalla, cambia.
	game:GetService("UserInputService").InputBegan:Connect(function()
		local ok, MenuUiMod = pcall(function() return require(script.Parent.MenuUi) end)
		if ok and MenuUiMod.DeviceName then devVal.Text = MenuUiMod.DeviceName() end
	end)

	-- NOTA: cambiar las teclas (remapeo) todavía NO se puede: al motor le falta
	-- un sistema de acciones tipo ContextActionService. Cuando exista, esta
	-- pestaña es el sitio donde irían los botones para reasignarlas.
	local aviso = ui.label(page,
		ui:Tr("NoRebind"),
		13, ui.Theme.Dim)
	aviso.Size = UDim2.new(1, -8, 0, 26)

	return Help
end

return Help
