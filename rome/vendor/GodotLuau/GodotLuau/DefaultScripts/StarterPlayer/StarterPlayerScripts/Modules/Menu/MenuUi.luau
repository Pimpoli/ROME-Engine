-- > GodotLuau — PimpoliDev
-- MenuUi.lua — ModuleScript — StarterPlayerScripts/Modules/Menu
--
-- Visual toolkit + Escape menu "shell". EDITABLE: change colors, sizes, fonts.
-- // Toolkit visual + "shell" del menú de Escape. EDITABLE: cambia colores, tamaños, fuentes.

local MenuUi = {}
local TweenService = game:GetService("TweenService")
local TWEEN_INFO = TweenInfo.new(0.2, Enum.EasingStyle.Quad, Enum.EasingDirection.Out)

-- Theme (edit to your liking) // Tema (edítalo a gusto)
MenuUi.Theme = {
	Overlay = Color3.fromRGB(0, 0, 0),
	Panel = Color3.fromRGB(16, 18, 24),
	Bar = Color3.fromRGB(11, 13, 18),
	Row = Color3.fromRGB(24, 27, 36),
	RowSel = Color3.fromRGB(34, 38, 50),
	Accent = Color3.fromRGB(255, 255, 255),
	AccentBg = Color3.fromRGB(0, 120, 255),
	Text = Color3.fromRGB(255, 255, 255),
	Dim = Color3.fromRGB(150, 156, 168),
	Green = Color3.fromRGB(56, 200, 110),
	Red = Color3.fromRGB(232, 78, 78),
	Stroke = Color3.fromRGB(45, 50, 65),
}
local T = MenuUi.Theme

-- ── Languages ─────────────────────────────────────────────────────────
-- Every text lives in a ModuleScript inside MenuUi/Languages, one per language.
-- To add one: copy English, translate, rename. It shows up automatically.
-- // Cada texto vive en un ModuleScript dentro de MenuUi/Languages, uno por
-- // idioma. Para anadir otro: copia English, traduce y renombra. Sale solo.
local DEFAULT_LANG = "English"
MenuUi.LangName = DEFAULT_LANG
MenuUi.L = {}
MenuUi.LFallback = {}

function MenuUi.Languages()
	local out = {}
	local folder = script:FindFirstChild("Languages")
	if folder then
		for _, m in ipairs(folder:GetChildren()) do
			if m:IsA("ModuleScript") then table.insert(out, m.Name) end
		end
	end
	table.sort(out)
	if #out == 0 then out = { DEFAULT_LANG } end
	return out
end

function MenuUi.LoadLanguage(nombre)
	MenuUi.LangName = nombre or DEFAULT_LANG
	MenuUi.L, MenuUi.LFallback = {}, {}
	local folder = script:FindFirstChild("Languages")
	if folder then
		local def = folder:FindFirstChild(DEFAULT_LANG)
		if def then pcall(function() MenuUi.LFallback = require(def) end) end
		local mod = folder:FindFirstChild(MenuUi.LangName)
		if mod then pcall(function() MenuUi.L = require(mod) end) end
	end
end

-- Devuelve el texto traducido; si falta la clave, cae al ingles y luego a la clave.
function MenuUi:Tr(clave)
	local v = MenuUi.L[clave]
	if v == nil then v = MenuUi.LFallback[clave] end
	if v == nil then v = clave end
	return v
end

-- El idioma elegido se guarda con el DataStore local del motor.
function MenuUi.SavedLanguage()
	local nombre = nil
	pcall(function()
		nombre = game:GetService("DataStoreService"):GetDataStore("GodotLuauMenu"):GetAsync("Language")
	end)
	return nombre
end
function MenuUi.SaveLanguage(nombre)
	pcall(function()
		game:GetService("DataStoreService"):GetDataStore("GodotLuauMenu"):SetAsync("Language", nombre)
	end)
end

local FONT_REGULAR = Enum.Font.GothamMedium
local FONT_BOLD = Enum.Font.GothamBold

-- Construction helpers // Helpers de construcción
local function corner(inst, r)
	local c = Instance.new("UICorner")
	c.CornerRadius = UDim.new(0, r or 8)
	c.Parent = inst
	return c
end
MenuUi.corner = corner

local function stroke(inst, thickness, color)
	local s = Instance.new("UIStroke")
	s.Thickness = thickness or 1
	s.Color = color or T.Stroke
	s.ApplyStrokeMode = Enum.ApplyStrokeMode.Border
	s.Parent = inst
	return s
end
MenuUi.stroke = stroke

local function vlist(parent, gap)
	local l = Instance.new("UIListLayout")
	l.FillDirection = Enum.FillDirection.Vertical
	l.Padding = UDim.new(0, gap or 8)
	l.HorizontalAlignment = Enum.HorizontalAlignment.Center
	l.SortOrder = Enum.SortOrder.LayoutOrder
	l.Parent = parent
	return l
end
MenuUi.vlist = vlist

local function label(parent, text, size, color, xalign, isBold)
	local l = Instance.new("TextLabel")
	l.BorderSizePixel = 0
	l.BackgroundTransparency = 1
	l.Text = text
	l.Font = isBold and FONT_BOLD or FONT_REGULAR
	l.TextColor3 = color or T.Text
	l.TextSize = size or 16
	l.TextXAlignment = xalign or Enum.TextXAlignment.Left
	l.TextYAlignment = Enum.TextYAlignment.Center
	l.Parent = parent
	return l
end
MenuUi.label = label

-- ── Gamepad focus ────────────────────────────────────────────────────
-- Every widget registers here so a controller can drive the whole menu:
-- D-pad up/down moves, left/right adjusts, A activates, L1/R1 switch tabs.
-- // Cada widget se registra aqui para que el MANDO pueda manejar el menu:
-- // cruceta arriba/abajo mueve, izq/der ajusta, A activa, L1/R1 cambia pestaña.
function MenuUi:_regFocus(page, row, acciones)
	self._focus = self._focus or {}
	self._focus[page] = self._focus[page] or {}
	acciones.row = row
	table.insert(self._focus[page], acciones)
end

-- Row widgets (for tab content) // Widgets de fila (para el contenido de las pestañas)

-- Section header // Encabezado de sección
function MenuUi:Section(page, title)
	local l = label(page, title, 18, T.Text, Enum.TextXAlignment.Left, true)
	l.Size = UDim2.new(1, -8, 0, 40)
	return l
end

-- Base row: soft background with label on the left // Fila base: fondo suave con etiqueta a la izquierda
function MenuUi:Row(page, text, subtitle)
	local row = Instance.new("Frame")
	row.BorderSizePixel = 0
	row.Size = UDim2.new(1, -8, 0, subtitle and 60 or 48)
	row.BackgroundColor3 = T.Row
	row.Parent = page
	corner(row, 8)
	stroke(row, 1, T.Stroke)

	-- Hover animation // Animación al pasar el ratón
	row.MouseEnter:Connect(function() TweenService:Create(row, TWEEN_INFO, {BackgroundColor3 = T.RowSel}):Play() end)
	row.MouseLeave:Connect(function() TweenService:Create(row, TWEEN_INFO, {BackgroundColor3 = T.Row}):Play() end)

	local lbl = label(row, text, 15, T.Text, Enum.TextXAlignment.Left, true)
	lbl.Position = UDim2.new(0, 16, 0, 0)
	lbl.Size = UDim2.new(0.55, 0, subtitle and 0.62 or 1, 0)

	if subtitle then
		local sub = label(row, subtitle, 12, T.Dim)
		sub.Position = UDim2.new(0, 16, 0.55, 0)
		sub.Size = UDim2.new(0.6, 0, 0.42, 0)
	end
	return row
