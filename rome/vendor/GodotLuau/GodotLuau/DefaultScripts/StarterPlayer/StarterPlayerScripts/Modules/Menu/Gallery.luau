-- > GodotLuau — PimpoliDev
-- Gallery.lua — ModuleScript — Modules/Menu
--
-- "Gallery" tab: take screenshots and browse them. EDITABLE.
-- // Pestaña "Galería": tomar capturas y verlas. EDITABLE.
--
-- Screenshots go to user://screenshots/ — on Windows that is
-- %APPDATA%/Godot/app_userdata/<your game>/screenshots/
-- // Las capturas van a user://screenshots/ — en Windows eso es
-- // %APPDATA%/Godot/app_userdata/<tu juego>/screenshots/

local Gallery = {}
local Workspace = game:GetService("Workspace")

-- EDITABLE: cuántas miniaturas caben por fila.
local POR_FILA = 3

function Gallery.Build(ui, page, player)
	local T = ui.Theme

	ui:Section(page, ui:Tr("SecScreenshots"))

	local grid          -- se define abajo; lo usa refrescar()
	local contador      -- etiqueta con el número de capturas

	local function refrescar()
		for _, hijo in ipairs(grid:GetChildren()) do
			if hijo:IsA("GuiObject") then hijo:Destroy() end
		end
		local lista = {}
		pcall(function() lista = Workspace:ListScreenshots() end)
		contador.Text = #lista == 0
			and ui:Tr("NoShots")
			or (#lista .. " " .. ui:Tr("Saved"))

		for i, ruta in ipairs(lista) do
			local celda = Instance.new("ImageButton")
			celda.BorderSizePixel = 0
			celda.LayoutOrder = -i          -- la más nueva, primero
			celda.Size = UDim2.new(1 / POR_FILA, -8, 0, 100)
			celda.BackgroundColor3 = T.Row
			celda.ScaleType = Enum.ScaleType.Fit
			celda.Image = ruta
			celda.Parent = grid
			ui.corner(celda, 8)
			ui.stroke(celda, 1, T.Stroke)
			-- Clic: vista grande con Borrar / Cerrar. OJO: Instance.new("ImageButton")
			-- en este motor crea un ImageLabel (sin clic), asi que el clic lo pone
			-- un TextButton transparente encima de la miniatura.
			local zona = Instance.new("TextButton")
			zona.BorderSizePixel = 0
			zona.Text = ""
			zona.BackgroundTransparency = 1
			zona.Size = UDim2.new(1, 0, 1, 0)
			zona.ZIndex = 5
			zona.Parent = celda
			zona.MouseButton1Click:Connect(function() Gallery._abrir(ui, ruta) end)
		end
	end

	-- Botón de capturar. Se cierra el menú un instante para que la foto salga
	-- del JUEGO y no del propio menú abierto.
	local fila = ui:Row(page, ui:Tr("RowCapture"))
	ui:SetTip(fila, ui:Tr("TipCapture"))
	local btn = Instance.new("TextButton")
	btn.BorderSizePixel = 0
	btn.AnchorPoint = Vector2.new(1, 0.5)
	btn.Position = UDim2.new(1, -16, 0.5, 0)
	btn.Size = UDim2.new(0, 130, 0, 34)
	btn.BackgroundColor3 = T.AccentBg
	btn.TextColor3 = T.Text
	btn.Font = Enum.Font.GothamBold
	btn.TextSize = 15
	btn.Text = ui:Tr("Capture")
	btn.Parent = fila
	ui.corner(btn, 8)

	btn.MouseButton1Click:Connect(function()
		-- AL INSTANTE, sin animacion: Close() anima 0.2s y la captura salia con
		-- el menu todavia en pantalla. Se oculta el ScreenGui entero de golpe,
		-- se dejan pasar unos frames de render y entonces se captura.
		ui.gui.Enabled = false
		task.delay(0.35, function()
			local ok = false
			pcall(function() ok = (Workspace:TakeScreenshot() or "") ~= "" end)
			ui.gui.Enabled = true
			if ok then refrescar() end
		end)
	end)

	contador = ui.label(page, "", 14, T.Dim)
	contador.Size = UDim2.new(1, -8, 0, 22)

	grid = Instance.new("Frame")
	grid.BorderSizePixel = 0
	grid.Name = "Grid"
	grid.Size = UDim2.new(1, -8, 0, 340)
	grid.BackgroundTransparency = 1
	grid.Parent = page

	local gl = Instance.new("UIGridLayout")
	gl.CellSize = UDim2.new(1 / POR_FILA, -8, 0, 100)
	gl.CellPadding = UDim2.new(0, 8, 0, 8)
	gl.SortOrder = Enum.SortOrder.LayoutOrder
	gl.Parent = grid

	refrescar()
	Gallery._refrescar = refrescar
	return Gallery
end

-- Vista grande de una captura, con Borrar y Cerrar (clic fuera tambien cierra).
function Gallery._abrir(ui, ruta)
	local T = ui.Theme
	local Workspace = game:GetService("Workspace")
	if Gallery._vista then Gallery._vista:Destroy() end

	local fondo = Instance.new("TextButton")
	fondo.BorderSizePixel = 0
	fondo.Text = ""
	fondo.Size = UDim2.new(1, 0, 1, 0)
	fondo.BackgroundColor3 = Color3.fromRGB(0, 0, 0)
	fondo.BackgroundTransparency = 0.25
	fondo.ZIndex = 80
	fondo.Parent = ui.gui
	Gallery._vista = fondo

	local img = Instance.new("ImageLabel")
	img.BorderSizePixel = 0
	img.AnchorPoint = Vector2.new(0.5, 0.5)
	img.Position = UDim2.new(0.5, 0, 0.5, -20)
	img.Size = UDim2.new(0.72, 0, 0.72, 0)
	img.BackgroundColor3 = T.Panel
	img.ScaleType = Enum.ScaleType.Fit
	img.Image = ruta
	img.ZIndex = 81
	img.Parent = fondo
	ui.corner(img, 10)

	local function boton(texto, color, x, cb)
		local b = Instance.new("TextButton")
		b.BorderSizePixel = 0
		b.AnchorPoint = Vector2.new(0.5, 1)
		b.Position = UDim2.new(0.5, x, 1, -24)
		b.Size = UDim2.new(0, 130, 0, 40)
		b.BackgroundColor3 = color
		b.TextColor3 = T.Text
		b.Font = Enum.Font.GothamBold
		b.TextSize = 15
		b.Text = texto
		b.ZIndex = 82
		b.Parent = fondo
		ui.corner(b, 8)
		b.MouseButton1Click:Connect(cb)
	end
	boton(ui:Tr("Delete"), T.Red, -75, function()
		pcall(function() Workspace:DeleteScreenshot(ruta) end)
		fondo:Destroy(); Gallery._vista = nil
		if Gallery._refrescar then Gallery._refrescar() end
	end)
	boton(ui:Tr("Close"), T.Row, 75, function()
		fondo:Destroy(); Gallery._vista = nil
	end)
	fondo.MouseButton1Click:Connect(function()
		fondo:Destroy(); Gallery._vista = nil
	end)
end

return Gallery
