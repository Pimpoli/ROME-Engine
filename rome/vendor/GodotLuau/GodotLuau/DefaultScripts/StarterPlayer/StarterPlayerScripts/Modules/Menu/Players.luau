-- > GodotLuau — PimpoliDev
-- Players.lua — ModuleScript — Modules/Menu
--
-- "People" tab of the menu: list of players in the server. EDITABLE.
-- // Pestaña "Personas" del menú: lista de jugadores en el servidor. EDITABLE.

local Players = {}
local PlayersService = game:GetService("Players")

function Players.Build(ui, page, player)
	ui:Section(page, "In this server")

	local list = {}
	pcall(function() list = PlayersService:GetPlayers() end)
	if #list == 0 then list = { player } end

	local myName = tostring(player.Name or "")

	for _, p in ipairs(list) do
		-- El nombre visible: DisplayName si existe, si no el nombre del Character,
		-- y como ultimo recurso el nombre del nodo (que puede ser autogenerado).
		local pname = nil
		pcall(function() pname = p.DisplayName end)
		if not pname or pname == "" then
			pcall(function() pname = p.Character and p.Character.Name end)
		end
		if not pname or pname == "" then pname = tostring(p.Name or "Player") end
		local isMe = (pname == myName)

		local row = ui:Row(page, "")

		-- Avatar EN VIVO: un ViewportFrame retrata al Character y se refresca
		-- solo (si el jugador se pone un sombrero, aparece al momento). Si aun
		-- no hay Character, queda el circulo con la inicial hasta que exista.
		local av = Instance.new("Frame")
		av.BorderSizePixel = 0
		av.Size = UDim2.new(0, 44, 0, 44)
		av.Position = UDim2.new(0, 12, 0.5, -22)
		av.BackgroundColor3 = isMe and ui.Theme.AccentBg or Color3.fromRGB(70, 74, 84)
		av.Parent = row
		ui.corner(av, 22)

		local ini = Instance.new("TextLabel")
		ini.BorderSizePixel = 0
		ini.BackgroundTransparency = 1
		ini.Size = UDim2.new(1, 0, 1, 0)
		ini.Text = string.upper(string.sub(pname, 1, 1))
		ini.TextColor3 = ui.Theme.Text
		ini.Font = Enum.Font.GothamBold
		ini.TextSize = 16
		ini.Parent = av

		local function ponRetrato()
			local ch = p.Character
			if not ch then return end
			pcall(function()
				local vf = Instance.new("ViewportFrame")
				vf.Parent = av
				vf.Size = UDim2.new(0, 44, 0, 44)
				vf.Position = UDim2.new(0, 0, 0, 0)
				vf.Subject = ch
				ini.Visible = false
			end)
		end
		ponRetrato()
		pcall(function()
			p.CharacterAdded:Connect(function() task.delay(0.5, ponRetrato) end)
		end)

		-- Add " (You)" or " (Tú)" if it's the local player
		-- // Agrega " (Tú)" si es el jugador local
		local suffix = isMe and "  (You)" or ""
		local nm = ui.label(row, pname .. suffix, 16, ui.Theme.Text, Enum.TextXAlignment.Left, true)
		nm.Position = UDim2.new(0, 60, 0, 0)
		nm.Size = UDim2.new(1, -76, 1, 0)
	end

	return Players
end

return Players