end

-- Info row: label + value on the right // Fila informativa: etiqueta + valor a la derecha
function MenuUi:Info(page, text, value)
	local row = self:Row(page, text)
	local v = label(row, value, 15, T.Dim, Enum.TextXAlignment.Right)
	v.AnchorPoint = Vector2.new(1, 0.5)
	v.Position = UDim2.new(1, -16, 0.5, 0)
	v.Size = UDim2.new(0, 220, 1, 0)
	return v
end

-- Stepper (Roblox style) // Stepper (estilo Roblox)
function MenuUi:Stepper(page, text, getText, onMinus, onPlus)
	local row = self:Row(page, text)
	local function btn(sym, xoff)
		local b = Instance.new("TextButton")
		b.BorderSizePixel = 0
		b.Size = UDim2.new(0, 36, 0, 36)
		b.AnchorPoint = Vector2.new(1, 0.5)
		b.Position = UDim2.new(1, xoff, 0.5, 0)
		b.BackgroundColor3 = T.Row
		b.TextColor3 = T.Text
		b.Font = FONT_BOLD
		b.TextSize = 18
		b.Text = sym
		b.Parent = row
		corner(b, 8)
		stroke(b, 1, T.Stroke)

		b.MouseEnter:Connect(function() TweenService:Create(b, TWEEN_INFO, {BackgroundColor3 = T.RowSel}):Play() end)
		b.MouseLeave:Connect(function() TweenService:Create(b, TWEEN_INFO, {BackgroundColor3 = T.Row}):Play() end)
		return b
	end

	local val = label(row, getText(), 15, T.Text, Enum.TextXAlignment.Center, true)
	val.AnchorPoint = Vector2.new(1, 0.5)
	val.Position = UDim2.new(1, -56, 0.5, 0)
	val.Size = UDim2.new(0, 120, 1, 0)

	local plus = btn("+", -12)
	local minus = btn("-", -188)

	local function hazMenos() onMinus(); val.Text = getText() end
	local function hazMas() onPlus(); val.Text = getText() end
	minus.MouseButton1Click:Connect(hazMenos)
	plus.MouseButton1Click:Connect(hazMas)
	self:_regFocus(page, row, { left = hazMenos, right = hazMas, activate = hazMas })
	return val, row
end

-- Toggle ON/OFF (Animated pill style) // Toggle ON/OFF (Estilo píldora animada)
function MenuUi:_ToggleWidget(page, text, subtitle, getOn, onToggle)
	local row = self:Row(page, text, subtitle)

	local bg = Instance.new("TextButton")
	bg.BorderSizePixel = 0
	bg.Size = UDim2.new(0, 50, 0, 26)
	bg.AnchorPoint = Vector2.new(1, 0.5)
	bg.Position = UDim2.new(1, -16, 0.5, 0)
	bg.Text = ""
	bg.Parent = row
	corner(bg, 13)

	local circ = Instance.new("Frame")
	circ.BorderSizePixel = 0
	circ.Size = UDim2.new(0, 20, 0, 20)
	circ.AnchorPoint = Vector2.new(0, 0.5)
	circ.BackgroundColor3 = Color3.fromRGB(255, 255, 255)
	circ.Parent = bg
	corner(circ, 10)

	-- TODA la fila es clicable, no solo la pildora de 50x26 de la derecha.
	-- Apuntar a un interruptor pequeno es incomodo y en tactil directamente
	-- falla; en Roblox estas filas se pulsan enteras. El boton va DEBAJO de la
	-- pildora (ZIndex menor) para que el clic sobre ella siga funcionando igual.
	local zona = Instance.new("TextButton")
	zona.BorderSizePixel = 0
	zona.Size = UDim2.new(1, 0, 1, 0)
	zona.BackgroundTransparency = 1
	zona.Text = ""
	zona.ZIndex = (row.ZIndex or 1)
	zona.Parent = row

	-- Azul cuando esta encendido. La fila entera cambia de color, no solo el
	-- interruptor: de un vistazo se ve que hay activado sin leer nada.
	local AZUL_ON = Color3.fromRGB(36, 62, 110)

	local function colorFila(isOn, hover)
		if isOn then
			return hover and Color3.fromRGB(46, 78, 136) or AZUL_ON
		end
		return hover and T.RowSel or T.Row
	end

	local hovering = false
	local function paint(animate)
		local isOn = getOn()
		local goalColor = isOn and T.AccentBg or Color3.fromRGB(70, 74, 84)
		local goalPos = isOn and UDim2.new(1, -23, 0.5, 0) or UDim2.new(0, 3, 0.5, 0)
		local goalRow = colorFila(isOn, hovering)

		if animate then
			TweenService:Create(bg, TWEEN_INFO, {BackgroundColor3 = goalColor}):Play()
			TweenService:Create(circ, TWEEN_INFO, {Position = goalPos}):Play()
			TweenService:Create(row, TWEEN_INFO, {BackgroundColor3 = goalRow}):Play()
		else
			bg.BackgroundColor3 = goalColor
			circ.Position = goalPos
			row.BackgroundColor3 = goalRow
		end
	end
	paint(false)

	-- El hover generico de Row pinta gris siempre; aqui hay que respetar el
	-- azul del encendido, asi que se sobreescribe con el color que toca.
	zona.MouseEnter:Connect(function()
		hovering = true
		TweenService:Create(row, TWEEN_INFO, {BackgroundColor3 = colorFila(getOn(), true)}):Play()
	end)
	zona.MouseLeave:Connect(function()
		hovering = false
		TweenService:Create(row, TWEEN_INFO, {BackgroundColor3 = colorFila(getOn(), false)}):Play()
	end)

	local function activa() onToggle(); paint(true) end
	bg.MouseButton1Click:Connect(activa)
	zona.MouseButton1Click:Connect(activa)
	self:_regFocus(page, row, { activate = activa })
	return bg, row
end

