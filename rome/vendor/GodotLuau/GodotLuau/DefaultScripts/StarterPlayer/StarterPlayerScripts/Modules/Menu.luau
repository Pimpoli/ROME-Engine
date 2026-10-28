-- > GodotLuau — PimpoliDev
-- Menu.lua — ModuleScript — StarterPlayerScripts/Modules/Menu
--
-- THE ESCAPE MENU, ENTIRELY IN LUAU. Everything you see when pressing Esc is built
-- from here, so you can change everything: add rows, remove options you don't want,
-- reorder or remove tabs, change colors or redesign it entirely.
-- // EL MENÚ DE ESCAPE, ENTERO EN LUAU. Todo lo que ves al pulsar Esc se construye
-- // desde aquí, así que puedes cambiarlo todo: añadir filas, quitar opciones que no
-- // quieras, reordenar o quitar pestañas, cambiar los colores o rediseñarlo entero.
--
-- Menu.lua       <- this: assembles the menu and decides which tabs and buttons exist
-- MenuUi.lua     <- the visual toolkit (theme, Section, Row, Stepper, Toggle, Slider)
-- MenuUi/Languages/ <- one ModuleScript per language (copy one to add a language)
-- Settings.lua   <- the settings tab // la pestaña de ajustes
-- Players.lua    <- the player list // la lista de jugadores
-- Gallery.lua    <- screenshots // capturas
-- Help.lua       <- controls per device // controles según dispositivo
--
-- The engine has a fallback native menu written in C++. As soon as this module
-- starts it calls game:SetNativeMenuEnabled(false) and the native one steps aside.
-- // El motor trae un menú nativo de respaldo en C++; este módulo lo aparta al arrancar.

local Menu = {}

local UserInputService = game:GetService("UserInputService")
local Workspace = game:GetService("Workspace")

-- EDITABLE: el nombre y el logo que salen arriba-izquierda.
local EMPRESA = "Rocket Meteor"

-- El logo del juego. Siempre esta en esta ruta: al crear el proyecto se copia
-- ahi la imagen de fabrica, y cambiarlo desde "GodotLuau Config" (pestana
-- Marca) sobreescribe ESE archivo. Asi no hay que tocar este script para
-- cambiar el logo... pero sigue siendo una variable normal, y quien prefiera
-- apuntar a otra imagen suya solo tiene que escribir aqui la ruta.
-- // The game logo. Always at this path; the Config panel overwrites the file.
local LOGO = "res://GodotLuau/icons/MenuLogo.svg"

-- EDITABLE: which tabs exist and in what order. `title` is a language key from
-- MenuUi/Languages; `build` is the module that fills the tab.
-- // EDITABLE: qué pestañas hay y en qué orden. `title` es una clave de idioma.
local TABS = {
	{ title = "TitlePlayers", build = "Players" },
	{ title = "TitleSettings", build = "Settings" },
	{ title = "TitleGallery", build = "Gallery" },
	{ title = "TitleHelp", build = "Help" },
}

function Menu.Init(player)
	-- IDEMPOTENTE: este módulo puede ejecutarse más de una vez; sin esta guarda
	-- se apilaban varios menús completos uno encima de otro. Pero si la gui
	-- MURIÓ (reset del PlayerGui al morir), hay que reconstruir, no devolverla.
	if Menu.Ui then
		if Menu.Ui.gui and Menu.Ui.gui.Parent then return Menu.Ui end
		Menu.Ui = nil
	end
	local here = script
	local MenuUi = require(here:WaitForChild("MenuUi"))

	-- Idioma: el guardado, o inglés. (Se guarda en el DataStore local del motor.)
	MenuUi.LoadLanguage(MenuUi.SavedLanguage() or "English")

	-- El motor deja de dibujar su menú: desde aquí manda este archivo.
	game:SetNativeMenuEnabled(false)

	MenuUi.LogoPath = LOGO   -- lo usa la marca de agua junto a las pestañas
	local ui = MenuUi.new(player)

	for _, tab in ipairs(TABS) do
		local titulo = ui:Tr(tab.title)
		local page = ui:AddTab(titulo)
		local mod = here:FindFirstChild(tab.build)
		if mod then
			-- Si una pestaña falla, el resto del menú sigue en pie.
			local ok, err = pcall(function() require(mod).Build(ui, page, player) end)
			if not ok then
				warn("[Menu] Tab failed: " .. tab.build .. ": " .. tostring(err))
			end
		end
	end

	if TABS[1] then ui:ShowTab(ui:Tr(TABS[1].title)) end

	-- EDITABLE: tu logo arriba-izquierda (abre el menú, como el logo de Roblox) y
	-- el botón de chat al lado. Pon tu imagen en res://gamelogo.png; si no existe,
	-- sale el nombre en una pastilla (nunca un cuadro blanco).
	ui:BuildTopbar(EMPRESA, LOGO)

	-- EDITABLE: bottom buttons. `cb` is the action. // los botones de abajo.
	ui:BuildBottom({
		{ text = ui:Tr("LeaveGame"), color = MenuUi.Theme.Red, cb = function()
			ui:Close()
			game:Shutdown()
		end },
		{ text = ui:Tr("Reset"), cb = function()
			local ch = player.Character
			local h = ch and ch:FindFirstChild("Humanoid")
			if h then h.Health = 0 end
			ui:Close()
		end },
		{ text = ui:Tr("Resume"), cb = function()
			ui:Close()
		end },
	})

	-- Esc / Start abren y cierran. La conexión se hace UNA vez y usa Menu.Ui
	-- dinámico: así Rebuild() (cambio de idioma) no deja conexiones duplicadas
	-- que abrían y cerraban el menú en el mismo frame.
	if not Menu._escWired then
		Menu._escWired = true
		UserInputService.InputBegan:Connect(function(input, gameProcessed)
			if gameProcessed then return end
			local kc = input.KeyCode
			if kc == Enum.KeyCode.Escape or kc == Enum.KeyCode.ButtonStart then
				if Menu.Ui then Menu.Ui:Toggle() end
			end
		end)
	end

	Menu.Ui = ui -- To open it from another script // Para abrirlo desde otro script
	return ui
end

-- Rebuilds the whole menu (used when the language changes).
-- // Reconstruye el menú entero (lo usa el cambio de idioma).
function Menu.Rebuild(player)
	local estabaAbierto = Menu.Ui and Menu.Ui.isOpen
	if Menu.Ui then
		pcall(function() Menu.Ui.gui:Destroy() end)
		Menu.Ui = nil
	end
	local ui = Menu.Init(player)
	if estabaAbierto and ui then
		ui:Open()
		ui:ShowTab(ui:Tr("TitleSettings"))
	end
	return ui
end

return Menu
