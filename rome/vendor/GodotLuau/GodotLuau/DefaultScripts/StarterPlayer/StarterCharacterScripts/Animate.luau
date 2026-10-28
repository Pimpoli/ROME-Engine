-- > GodotLuau — PimpoliDev
-- Animate — las animaciones del personaje (estar quieto, andar, correr, saltar,
-- caer, trepar, sentarse, nadar, herramientas y gestos). Se clona al nacer
-- (script.Parent = el personaje).
-- /// Animate — the character's animations (idle, walk, run, jump, fall, climb,
-- sit, swim, tools and emotes). Cloned on spawn (script.Parent = the character).
--
-- Como en Roblox, cada estado es un StringValue con sus Animation dentro: un
-- juego puede cambiar `Animate.walk.WalkAnim.AnimationId` y se usa la suya.
-- Los ids que vienen puestos son los de las animaciones por defecto de Roblox:
-- GodotLuau los resuelve a las animaciones de sus avatares R6 y R15.
-- /// Like in Roblox, every state is a StringValue holding its Animations: a game
-- can change `Animate.walk.WalkAnim.AnimationId` and its own is used. The ids set
-- here are Roblox's default ones; GodotLuau maps them to its R6/R15 avatars.

local RunService = game:GetService("RunService")

local personaje = script.Parent
local humanoide = personaje:WaitForChild("Humanoid")
local esR15 = humanoide.RigType == Enum.HumanoidRigType.R15

-- ── Ranuras: estado -> { {nombre, id, peso}, ... } ──────────────────────
-- /// Slots: state -> { {name, id, weight}, ... }
local function id(n) return "rbxassetid://" .. tostring(n) end
local RANURAS = esR15 and {
	idle = { { "Animation1", 507766388, 9 }, { "Animation2", 507766666, 1 } },
	walk = { { "WalkAnim", 913402848, 10 } },
	run = { { "RunAnim", 913376220, 10 } },
	jump = { { "JumpAnim", 507765000, 10 } },
	fall = { { "FallAnim", 507767968, 10 } },
	climb = { { "ClimbAnim", 507765644, 10 } },
	sit = { { "SitAnim", 2506281703, 10 } },
	swim = { { "Swim", 913384386, 10 } },
	swimidle = { { "SwimIdle", 913389285, 10 } },
	toolnone = { { "ToolNoneAnim", 507768375, 10 } },
	toolslash = { { "ToolSlashAnim", 522635514, 10 } },
	toollunge = { { "ToolLungeAnim", 522638767, 10 } },
	wave = { { "WaveAnim", 507770239, 10 } },
	point = { { "PointAnim", 507770453, 10 } },
	dance = { { "Animation1", 507771019, 10 }, { "Animation2", 507771955, 10 }, { "Animation3", 507772104, 10 } },
	dance2 = { { "Animation1", 507776043, 10 }, { "Animation2", 507776720, 10 }, { "Animation3", 507776879, 10 } },
	dance3 = { { "Animation1", 507777268, 10 }, { "Animation2", 507777451, 10 }, { "Animation3", 507777623, 10 } },
	laugh = { { "LaughAnim", 507770818, 10 } },
	cheer = { { "CheerAnim", 507770677, 10 } },
} or {
	idle = { { "Animation1", 180435571, 9 }, { "Animation2", 180435792, 1 } },
	walk = { { "WalkAnim", 180426354, 10 } },
	run = { { "RunAnim", 180426354, 10 } },
	jump = { { "JumpAnim", 125750702, 10 } },
	fall = { { "FallAnim", 180436148, 10 } },
	climb = { { "ClimbAnim", 180436334, 10 } },
	sit = { { "SitAnim", 178130996, 10 } },
	toolnone = { { "ToolNoneAnim", 182393478, 10 } },
	toolslash = { { "ToolSlashAnim", 129967390, 10 } },
	toollunge = { { "ToolLungeAnim", 129967478, 10 } },
	wave = { { "WaveAnim", 128777973, 10 } },
	point = { { "PointAnim", 128853357, 10 } },
	dance = { { "Animation1", 182435998, 10 }, { "Animation2", 182491037, 10 }, { "Animation3", 182491065, 10 } },
	dance2 = { { "Animation1", 182436842, 10 }, { "Animation2", 182491248, 10 }, { "Animation3", 182491277, 10 } },
	dance3 = { { "Animation1", 182436935, 10 }, { "Animation2", 182491368, 10 }, { "Animation3", 182491423, 10 } },
	laugh = { { "LaughAnim", 129423131, 10 } },
	cheer = { { "CheerAnim", 129423030, 10 } },
}