-- Quality bar: 10 clickable squares, brighter the higher the level.
-- // Barra de calidad: 10 cuadraditos clicables; cuanto mas alto el nivel, mas blancos.
-- Es el widget del menu nativo de antes: se lee de un golpe y se elige con un clic.
function MenuUi:QualityBar(page, text, getLevel, setLevel, steps)
	local row = self:Row(page, text)
	local n = steps or 10
	local cells = {}

	-- Botones - / + ademas de los cuadraditos: los cuadraditos son para saltar a
	-- un nivel de un clic, las flechas para ajustar de uno en uno sin apuntar.
	local function arrow(sym, xoff)
		local b = Instance.new("TextButton")
		b.BorderSizePixel = 0
		b.Size = UDim2.new(0, 36, 0, 36)
		b.AnchorPoint = Vector2.new(1, 0.5)
		b.Position = UDim2.new(1, xoff, 0.5, 0)
		b.BackgroundColor3 = T.Row
		b.TextColor3 = T.Text
		b.Font = FONT_BOLD
		b.TextSize = 18
		b.Text = sym
		b.Parent = row
		corner(b, 8)
		stroke(b, 1, T.Stroke)
		b.MouseEnter:Connect(function() TweenService:Create(b, TWEEN_INFO, {BackgroundColor3 = T.RowSel}):Play() end)
		b.MouseLeave:Connect(function() TweenService:Create(b, TWEEN_INFO, {BackgroundColor3 = T.Row}):Play() end)
		return b
	end

	local barW = n * 18 + (n - 1) * 4
	local holder = Instance.new("Frame")
	holder.BorderSizePixel = 0
	holder.AnchorPoint = Vector2.new(1, 0.5)
	holder.Position = UDim2.new(1, -60, 0.5, 0)   -- deja hueco al boton "+"
	holder.Size = UDim2.new(0, barW, 0, 24)
	holder.BackgroundTransparency = 1
	holder.Parent = row

	local hl = Instance.new("UIListLayout")
	hl.FillDirection = Enum.FillDirection.Horizontal
	hl.Padding = UDim.new(0, 4)
	hl.VerticalAlignment = Enum.VerticalAlignment.Center
	hl.SortOrder = Enum.SortOrder.LayoutOrder
	hl.Parent = holder

	local function paint()
		-- Sanear: si getLevel() devuelve nil (el motor aun no responde) todos los
		-- cuadraditos salian apagados y parecia que la barra estaba rota.
		local lvl = tonumber(getLevel()) or 0
		lvl = math.clamp(math.floor(lvl), 0, n)
		for i, c in ipairs(cells) do
			-- Lleno = casi blanco; vacio = gris oscuro. El degradado suave dentro de
			-- los llenos hace que se "lea" el nivel sin mirar ningun numero.
			local on = (i <= lvl)
			local tint = on and (0.55 + 0.45 * (i / n)) or 0.16
			local v = math.floor(255 * tint)
			-- Se asigna DIRECTO y ademas se anima. Antes solo se animaba, y si el
			-- tween de BackgroundColor3 no corre (o corre antes de que el cuadradito
			-- este en pantalla) los 10 se quedaban grises pasara lo que pasara.
			c.BackgroundColor3 = Color3.fromRGB(v, v, v)
			TweenService:Create(c, TWEEN_INFO, {BackgroundColor3 = Color3.fromRGB(v, v, v)}):Play()
		end
	end

	for i = 1, n do
		local c = Instance.new("TextButton")
		c.BorderSizePixel = 0
		c.Name = "Cell" .. i
		c.LayoutOrder = i
		c.Size = UDim2.new(0, 18, 0, 24)
		c.Text = ""
		c.BackgroundColor3 = Color3.fromRGB(40, 40, 40)
		c.Parent = holder
		corner(c, 4)
		c.MouseButton1Click:Connect(function() setLevel(i); paint() end)
		cells[i] = c
	end
	local plus  = arrow("+", -16)
	local minus = arrow("-", -(60 + barW + 8))
	local function hazMas() local v = math.min(n, getLevel() + 1); setLevel(v); paint() end
	local function hazMenos() local v = math.max(1, getLevel() - 1); setLevel(v); paint() end
	plus.MouseButton1Click:Connect(hazMas)
	minus.MouseButton1Click:Connect(hazMenos)
	self:_regFocus(page, row, { left = hazMenos, right = hazMas, activate = hazMas })

	paint()
	return { Refresh = paint, Row = row }
end