-- Crea lo que falte (sin tocar lo que el juego ya haya puesto).
-- /// Creates whatever is missing (without touching what the game already set).
for estado, lista in pairs(RANURAS) do
	local ranura = script:FindFirstChild(estado)
	if not ranura then
		ranura = Instance.new("StringValue")
		ranura.Name = estado
		ranura.Parent = script
	end
	for _, def in ipairs(lista) do
		if not ranura:FindFirstChild(def[1]) then
			local a = Instance.new("Animation")
			a.Name = def[1]
			a.AnimationId = id(def[2])
			local peso = Instance.new("NumberValue")
			peso.Name = "Weight"
			peso.Value = def[3]
			peso.Parent = a
			a.Parent = ranura
		end
	end
end

local animador = humanoide:FindFirstChildOfClass("Animator") or humanoide

-- ── Pistas: se cargan al pedirlas y se rehacen si el juego cambia el id ──
-- /// Tracks: loaded on demand and reloaded if the game changes the id.
local pistas = {}   -- Animation -> { id = AnimationId cargado, pista = AnimationTrack }
local function pistaDe(anim)
	local p = pistas[anim]
	if p and p.id == anim.AnimationId then return p.pista end
	if p and p.pista then p.pista:Stop(0) end
	local ok, pista = pcall(function() return animador:LoadAnimation(anim) end)
	if not ok then return nil end
	pistas[anim] = { id = anim.AnimationId, pista = pista }
	return pista
end

-- Elige una Animation de la ranura por su peso (Weight).
-- /// Picks one of the slot's Animations by its weight.
local function elegir(estado)
	local ranura = script:FindFirstChild(estado)
	if not ranura then return nil end
	local opciones, total = {}, 0
	for _, a in ipairs(ranura:GetChildren()) do
		if a:IsA("Animation") then
			local w = a:FindFirstChild("Weight")
			local peso = (w and w.Value) or 1
			if peso > 0 then
				total += peso
				table.insert(opciones, { a, peso })
			end
		end
	end
	if total <= 0 then return nil end
	local r = math.random() * total
	for _, o in ipairs(opciones) do
		r -= o[2]
		if r <= 0 then return o[1] end
	end
	return opciones[#opciones][1]
end

local estadoActual, pistaActual = nil, nil
local function tocar(estado, fundido, velocidad)
	if estadoActual == estado and pistaActual and pistaActual.IsPlaying then
		if velocidad then pistaActual:AdjustSpeed(velocidad) end
		return
	end
	local anim = elegir(estado)
	local pista = anim and pistaDe(anim)
	if pistaActual and pistaActual ~= pista then pistaActual:Stop(fundido) end
	estadoActual, pistaActual = estado, pista
	if pista then
		pista:Play(fundido)
		pista:AdjustSpeed(velocidad or 1)
	end
end

-- El idle elige otra variante (respirar / mirar alrededor) cada vuelta.
-- /// The idle picks another variant (breathe / look around) every loop.
local function alAcabarIdle()
	if estadoActual == "idle" then
		estadoActual = nil
		tocar("idle", 0.2)
	end
end

-- ── Herramientas: toolnone mientras se lleva una; slash/lunge al usarla ──
-- /// Tools: toolnone while one is held; slash/lunge when it is used.
local herramienta, pistaHerramienta, pistaGolpe = nil, nil, nil
local function soltarHerramienta()
	if pistaHerramienta then pistaHerramienta:Stop(0.1) end
	herramienta, pistaHerramienta = nil, nil
end
local function cogerHerramienta(tool)
	soltarHerramienta()
	if not tool:FindFirstChild("Handle") then return end
	herramienta = tool
	local anim = elegir("toolnone")
	pistaHerramienta = anim and pistaDe(anim)
	if pistaHerramienta then pistaHerramienta:Play(0.1) end
end
personaje.ChildAdded:Connect(function(h) if h:IsA("Tool") then cogerHerramienta(h) end end)
personaje.ChildRemoved:Connect(function(h) if h == herramienta then soltarHerramienta() end end)
local function golpeDeHerramienta()
	-- Las Tool clasicas avisan con un StringValue "toolanim" (Slash / Lunge).
	-- /// Classic Tools signal with a "toolanim" StringValue (Slash / Lunge).
	if not herramienta then return end
	local aviso = herramienta:FindFirstChild("toolanim")
	if not aviso then return end
	local cual = aviso.Value == "Lunge" and "toollunge" or "toolslash"
	aviso:Destroy()
	local anim = elegir(cual)
	pistaGolpe = anim and pistaDe(anim)
	if pistaGolpe then pistaGolpe:Play(0.1) end
end

-- ── Gestos: /e wave, /e dance... y la funcion PlayEmote de Roblox ────────
-- /// Emotes: /e wave, /e dance... and Roblox's PlayEmote function.
local GESTOS = { wave = true, point = true, dance = true, dance2 = true, dance3 = true, laugh = true, cheer = true }
local function hacerGesto(nombre)
	if not GESTOS[nombre] or not (estadoActual == "idle" or estadoActual == nil) then return false end
	tocar(nombre, 0.1)
	return true
end
local jugarGesto = script:FindFirstChild("PlayEmote")
if not jugarGesto then
	jugarGesto = Instance.new("BindableFunction")
	jugarGesto.Name = "PlayEmote"
	jugarGesto.Parent = script
end
jugarGesto.OnInvoke = function(gesto)
	if typeof(gesto) == "string" then return hacerGesto(gesto) end
	return false
end
local jugador = game:GetService("Players"):GetPlayerFromCharacter(personaje)
if jugador then
	pcall(function()
		jugador.Chatted:Connect(function(msg)
			local nombre = string.match(msg, "^/e%s+(%w+)") or string.match(msg, "^/emote%s+(%w+)")
			if nombre then hacerGesto(string.lower(nombre)) end
		end)
	end)
end

-- ── Estados del Humanoid ─────────────────────────────────────────────
-- /// Humanoid states.
local enElAire, tiempoSalto, velocidadSuelo = false, 0, 0

local function moverse(velocidad)
	velocidadSuelo = velocidad
	if enElAire then return end
	if velocidad > 0.75 then
		-- El paso de cada animacion cuadra con una velocidad: se escala para
		-- que los pies no patinen. /// Scaled so the feet do not slide.
		if esR15 then
			if velocidad < 13 then tocar("walk", 0.2, velocidad / 10)
			else tocar("run", 0.2, velocidad / 16) end
		else
			tocar("walk", 0.2, velocidad / 16)
		end
	elseif not GESTOS[estadoActual or ""] then
		tocar("idle", 0.2)
	end
end

humanoide.Running:Connect(moverse)
humanoide.Jumping:Connect(function(activo)
	if activo then
		enElAire = true
		tiempoSalto = 0.31
		tocar("jump", 0.1)
	end
end)
humanoide.FreeFalling:Connect(function(activo)
	enElAire = activo
	if activo then
		if tiempoSalto <= 0 then tocar("fall", 0.2) end
	else
		moverse(velocidadSuelo)
	end
end)
humanoide.Seated:Connect(function(activo)
	if activo then tocar("sit", 0.5) else moverse(0) end
end)
humanoide.StateChanged:Connect(function(_, nuevo)
	if nuevo == Enum.HumanoidStateType.Climbing then
		tocar("climb", 0.1, 1)
	elseif nuevo == Enum.HumanoidStateType.Swimming then
		tocar(esR15 and "swimidle" or "idle", 0.4)
	elseif nuevo == Enum.HumanoidStateType.Landed then
		enElAire = false
		moverse(velocidadSuelo)
	end
end)
pcall(function()
	humanoide.Climbing:Connect(function(velocidad) tocar("climb", 0.1, math.max(velocidad, 0) / 8) end)
	humanoide.Swimming:Connect(function(velocidad)
		if not esR15 then return end
		if velocidad > 1 then tocar("swim", 0.4, velocidad / 12) else tocar("swimidle", 0.4) end
	end)
end)
humanoide.Died:Connect(function()
	if pistaActual then pistaActual:Stop(0.2) end
	soltarHerramienta()
	estadoActual, pistaActual = nil, nil
end)

RunService.Heartbeat:Connect(function(dt)
	if tiempoSalto > 0 then
		tiempoSalto -= dt
		if tiempoSalto <= 0 and enElAire then tocar("fall", 0.2) end
	end
	if estadoActual == "idle" and pistaActual and not pistaActual.IsPlaying then alAcabarIdle() end
	if herramienta then golpeDeHerramienta() end
end)

-- Arranca quieto. /// Starts idle.
tocar("idle", 0.1)