-- Dropdown: a row that unfolds a list of options when clicked. Scales to many
-- items (a future user with 40 languages picks from a list, not 40 clicks).
-- // Desplegable: una fila que al tocarla abre la lista de opciones completa.
function MenuUi:Dropdown(page, text, items, getCurrent, onPick)
	local row = self:Row(page, text)

	local btn = Instance.new("TextButton")
	btn.BorderSizePixel = 0
	btn.AnchorPoint = Vector2.new(1, 0.5)
	btn.Position = UDim2.new(1, -16, 0.5, 0)
	btn.Size = UDim2.new(0, 190, 0, 34)
	btn.BackgroundColor3 = T.RowSel
	btn.TextColor3 = T.Text
	btn.Font = FONT_BOLD
	btn.TextSize = 14
	btn.Text = tostring(getCurrent()) .. "   v"
	btn.Parent = row
	corner(btn, 8)
	stroke(btn, 1, T.Stroke)

	-- La lista va DESPUES de la fila; el UIListLayout la coloca debajo.
	local lista = Instance.new("Frame")
	lista.BorderSizePixel = 0
	lista.Size = UDim2.new(1, -8, 0, #items * 38 + 6)
	lista.BackgroundColor3 = T.Bar
	lista.Parent = page
	lista.Visible = false   -- despues de Parent: antes, el gate del motor lo pisaba
	corner(lista, 8)
	stroke(lista, 1, T.Stroke)
	local ll = Instance.new("UIListLayout")
	ll.FillDirection = Enum.FillDirection.Vertical
	ll.Padding = UDim.new(0, 2)
	ll.HorizontalAlignment = Enum.HorizontalAlignment.Center
	ll.SortOrder = Enum.SortOrder.LayoutOrder
	ll.Parent = lista

	for _, nombre in ipairs(items) do
		local it = Instance.new("TextButton")
		it.BorderSizePixel = 0
		it.Size = UDim2.new(1, -12, 0, 34)
		it.BackgroundColor3 = (nombre == getCurrent()) and T.RowSel or T.Bar
		it.TextColor3 = T.Text
		it.Font = FONT_REGULAR
		it.TextSize = 14
		it.Text = nombre
		it.Parent = lista
		corner(it, 6)
		it.MouseEnter:Connect(function() it.BackgroundColor3 = T.RowSel end)
		it.MouseLeave:Connect(function() it.BackgroundColor3 = T.Bar end)
		it.MouseButton1Click:Connect(function()
			lista.Visible = false
			btn.Text = nombre .. "   v"
			onPick(nombre)
		end)
	end

	btn.MouseButton1Click:Connect(function() lista.Visible = not lista.Visible end)
	local function ciclo(paso)
		local cur, idx = getCurrent(), 1
		for i, nm in ipairs(items) do if nm == cur then idx = i end end
		idx = math.clamp(idx + paso, 1, #items)
		if items[idx] ~= cur then onPick(items[idx]) end
	end
	self:_regFocus(page, row, {
		left = function() ciclo(-1) end,
		right = function() ciclo(1) end,
		activate = function() lista.Visible = not lista.Visible end,
	})
	return btn, row
end

-- Menu Shell // Shell del menú
function MenuUi.new(player)
	local self = setmetatable({}, { __index = MenuUi })
	local pg = player:WaitForChild("PlayerGui")

	-- Si quedo un menu de una pasada anterior, se retira: sin esto se apilaban
	-- varios ScreenGui identicos y el de abajo seguia capturando los clics.
	local viejo = pg:FindFirstChild("GodotLuauMenu")
	if viejo then viejo:Destroy() end

	local gui = Instance.new("ScreenGui")
	gui.Name = "GodotLuauMenu"
	gui.ResetOnSpawn = false
	gui.ZIndexBehavior = Enum.ZIndexBehavior.Sibling
	gui.DisplayOrder = 50
	gui.Parent = pg

	local overlay = Instance.new("Frame")
	overlay.BorderSizePixel = 0
	overlay.Name = "Overlay"
	overlay.Size = UDim2.new(1, 0, 1, 0)
	overlay.BackgroundColor3 = T.Overlay
	overlay.BackgroundTransparency = 1
	overlay.Visible = false
	overlay.Parent = gui

	local panel = Instance.new("Frame")
	panel.BorderSizePixel = 0
	panel.Name = "Panel"
	panel.AnchorPoint = Vector2.new(0.5, 0.5)
	panel.Position = UDim2.new(0.5, 0, 0.5, 20) -- Start slightly lower for animation // Empieza ligeramente abajo para animación
	-- EDITABLE: ancho del panel. Estrecho y centrado, como el menu nativo de antes
	-- (antes ocupaba casi toda la pantalla y quedaba enorme en monitores grandes).
	panel.Size = UDim2.new(0.58, 0, 0.86, 0)
	panel.BackgroundColor3 = T.Panel
	panel.BackgroundTransparency = 1
	panel.Parent = overlay
	corner(panel, 12)
	stroke(panel, 1, T.Stroke)

	-- Tab bar // Barra de pestañas
	local tabbar = Instance.new("Frame")
	tabbar.BorderSizePixel = 0
	tabbar.Name = "TabBar"
	tabbar.Size = UDim2.new(1, -24, 0, 52)
	tabbar.Position = UDim2.new(0, 12, 0, 6)
	tabbar.BackgroundTransparency = 1
	tabbar.Parent = panel

	local tlayout = Instance.new("UIListLayout")
	tlayout.FillDirection = Enum.FillDirection.Horizontal
	tlayout.Padding = UDim.new(0, 20)
	tlayout.VerticalAlignment = Enum.VerticalAlignment.Center
	tlayout.HorizontalAlignment = Enum.HorizontalAlignment.Left
	tlayout.SortOrder = Enum.SortOrder.LayoutOrder
	tlayout.Parent = tabbar

	-- Watermark: el icono del juego a la derecha de las pestañas.
	-- // Marca de agua: el icono a la derecha, junto a la pestaña de Ayuda.
	local marca = Instance.new("ImageLabel")
	marca.BorderSizePixel = 0
	marca.Name = "Watermark"
	marca.AnchorPoint = Vector2.new(1, 0)
	marca.Position = UDim2.new(1, -14, 0, 14)   -- esquina derecha, junto a Ayuda
	marca.Size = UDim2.new(0, 48, 0, 48)
	marca.BackgroundTransparency = 1
	marca.ScaleType = Enum.ScaleType.Fit
	marca.Image = MenuUi.LogoPath or ""
	marca.Parent = panel

	-- Separator line // Línea separadora
	local sep = Instance.new("Frame")
	sep.Size = UDim2.new(1, 0, 0, 1)
	sep.Position = UDim2.new(0, 0, 0, 60)
	sep.BackgroundColor3 = T.Stroke
	sep.BorderSizePixel = 0
	sep.Parent = panel

	-- Content // Contenido
	local content = Instance.new("Frame")
	content.BorderSizePixel = 0
	content.Name = "Content"
	content.Position = UDim2.new(0, 12, 0, 70)
	content.Size = UDim2.new(1, -24, 1, -152)
	content.BackgroundTransparency = 1
	content.ClipsDescendants = true   -- recorta: la pagina se desplaza por dentro
	content.Parent = panel

	-- Bottom bar // Barra inferior
	local bottom = Instance.new("Frame")
	bottom.BorderSizePixel = 0
	bottom.Name = "Bottom"
	bottom.AnchorPoint = Vector2.new(0.5, 1)
	bottom.Position = UDim2.new(0.5, 0, 1, -12)
	bottom.Size = UDim2.new(1, -24, 0, 56)
	bottom.BackgroundTransparency = 1
	bottom.Parent = panel

	self.gui, self.overlay, self.panel = gui, overlay, panel
	self.tabbar, self.content, self.bottom = tabbar, content, bottom
	self.pages, self.tabButtons, self.current = {}, {}, nil
	self.isOpen = false
	self:_wireScroll()
	return self
end

-- Creates a tab (button + content page). Returns the page.
-- // Crea una pestaña (botón + página de contenido). Devuelve la página.
function MenuUi:AddTab(name)
	local btn = Instance.new("TextButton")
	btn.BorderSizePixel = 0
	btn.Name = "Tab_" .. name
	btn.Size = UDim2.new(0, 132, 0, 40)   -- ancho para titulos traducidos largos
	btn.BackgroundTransparency = 1
	btn.TextColor3 = T.Dim
	btn.Font = FONT_BOLD
	btn.TextSize = 16
	btn.Text = name
	btn.Parent = self.tabbar

	-- Animated line indicator // Línea indicadora animada
	local line = Instance.new("Frame")
	line.Size = UDim2.new(0, 0, 0, 2)
	line.AnchorPoint = Vector2.new(0.5, 1)
	line.Position = UDim2.new(0.5, 0, 1, 6)
	line.BackgroundColor3 = T.Text
	line.BorderSizePixel = 0
	line.Parent = btn

	btn.MouseButton1Click:Connect(function() self:ShowTab(name) end)
	self.tabButtons[name] = {btn = btn, line = line}

	-- Using ScrollingFrame for infinite content // Usando ScrollingFrame para contenido infinito
	local page = Instance.new("ScrollingFrame")
	page.Name = "Page_" .. name
	page.Size = UDim2.new(1, 0, 1, 0)
	page.BackgroundTransparency = 1
	page.ScrollBarThickness = 4
	page.ScrollBarImageColor3 = T.Dim
	page.BorderSizePixel = 0
	page.CanvasSize = UDim2.new(0, 0, 0, 0)   -- lo ajusta FitCanvas // FitCanvas sets it
	page.Visible = false
	page.Parent = self.content

	-- El scroll mueve un contenedor INTERIOR, no la pagina: si se moviera la
	-- pagina, su borde inferior subiria con ella y la mitad de abajo del menu
	-- quedaria vacia (el recorte viaja con el rect que se mueve).
	-- OJO: NO usar AutomaticCanvasSize. Al crecer el lienzo solo, el motor
	-- recoloca los hijos y el UIListLayout se colapsa (filas amontonadas).
	local inner = Instance.new("Frame")
	inner.BorderSizePixel = 0
	inner.Name = "Canvas"
	inner.Size = UDim2.new(1, 0, 1, 0)
	inner.BackgroundTransparency = 1
	inner.Parent = page
	local list = vlist(inner, 8)

	self._tabOrder = self._tabOrder or {}
	table.insert(self._tabOrder, name)
	self.pages[name] = inner        -- aqui construyen las pestanas y esto se mueve
	self.pageOuter = self.pageOuter or {}
	self.pageOuter[name] = page     -- esto queda fijo y recorta
	return inner
end

-- Fija el lienzo de una pestana YA construida: suma lo que ocupan sus filas.
-- Hay que llamarlo DESPUES de anadir todo (lo hace Menu.lua por cada pestana).
function MenuUi:FitCanvas(page)
	local alto = 0
	for _, hijo in ipairs(page:GetChildren()) do
		if hijo:IsA("GuiObject") then
			alto = alto + hijo.Size.Y.Offset + 8   -- 8 = separacion del UIListLayout
		end
	end
	page.CanvasSize = UDim2.new(0, 0, 0, alto + 12)
	return alto
end

function MenuUi:ShowTab(name)
	for n, page in pairs(self.pageOuter or self.pages) do page.Visible = (n == name) end
	for n, data in pairs(self.tabButtons) do
		local isActive = (n == name)
		TweenService:Create(data.btn, TWEEN_INFO, {TextColor3 = isActive and T.Text or T.Dim}):Play()
		TweenService:Create(data.line, TWEEN_INFO, {Size = isActive and UDim2.new(1, 0, 0, 2) or UDim2.new(0, 0, 0, 2)}):Play()
	end
	self.current = name
end

-- Bottom bar: 3 Roblox-style buttons // Barra inferior: 3 botones tipo Roblox
function MenuUi:BuildBottom(specs)
	local n = #specs
	for i, s in ipairs(specs) do
		local b = Instance.new("TextButton")
		b.BorderSizePixel = 0
		b.Size = UDim2.new(1 / n, -10, 1, 0)
		b.Position = UDim2.new((i - 1) / n, 5, 0, 0)
		b.BackgroundColor3 = s.color or T.Row
		b.TextColor3 = T.Text
		b.Font = FONT_BOLD
		b.TextSize = 16
		b.Text = "  " .. (s.key and ("[" .. s.key .. "]  ") or "") .. s.text
		b.Parent = self.bottom
		corner(b, 8)
		stroke(b, 1, T.Stroke)

		b.MouseEnter:Connect(function() TweenService:Create(b, TWEEN_INFO, {BackgroundColor3 = T.RowSel}):Play() end)
		b.MouseLeave:Connect(function() TweenService:Create(b, TWEEN_INFO, {BackgroundColor3 = s.color or T.Row}):Play() end)
		b.MouseButton1Click:Connect(s.cb)
	end
end

-- Live device name: updates when the player switches to a controller, etc.
-- // Nombre del dispositivo EN VIVO: cambia al usar mando, tactil, teclado...
function MenuUi.DeviceName()
	local UIS = game:GetService("UserInputService")
	local touch, keyboard, gamepad = false, false, false
	pcall(function()
		touch = UIS.TouchEnabled
		keyboard = UIS.KeyboardEnabled
		gamepad = UIS.GamepadEnabled
	end)
	local last = nil
	pcall(function() last = UIS:GetLastInputType() end)
	-- An EnumItem, as in Roblox (Gamepad1..Gamepad8). // Un EnumItem, como en Roblox.
	local usingPad = typeof(last) == "EnumItem" and string.sub(last.Name, 1, 7) == "Gamepad"
	if touch and not keyboard then
		return usingPad and "Mobile with Controller" or "Mobile"
	end
	if gamepad and not keyboard and not touch then
		return "Console"
	end
	return usingPad and "PC with Controller" or "PC"
end

-- Dim a row that the current mode does not allow to change. The click guards
-- live in Settings; this is only the visual cue (text goes darker).
-- // Atenua una fila que el modo actual no deja tocar. El bloqueo real esta en
-- // los guards de Settings; esto es solo el aviso visual (el texto se oscurece).
function MenuUi:SetRowEnabled(row, on)
	-- Por COLOR y no por TextTransparency: la transparencia de texto no esta
	-- puenteada al motor y el atenuado no se veia (parecia "bugueado").
	local col = on and T.Text or Color3.fromRGB(110, 114, 124)
	for _, d in ipairs(row:GetDescendants()) do
		if d:IsA("TextLabel") or d:IsA("TextButton") then d.TextColor3 = col end
	end
end

-- ── Tooltip flotante ─────────────────────────────────────────────────
-- SetTip(fila, texto): al dejar el raton encima sale una cajita que explica
-- que hace ese ajuste; al salir, desaparece.
function MenuUi:_ensureTip()
	if self.tip then return end
	local tip = Instance.new("Frame")
	tip.BorderSizePixel = 0
	tip.Name = "Tooltip"
	tip.Visible = false
	tip.Size = UDim2.new(0, 500, 0, 34)
	tip.BackgroundColor3 = T.Bar
	tip.ZIndex = 60
	tip.Parent = self.gui
	corner(tip, 8)
	stroke(tip, 1, T.Stroke)
	local l = label(tip, "", 13, T.Text)
	l.Position = UDim2.new(0, 12, 0, 0)
	l.Size = UDim2.new(1, -24, 1, 0)
	l.ZIndex = 61
	self.tip, self.tipLabel = tip, l
end

function MenuUi:SetTip(row, texto)
	self:_ensureTip()
	row.MouseEnter:Connect(function()
		self.tipLabel.Text = texto
		local p = row.AbsolutePosition
		local sz = row.AbsoluteSize
		self.tip.Position = UDim2.new(0, p.X + 20, 0, p.Y + sz.Y + 4)
		self.tip.Visible = true
	end)
	row.MouseLeave:Connect(function() self.tip.Visible = false end)
end

-- ── Barra deslizante (porcentajes) ───────────────────────────────────
-- Clic o arrastre fija el valor; el relleno azul y el % lo ensenan.
function MenuUi:Slider(page, text, get01, set01, fmt)
	local row = self:Row(page, text)
	local UIS = game:GetService("UserInputService")

	local bar = Instance.new("TextButton")
	bar.BorderSizePixel = 0
	bar.Text = ""
	bar.AnchorPoint = Vector2.new(1, 0.5)
	bar.Position = UDim2.new(1, -76, 0.5, 0)
	bar.Size = UDim2.new(0, 190, 0, 10)
	bar.BackgroundColor3 = Color3.fromRGB(52, 56, 68)
	bar.Parent = row
	corner(bar, 5)

	local fill = Instance.new("Frame")
	fill.BorderSizePixel = 0
	fill.BackgroundColor3 = T.AccentBg
	fill.Parent = bar
	corner(fill, 5)

	-- El circulito de agarre en la punta del relleno, como una barra de volumen.
	local knob = Instance.new("Frame")
	knob.BorderSizePixel = 0
	knob.Size = UDim2.new(0, 18, 0, 18)
	knob.AnchorPoint = Vector2.new(0.5, 0.5)
	knob.BackgroundColor3 = Color3.fromRGB(255, 255, 255)
	knob.ZIndex = 5
	knob.Parent = bar
	corner(knob, 9)

	local val = label(row, "", 14, T.Text, Enum.TextXAlignment.Right, true)
	val.AnchorPoint = Vector2.new(1, 0.5)
	val.Position = UDim2.new(1, -16, 0.5, 0)
	val.Size = UDim2.new(0, 52, 1, 0)

	local function paint()
		local f = math.clamp(get01(), 0, 1)
		fill.Size = UDim2.new(f, 0, 1, 0)
		knob.Position = UDim2.new(f, 0, 0.5, 0)
		val.Text = fmt(f)
	end
	paint()

	-- COMO WINDOWS: al pulsar empieza a seguir al raton, mientras se MANTENGA
	-- pulsado sigue, y al soltar se queda quieto. Ademas de escuchar el evento
	-- de soltar, cada movimiento COMPRUEBA el estado real del boton
	-- (Workspace:IsMouseDown): si el evento de soltar se pierde (p.ej. soltando
	-- fuera de la ventana), el arrastre se corta igual y el knob no se queda
	-- pegado al raton, que era justo el bug.
	local dragging = false
	local RunService = game:GetService("RunService")
	local Workspace = game:GetService("Workspace")
	local function botonPulsado()
		local down = false
		pcall(function() down = Workspace:IsMouseDown() end)
		return down
	end
	local function applyAt()
		local mx = UIS:GetMouseLocation().X
		local x0 = bar.AbsolutePosition.X
		local w = bar.AbsoluteSize.X
		if w <= 0 then return end
		set01(math.clamp((mx - x0) / w, 0, 1))
		paint()
	end
	-- El relleno y el circulito (Frames con filtro STOP) se COMIAN el clic:
	-- solo la parte vacia de la barra arrastraba. Una zona transparente por
	-- encima de TODO recibe el raton: mantener pulsado ajusta desde el primer
	-- pixel, se pulse donde se pulse.
	local zona = Instance.new("TextButton")
	zona.BorderSizePixel = 0
	zona.Text = ""
	zona.BackgroundTransparency = 1
	zona.Size = UDim2.new(1, 16, 1, 16)   -- un poco mas alta que la barra
	zona.Position = UDim2.new(0, -8, 0, -8)
	zona.ZIndex = 10
	zona.Parent = bar
	-- COMO WINDOWS, por bucle de frames: al pulsar arranca un bucle que CADA
	-- FRAME sigue al raton mientras el boton siga fisicamente pulsado
	-- (Workspace:IsMouseDown). Al soltar, el bucle muere solo. No depende de
	-- ningun evento de "soltar" ni de "mover": no puede quedarse pegado ni ir
	-- a saltos, que eran las dos quejas.
	local function empezarArrastre()
		if dragging then return end
		dragging = true
		task.spawn(function()
			while dragging do
				if not botonPulsado() then dragging = false break end
				applyAt()
				RunService.Heartbeat:Wait()
			end
		end)
	end
	zona.MouseButton1Down:Connect(empezarArrastre)
	bar.MouseButton1Down:Connect(empezarArrastre)
	self:_regFocus(page, row, {
		left = function() set01(math.clamp(get01() - 0.05, 0, 1)); paint() end,
		right = function() set01(math.clamp(get01() + 0.05, 0, 1)); paint() end,
	})
	UIS.InputEnded:Connect(function(input)
		if input.UserInputType == Enum.UserInputType.MouseButton1 then dragging = false end
	end)
	return val, row
end

-- Mouse wheel scrolling, like the old settings menu.
-- // Desplazamiento con la rueda, como el menu de ajustes anterior.
--
-- NO se usa el CanvasSize del ScrollingFrame: al escribirlo desde Luau el motor
-- recalcula la posicion de los hijos y el UIListLayout se colapsa (todas las filas
-- se amontonaban en la primera). Aqui se mueve la PAGINA dentro de Content, que
-- recorta, y se limita el recorrido a mano. Es lo mismo que hacia el menu nativo.
function MenuUi:_altoContenido(page)
	local alto = 0
	for _, hijo in ipairs(page:GetChildren()) do
		if hijo:IsA("GuiObject") then alto = alto + hijo.Size.Y.Offset + 8 end
	end
	return alto
end

-- Una vuelta de rueda: dz = +1 (arriba) o -1 (abajo). Separado de la conexion
-- para poder probarlo directamente sin simular eventos de raton.
function MenuUi:_onWheel(dz)
	local page = self.pages[self.current]
	if not page then return end
	-- Cuanto sobra por debajo: si cabe todo, no hay nada que desplazar.
	local visible = self.content.AbsoluteSize.Y
	local sobra = self:_altoContenido(page) - visible
	if sobra <= 0 then self.scrollY = 0 else
		self.scrollY = math.clamp(self.scrollY + dz * 90, -sobra, 0)
	end
	-- Suave: se anima hasta la posicion nueva en vez de saltar de golpe.
	-- El tween anterior se CANCELA: si conviven dos sobre la misma propiedad,
	-- puede ganar el viejo y la lista se queda a medio camino (medido: dos
	-- vueltas de rueda acababan en -90 en vez de -180).
	if self._scrollTween then pcall(function() self._scrollTween:Cancel() end) end
	self._scrollTween = TweenService:Create(page, TWEEN_INFO,
		{Position = UDim2.new(0, 0, 0, self.scrollY)})
	self._scrollTween:Play()
end

function MenuUi:_wireScroll()
	local UserInputService = game:GetService("UserInputService")
	self.scrollY = 0
	UserInputService.InputChanged:Connect(function(input)
		-- Si esta UI ya fue destruida (Rebuild por idioma / reset del PlayerGui),
		-- la conexion vieja sigue viva: no debe tocar nodos muertos.
		if not (self.gui and self.gui.Parent) then return end
		if not self.isOpen then return end
		if input.UserInputType ~= Enum.UserInputType.MouseWheel then return end
		self:_onWheel(input.Position.Z)
	end)

	-- ── MANDO: cruceta mueve el foco, izq/der ajusta, A activa, L1/R1 pestañas ──
	self._fi = 1
	local function focoLista()
		return (self._focus and self._focus[self.pages[self.current]]) or {}
	end
	local function pinta(lst)
		for i, f in ipairs(lst) do
			if f.row and f.row.Visible then
				f.row.BackgroundColor3 = (i == self._fi) and T.RowSel or T.Row
			end
		end
	end
	local function mueve(paso)
		local lst = focoLista()
		if #lst == 0 then return end
		local intentos = 0
		repeat
			self._fi = math.clamp(self._fi + paso, 1, #lst)
			intentos = intentos + 1
		until (lst[self._fi] and lst[self._fi].row.Visible) or intentos > #lst
		pinta(lst)
		-- La lista se desplaza sola para que el foco quede a la vista.
		self:_onWheel(0)   -- recalcula tope
		local aprox = -(self._fi - 3) * 58
		local page = self.pages[self.current]
		if page then
			local sobra = self:_altoContenido(page) - self.content.AbsoluteSize.Y
			if sobra > 0 then
				self.scrollY = math.clamp(aprox, -sobra, 0)
				TweenService:Create(page, TWEEN_INFO, {Position = UDim2.new(0, 0, 0, self.scrollY)}):Play()
			end
		end
	end
	UserInputService.InputBegan:Connect(function(input, gp)
		if not (self.gui and self.gui.Parent) then return end
		if gp or not self.isOpen then return end
		local kc = input.KeyCode
		if kc == Enum.KeyCode.DPadDown then mueve(1)
		elseif kc == Enum.KeyCode.DPadUp then mueve(-1)
		elseif kc == Enum.KeyCode.DPadLeft then
			local f = focoLista()[self._fi]
			if f and f.left then f.left() end
		elseif kc == Enum.KeyCode.DPadRight then
			local f = focoLista()[self._fi]
			if f and f.right then f.right() end
		elseif kc == Enum.KeyCode.ButtonA then
			local f = focoLista()[self._fi]
			if f and (f.activate or f.right) then (f.activate or f.right)() end
		elseif kc == Enum.KeyCode.ButtonR1 or kc == Enum.KeyCode.ButtonL1 then
			-- Cambiar de pestaña con los gatillos superiores.
			local orden = self._tabOrder or {}
			local idx = 1
			for i, nm in ipairs(orden) do if nm == self.current then idx = i end end
			idx = idx + ((kc == Enum.KeyCode.ButtonR1) and 1 or -1)
			if idx < 1 then idx = #orden elseif idx > #orden then idx = 1 end
			if orden[idx] then self:ShowTab(orden[idx]); self._fi = 1 end
		end
	end)
end

-- Panel transparency, saved with the language store.
-- // Transparencia del panel; se guarda junto al idioma.
function MenuUi:SetPanelTransparency(f)
	self._panelTransp = math.clamp(f, 0, 0.85)
	self.panel.BackgroundTransparency = self._panelTransp
	pcall(function()
		game:GetService("DataStoreService"):GetDataStore("GodotLuauMenu"):SetAsync("PanelTransparency", self._panelTransp)
	end)
end
function MenuUi:GetPanelTransparency()
	if self._panelTransp == nil then
		local v = nil
		pcall(function()
			v = game:GetService("DataStoreService"):GetDataStore("GodotLuauMenu"):GetAsync("PanelTransparency")
		end)
		self._panelTransp = tonumber(v) or 0
		self.panel.BackgroundTransparency = self._panelTransp
	end
	return self._panelTransp
end

-- Performance overlay (FPS, frame time, ping), toggled from Settings.
-- // Estadisticas de rendimiento (FPS, tiempo de frame, ping).
function MenuUi:SetPerfVisible(on)
	if not self.perf then
		local pf = Instance.new("Frame")
		pf.BorderSizePixel = 0
		pf.Name = "PerfStats"
		pf.AnchorPoint = Vector2.new(1, 0)
		pf.Position = UDim2.new(1, -10, 0, 10)
		pf.Size = UDim2.new(0, 175, 0, 84)
		pf.BackgroundColor3 = T.Bar
		pf.BackgroundTransparency = 0.25
		pf.Parent = self.gui
		corner(pf, 8)
		local l = label(pf, "", 13, T.Green or T.Text)
		l.Position = UDim2.new(0, 10, 0, 0)
		l.Size = UDim2.new(1, -20, 1, 0)
		self.perf, self.perfLabel = pf, l
		local Workspace = game:GetService("Workspace")
		local acc = 0
		game:GetService("RunService").Heartbeat:Connect(function(dt)
			if not self.perf.Visible then return end
			acc = acc + dt
			if acc < 0.25 then return end
			acc = 0
			local fps = 0
			pcall(function() fps = Workspace:GetFPS() end)
			local ms = fps > 0 and (1000 / fps) or 0
			local ping = "--"
			pcall(function()
				local ns = game:FindFirstChild("NetworkService")
				if ns then ping = math.floor(ns:GetPingMs() + 0.5) .. " ms" end
			end)
			local mem = 0
			pcall(function() mem = Workspace:GetMemoryMB() end)
			local NL = string.char(10)
			self.perfLabel.Text = string.format(
				"FPS: %d" .. NL .. "Frame: %.1f ms" .. NL .. "Mem: %.1f MB" .. NL .. "Ping: %s",
				math.floor(fps + 0.5), ms, mem, ping)
		end)
	end
	self.perf.Visible = on
end

-- Open/Close animations // Animaciones de abrir/cerrar
function MenuUi:Open()
	self.overlay.Visible = true
	self.isOpen = true
	-- Se avisa al motor AQUI y no fuera: asi da igual como se haya abierto el menu
	-- (Esc, boton-logo u otro script). Mientras este abierto, la rueda deja de
	-- acercar/alejar la camara y sirve para desplazar el menu, como en Roblox.
	pcall(function() game:GetService("Workspace"):SetMenuOpen(true) end)
	TweenService:Create(self.overlay, TWEEN_INFO, {BackgroundTransparency = 0.3}):Play()
	TweenService:Create(self.panel, TWEEN_INFO, {BackgroundTransparency = 0, Position = UDim2.new(0.5, 0, 0.5, 0)}):Play()
end

function MenuUi:Close()
	self.isOpen = false
	pcall(function() game:GetService("Workspace"):SetMenuOpen(false) end)
	local t1 = TweenService:Create(self.overlay, TWEEN_INFO, {BackgroundTransparency = 1})
	local t2 = TweenService:Create(self.panel, TWEEN_INFO, {BackgroundTransparency = 1, Position = UDim2.new(0.5, 0, 0.5, 20)})
	t1:Play()
	t2:Play()
	t1.Completed:Connect(function()
		if not self.isOpen then self.overlay.Visible = false end
	end)
end

-- OJO: habia DOS funciones llamadas Toggle (el WIDGET de interruptor y el
-- abrir/cerrar del menu) y en Lua la segunda PISA a la primera: los
-- interruptores de Settings (V-Sync, camaras) no se creaban NUNCA y cada
-- ui:Toggle(...) del build abria/cerraba el menu. Ahora una sola entrada
-- despacha: con argumentos es el widget, sin argumentos abre/cierra.
function MenuUi:Toggle(page, text, subtitle, getOn, onToggle)
	if page ~= nil then
		return self:_ToggleWidget(page, text, subtitle, getOn, onToggle)
	end
	if self.isOpen then self:Close() else self:Open() end
end

-- Topbar: the logo button opens the menu (like the Roblox logo) and, next to it,
-- a pill with the menu and chat buttons. Same sizes as Roblox's bar on PC (a
-- 44 px round button at 16,12 and a 96 px pill), so games that put their own
-- buttons to the right of it (GuiService.TopbarInset) look the same as in Roblox.
-- // Barra de arriba: el logo abre el menu (como el logo de Roblox) y al lado va
-- // una pastilla con menu y chat. Mismas medidas que la barra de Roblox en PC
-- // (boton redondo de 44 px en 16,12 y pastilla de 96 px): los juegos que ponen
-- // sus botones a la derecha (GuiService.TopbarInset) se ven igual que en Roblox.
-- Pon tu logo en res://gamelogo.png y se usa solo. Si no hay imagen, se muestra el
-- NOMBRE en una pastilla, asi que siempre se ve algo digno (nunca un cuadro blanco).
local FONDO_BARRA = Color3.fromRGB(18, 18, 21)   -- botones de la barra: oscuros, casi opacos
local TRANSP_BARRA = 0.08

-- Boton redondo (o pastilla) de la barra, con el realce al pasar el raton.
local function botonBarra(nombre, ancho, padre)
	local b = Instance.new("TextButton")
	b.BorderSizePixel = 0
	b.Name = nombre
	b.AutoButtonColor = false
	b.Size = UDim2.new(0, ancho, 1, 0)
	b.BackgroundColor3 = FONDO_BARRA
	b.BackgroundTransparency = TRANSP_BARRA
	b.TextColor3 = T.Text
	b.Font = FONT_BOLD
	b.TextSize = 14
	b.Text = ""
	b.Parent = padre
	corner(b, 22)
	return b
end

-- Icono de menu: tres rayas blancas.
local function iconoMenu(padre)
	for i = -1, 1 do
		local raya = Instance.new("Frame")
		raya.BorderSizePixel = 0
		raya.AnchorPoint = Vector2.new(0.5, 0.5)
		raya.Position = UDim2.new(0.5, 0, 0.5, i * 6)
		raya.Size = UDim2.new(0, 18, 0, 2)
		raya.BackgroundColor3 = T.Text
		raya.Parent = padre
		corner(raya, 1)
	end
end

-- Icono de chat: un bocadillo con su pico. Devuelve el contorno (para apagarlo).
local function iconoChat(padre)
	local globo = Instance.new("Frame")
	globo.BorderSizePixel = 0
	globo.AnchorPoint = Vector2.new(0.5, 0.5)
	globo.Position = UDim2.new(0.5, 0, 0.5, -1)
	globo.Size = UDim2.new(0, 20, 0, 15)
	globo.BackgroundTransparency = 1
	globo.Parent = padre
	corner(globo, 4)
	local borde = stroke(globo, 2, T.Text)
	local pico = Instance.new("Frame")
	pico.BorderSizePixel = 0
	pico.AnchorPoint = Vector2.new(0.5, 0.5)
	pico.Position = UDim2.new(0, 5, 1, 1)
	pico.Size = UDim2.new(0, 5, 0, 5)
	pico.Rotation = 45
	pico.BackgroundColor3 = T.Text
	pico.Parent = globo
	return borde, pico
end

function MenuUi:BuildTopbar(nombre, rutaImagen)
	local bar = Instance.new("Frame")
	bar.BorderSizePixel = 0
	bar.Name = "Topbar"
	bar.Position = UDim2.new(0, 16, 0, 12)
	bar.Size = UDim2.new(0, 148, 0, 44)
	bar.BackgroundTransparency = 1
	bar.Parent = self.gui

	local lay = Instance.new("UIListLayout")
	lay.FillDirection = Enum.FillDirection.Horizontal
	lay.Padding = UDim.new(0, 8)
	lay.VerticalAlignment = Enum.VerticalAlignment.Center
	lay.SortOrder = Enum.SortOrder.LayoutOrder
	lay.Parent = bar

	-- REDONDO con la imagen del logo, como el de Roblox. Si no hay imagen, cae al
	-- nombre en una pastilla.
	local btn = botonBarra("LogoButton", 44, bar)
	btn.LayoutOrder = 1
	if rutaImagen and rutaImagen ~= "" then
		local img = Instance.new("ImageLabel")
		img.BorderSizePixel = 0
		img.Size = UDim2.new(1, -14, 1, -14)
		img.Position = UDim2.new(0, 7, 0, 7)
		img.BackgroundTransparency = 1
		img.ScaleType = Enum.ScaleType.Fit
		img.Image = rutaImagen
		img.Parent = btn
		-- La imagen se COME el clic (TextureRect con filtro STOP): una zona
		-- transparente por encima garantiza que el logo siempre abra el menu.
		local zona = Instance.new("TextButton")
		zona.BorderSizePixel = 0
		zona.Text = ""
		zona.BackgroundTransparency = 1
		zona.Size = UDim2.new(1, 0, 1, 0)
		zona.ZIndex = 10
		zona.Parent = btn
		zona.MouseButton1Click:Connect(function() self:Toggle() end)
	else
		btn.Size = UDim2.new(0, 146, 1, 0)
		btn.Text = "  " .. (nombre or "Menu") .. "  "
	end
	btn.MouseButton1Click:Connect(function() self:Toggle() end)

	-- La pastilla: menu a la izquierda y chat a la derecha.
	local pill = botonBarra("MenuChat", 96, bar)
	pill.LayoutOrder = 2
	local menuBtn = Instance.new("TextButton")
	menuBtn.BorderSizePixel = 0
	menuBtn.Name = "MenuButton"
	menuBtn.Size = UDim2.new(0.5, 0, 1, 0)
	menuBtn.BackgroundTransparency = 1
	menuBtn.Text = ""
	menuBtn.Parent = pill
	iconoMenu(menuBtn)
	menuBtn.MouseButton1Click:Connect(function() self:Toggle() end)

	local chatBtn = Instance.new("TextButton")
	chatBtn.BorderSizePixel = 0
	chatBtn.Name = "ChatButton"
	chatBtn.Position = UDim2.new(0.5, 0, 0, 0)
	chatBtn.Size = UDim2.new(0.5, 0, 1, 0)
	chatBtn.BackgroundTransparency = 1
	chatBtn.Text = ""
	chatBtn.Parent = pill
	local borde, pico = iconoChat(chatBtn)
	chatBtn.MouseButton1Click:Connect(function()
		-- Alterna la ventana de chat del motor (abre si esta cerrada y al reves),
		-- que es lo que hace el boton de chat de la topbar de Roblox.
		local abierto = false
		pcall(function() abierto = game:GetService("Workspace"):ToggleChat() end)
		borde.Transparency = abierto and 0 or 0.5
		pico.BackgroundTransparency = abierto and 0 or 0.5
	end)

	-- Realce al pasar el raton, como los de Roblox.
	for _, b in ipairs({ btn, pill }) do
		local zonaRaton = (b == pill) and { menuBtn, chatBtn } or { b }
		for _, z in ipairs(zonaRaton) do
			z.MouseEnter:Connect(function() TweenService:Create(b, TWEEN_INFO, {BackgroundTransparency = 0}):Play() end)
			z.MouseLeave:Connect(function() TweenService:Create(b, TWEEN_INFO, {BackgroundTransparency = TRANSP_BARRA}):Play() end)
		end
	end

	-- Donde acaba la barra (logo + hueco + pastilla): los juegos ponen sus botones
	-- a la derecha (GuiService.TopbarInset, 58 px de alto como en Roblox).
	local GuiService = game:GetService("GuiService")
	if GuiService._glFijarBarra then
		GuiService:_glFijarBarra(16 + btn.Size.X.Offset + 8 + 96, 58)
	end

	self.topbar, self.logoButton, self.chatButton, self.menuButton = bar, btn, chatBtn, menuBtn
	return btn
end

return MenuUi
