#ifndef GL_LUAU_TEMPLATES_GEN_H
#define GL_LUAU_TEMPLATES_GEN_H

// ════════════════════════════════════════════════════════════════════
//  GENERADO AUTOMATICAMENTE — NO EDITAR A MANO
//
//  Este header lo escribe tools/generar_plantillas_luau.py a partir de
//  los archivos .luau de GodotLuau/DefaultScripts/. Edita el .luau, no
//  esto: cualquier cambio aqui se pierde al regenerar.
//
//  Por que existe: las plantillas viven como .luau de verdad (con
//  autocompletado y faciles de encontrar), pero la DLL tiene que seguir
//  funcionando sin esa carpeta (juego exportado, instalacion parcial).
//  Estas constantes son esa copia embebida de reserva; en ejecucion
//  gl_luau_template() prefiere el archivo de disco si existe.
//
//  Tras tocar un .luau:  python tools/generar_plantillas_luau.py
// ════════════════════════════════════════════════════════════════════

// ── StarterPlayer/StarterCharacterScripts/Health.luau ──
inline constexpr char LUAU_TEMPLATE_HEALTH_PATH[] = "StarterPlayer/StarterCharacterScripts/Health.luau";
inline constexpr char LUAU_TEMPLATE_HEALTH[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- Health.lua — ServerScript — StarterCharacterScripts
-- Regenera la vida con el tiempo. Se clona al personaje al nacer (script.Parent = el jugador).
-- /// Regenerates health over time. Cloned onto the character on spawn (script.Parent = the player).

local RunService = game:GetService("RunService")

local humanoid = script.Parent:FindFirstChild("Humanoid")
if not humanoid then
    dwarn("[Health] Humanoid no encontrado en el personaje")
    return
end

dprint("[Health] Regeneracion activa: " .. script.Parent.Name)

-- La regeneración empieza REGEN_DELAY s tras el ÚLTIMO daño (recibir daño reinicia el contador).
-- /// Regen starts REGEN_DELAY s after the LAST hit (taking damage resets the timer).
local REGEN_DELAY = 3.0    -- s sin daño para regenerar /// seconds without damage before regen
local REGEN_RATE  = 0.01   -- fracción de MaxHealth por segundo (1%) /// fraction of MaxHealth per second (1%)

local last_health = humanoid.Health
local since_damage = REGEN_DELAY  -- permite regenerar desde el inicio /// allow regen from the start

humanoid.HealthChanged:Connect(function(newHP)
    if newHP < last_health then
        since_damage = 0.0  -- recibió daño → reinicia el contador /// took damage → reset the timer
    end
    last_health = newHP
end)

humanoid.Died:Connect(function()
    dprint("[Health] " .. script.Parent.Name .. " ha muerto.")
end)

-- Cada frame: si pasó el retardo y falta vida, sube un poco hacia MaxHealth.
-- /// Each frame: if the delay passed and health is missing, ramp up toward MaxHealth.
RunService.Heartbeat:Connect(function(dt)
    since_damage = since_damage + dt
    if since_damage < REGEN_DELAY then return end
    if humanoid.Health > 0 and humanoid.Health < humanoid.MaxHealth then
        local nuevo = humanoid.Health + humanoid.MaxHealth * REGEN_RATE * dt
        humanoid.Health = math.min(humanoid.MaxHealth, nuevo)
        last_health = humanoid.Health
    end
end)
)LUAU";

// ── StarterPlayer/StarterCharacterScripts/Animate.luau ──
inline constexpr char LUAU_TEMPLATE_ANIMATE_PATH[] = "StarterPlayer/StarterCharacterScripts/Animate.luau";
inline constexpr char LUAU_TEMPLATE_ANIMATE[] = R"LUAU(
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
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/PlayerModule.luau ──
inline constexpr char LUAU_TEMPLATE_PLAYER_MODULE_PATH[] = "StarterPlayer/StarterPlayerScripts/PlayerModule.luau";
inline constexpr char LUAU_TEMPLATE_PLAYER_MODULE[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- PlayerModule.lua — LocalScript — StarterPlayerScripts
-- Cerebro del jugador: carga los módulos de Modules/ y los une. Se clona al
-- jugador al nacer (script.Parent = el jugador).
-- /// Player brain: loads the modules in Modules/ and wires them. Cloned onto
-- /// the player on spawn (script.Parent = the player).

local RunService = game:GetService("RunService")

-- El jugador tiene el Humanoid y la carpeta Modules/. /// The player holds the Humanoid and the Modules/ folder.
local character = script.Parent
local humanoid  = character:FindFirstChild("Humanoid")

if not humanoid then
    dwarn("[PlayerModule] Humanoid no encontrado en el jugador")
    return
end

-- Cargar los módulos (movimiento, cámara, chat) desde Modules/.
-- /// Load the modules (movement, camera, chat) from Modules/.
local ControlModule = require(script.Parent.Modules.ControlModule)
local CameraModule  = require(script.Parent.Modules.CameraModule)
local ChatModule    = require(script.Parent.Modules.ChatModule)

-- Arrancar cada módulo. /// Start each module.
ControlModule:Initialize()
CameraModule:Apply()
ChatModule:Initialize()

-- El menú de Escape lo monta Modules/Menu, ENTERO EN LUAU y editable (añade o
-- quita pestañas, filas y botones). Al arrancar avisa al motor y el menú nativo
-- de C++ se aparta. El pcall es la red de seguridad: si este módulo falta o
-- falla, el nativo sigue en pie y nunca te quedas sin menú.
-- /// The Escape menu is built by Modules/Menu, entirely in Luau, so it is fully
-- /// editable. It tells the engine to stand its native menu down. The pcall is
-- /// the safety net: if this module is missing or throws, the native one stays.
local okMenu, errMenu = pcall(function()
    local Menu = require(script.Parent.Modules.Menu)
    Menu.Init(game:GetService("Players").LocalPlayer)
end)
-- Si falla, AVISA. Antes el pcall se tragaba el error y te quedabas con el menú
-- nativo sin saber por qué: había que adivinar.
-- /// If it fails, SAY SO. The pcall used to swallow the error silently.
if not okMenu then warn("[PlayerModule] El menu de Luau no arranco: " .. tostring(errMenu)) end

-- Pasar la velocidad y el salto del ControlModule al Humanoid.
-- /// Copy the ControlModule's walk speed and jump power to the Humanoid.
humanoid.WalkSpeed = ControlModule.WalkSpeed
humanoid.JumpPower = ControlModule.JumpPower

dprint("[PlayerModule] Jugador listo! Speed=" .. ControlModule.WalkSpeed)

humanoid.Died:Connect(function()
    dprint("[PlayerModule] El personaje ha muerto.")
end)

-- Cada frame: el ControlModule calcula la velocidad (sprint suave) y la aplica.
-- /// Each frame: ControlModule computes the speed (smooth sprint) and applies it.
-- Ultima velocidad que ESCRIBIMOS nosotros. Sirve para distinguir "la cambio el
-- juego" de "la cambiamos nosotros el frame pasado".
local ultimaEscrita = humanoid.WalkSpeed

RunService.Heartbeat:Connect(function(dt)
    if humanoid.Health <= 0 then return end

    -- Si el JUEGO toco Humanoid.WalkSpeed, manda el juego. Antes esta linea no
    -- existia y el bucle devolvia la velocidad a los 16 de fabrica en el
    -- siguiente frame: por eso "no se puede cambiar la velocidad del jugador".
    -- El sprint sigue funcionando, ahora escalando sobre la velocidad del juego.
    -- /// If the GAME changed Humanoid.WalkSpeed, the game wins.
    if math.abs(humanoid.WalkSpeed - ultimaEscrita) > 0.01 then
        ControlModule:SetWalkSpeed(humanoid.WalkSpeed)
    end

    ControlModule:Update(dt)
    local speed = ControlModule:GetCurrentSpeed()
    if math.abs(humanoid.WalkSpeed - speed) > 0.01 then
        humanoid.WalkSpeed = speed
    end
    ultimaEscrita = humanoid.WalkSpeed

    -- ══ TU CÓDIGO POR FRAME AQUÍ /// YOUR PER-FRAME CODE HERE ══
    -- local x, z = ControlModule:GetMoveVector()   -- dirección del teclado /// keyboard direction
    -- local st, stMax = ControlModule:GetStamina() -- estamina actual /// current stamina
    -- if ChatModule:IsOpen() then ... end
    -- ══════════════════════════════════════════════════════════
end)
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/ControlModule.luau ──
inline constexpr char LUAU_TEMPLATE_CONTROL_MODULE_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/ControlModule.luau";
inline constexpr char LUAU_TEMPLATE_CONTROL_MODULE[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- ControlModule.lua — ModuleScript — Modules/
-- Elige la plataforma y reparte el trabajo al sub-módulo correcto (PC / móvil /
-- consola). Es el "router" del movimiento.
-- /// Picks the platform and routes the work to the right sub-module (PC / mobile
-- /// / console). This is the movement "router".

local ControlModule = {}

-- Sub-módulos (hijos de este módulo en el árbol). /// Sub-modules (children of this module in the tree).
local PCModule      = require(script.PCModule)
local MobileModule  = require(script.MobileModule)
local ConsoleModule = require(script.ConsoleModule)

-- CONFIG: Platform "" = auto · "PC" teclado · "Mobile" táctil · "Console" mando.
-- /// CONFIG: Platform "" = auto · "PC" keyboard · "Mobile" touch · "Console" gamepad.
ControlModule.Platform  = "PC"  -- fuerza una plataforma (o "" para auto) /// force a platform (or "" for auto)
ControlModule.WalkSpeed = 16
ControlModule.JumpPower = 50

local active = PCModule

function ControlModule:Initialize()
    local p = self.Platform
    if p == "Mobile" then
        active = MobileModule
    elseif p == "Console" then
        active = ConsoleModule
    else
        active = PCModule
    end
    -- Copiar velocidad/salto del sub-módulo activo. /// Copy speed/jump from the active sub-module.
    self.WalkSpeed = active.WalkSpeed
    self.JumpPower = active.JumpPower
    if active.Initialize then active:Initialize() end
    dprint("[ControlModule] Plataforma: " .. (p == "" and "Auto(PC)" or p))
end

-- Lo llama PlayerModule cada frame (sprint suave, estamina…). /// Called by PlayerModule each frame (smooth sprint, stamina…).
function ControlModule:Update(dt)
    if active and active.Update then
        active:Update(dt)
    end
end

-- Cambia la velocidad BASE de caminar conservando la proporcion de sprint.
-- La llama PlayerModule cuando el juego escribe Humanoid.WalkSpeed: en Roblox
-- manda el juego, no el modulo de control. Sin esto, poner WalkSpeed = 50 en un
-- script no hacia absolutamente nada (el Heartbeat lo devolvia a 16 al frame
-- siguiente) y no habia forma de cambiar la velocidad del jugador.
-- /// Sets the base walk speed, keeping the sprint ratio. The game owns WalkSpeed.
function ControlModule:SetWalkSpeed(v)
    if type(v) ~= "number" or v < 0 then return end
    self.WalkSpeed = v
    if not active then return end
    if active.SetWalkSpeed then
        active:SetWalkSpeed(v)
        return
    end
    -- Sin setter propio: se reescriben los dos y se mantiene el factor de correr
    -- que tuviera el modulo (24/16 = 1.5 de fabrica).
    local factor = 1.5
    if active.RunSpeed and active.WalkSpeed and active.WalkSpeed > 0 then
        factor = active.RunSpeed / active.WalkSpeed
    end
    active.WalkSpeed = v
    active.RunSpeed  = v * factor
end

function ControlModule:GetCurrentSpeed()
    if active and active.GetCurrentSpeed then
        return active:GetCurrentSpeed()
    end
    return self.WalkSpeed
end

function ControlModule:GetStamina()
    if active and active.GetStamina then
        return active:GetStamina()
    end
    return 100, 100
end

function ControlModule:GetMoveVector()
    if active and active.GetMoveVector then
        return active:GetMoveVector()
    end
    return 0, 0
end

return ControlModule
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/ControlModule/PCModule.luau ──
inline constexpr char LUAU_TEMPLATE_PC_MODULE_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/ControlModule/PCModule.luau";
inline constexpr char LUAU_TEMPLATE_PC_MODULE[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- PCModule.lua — ModuleScript — Modules/ControlModule/
-- Movimiento en PC (teclado + ratón). El motor lee las teclas; aquí defines las
-- velocidades y el comportamiento.
-- /// PC movement (keyboard + mouse). The engine reads the keys; here you set the
-- /// speeds and behavior.

local PCModule = {}

-- Velocidades. /// Speeds.
PCModule.WalkSpeed    = 16    -- caminar (16 = estándar) /// walk (16 = standard)
PCModule.RunSpeed     = 24    -- correr (mantener LeftShift) /// run (hold LeftShift)
PCModule.Acceleration = 8     -- suavidad: mayor = más brusco /// smoothing: higher = snappier
PCModule.JumpPower    = 50    -- fuerza de salto /// jump strength
PCModule.AutoRotate   = true  -- girar hacia el movimiento /// turn toward movement

-- Estamina (opcional): correr la gasta, caminar la recupera; a 0 no puedes correr.
-- /// Stamina (optional): running drains it, walking refills it; at 0 you can't run.
PCModule.StaminaEnabled = false
PCModule.StaminaMax     = 100
PCModule.StaminaDrain   = 20   -- por segundo corriendo /// per second running
PCModule.StaminaRegen   = 15   -- por segundo caminando /// per second walking

local current_speed = 16
local stamina       = 100

function PCModule:Initialize()
    current_speed = self.WalkSpeed
    stamina       = self.StaminaMax
    dprint("[PCModule] Modo PC activo: W/A/S/D para mover, Shift para correr")
end

-- Cada frame (lo llama ControlModule): estamina + acelerar hacia la velocidad objetivo.
-- /// Each frame (called by ControlModule): stamina + accelerate toward target speed.
function PCModule:Update(dt)
    local wants_run = UserInputService:IsKeyDown("LeftShift")

    if self.StaminaEnabled then
        if wants_run and stamina > 0 then
            stamina = math.max(0, stamina - self.StaminaDrain * dt)
        else
            stamina = math.min(self.StaminaMax, stamina + self.StaminaRegen * dt)
        end
        if stamina <= 0 then wants_run = false end
    end

    -- Acelera suave hacia la velocidad objetivo (sin saltos). /// Ease toward the target speed (no jumps).
    local target = wants_run and self.RunSpeed or self.WalkSpeed
    local t = math.min(1, self.Acceleration * dt)
    current_speed = current_speed + (target - current_speed) * t
end

-- El juego escribio Humanoid.WalkSpeed: en Roblox eso es INSTANTANEO, no una
-- rampa. Se mueve tambien current_speed para que el cambio se note en el mismo
-- frame; la aceleracion suave se queda solo para lo que es: entrar y salir del
-- sprint. /// The game set WalkSpeed: in Roblox that is instant, not a ramp.
function PCModule:SetWalkSpeed(v)
    local factor = (self.WalkSpeed > 0) and (self.RunSpeed / self.WalkSpeed) or 1.5
    self.WalkSpeed = v
    self.RunSpeed  = v * factor
    current_speed  = v
end

function PCModule:GetCurrentSpeed()
    return current_speed
end

function PCModule:GetStamina()
    return stamina, self.StaminaMax
end

function PCModule:GetMoveVector()
    -- Dirección leída del teclado (el motor ya mueve; útil para animaciones/efectos).
    -- /// Direction read from the keyboard (engine already moves; handy for animations/FX).
    local x, z = 0, 0
    if UserInputService:IsKeyDown("A") then x = x - 1 end
    if UserInputService:IsKeyDown("D") then x = x + 1 end
    if UserInputService:IsKeyDown("W") then z = z - 1 end
    if UserInputService:IsKeyDown("S") then z = z + 1 end
    return x, z
end

return PCModule
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/ControlModule/MobileModule.luau ──
inline constexpr char LUAU_TEMPLATE_MOBILE_MODULE_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/ControlModule/MobileModule.luau";
inline constexpr char LUAU_TEMPLATE_MOBILE_MODULE[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- MobileModule.lua — ModuleScript — Modules/ControlModule/
-- Movimiento en móvil (táctil). Cambia TouchMode para el tipo de control.
-- /// Mobile movement (touch). Change TouchMode for the control type.

local MobileModule = {}

-- CONFIG. TouchMode: "Joystick" (stick virtual) · "Console" (D-Pad) · "Tap" (tocar para ir).
-- /// CONFIG. TouchMode: "Joystick" (virtual stick) · "Console" (D-Pad) · "Tap" (tap to go).
MobileModule.WalkSpeed = 16
MobileModule.JumpPower = 50
MobileModule.TouchMode = "Joystick"

function MobileModule:Initialize()
    dprint("[MobileModule] Modo Movil activo: " .. self.TouchMode)
    -- Aquí puedes crear tu joystick con ScreenGui/ImageButton. /// Build your joystick here with ScreenGui/ImageButton.
end

function MobileModule:GetCurrentSpeed()
    return self.WalkSpeed
end

function MobileModule:GetMoveVector()
    return 0, 0  -- Impleméntalo con el táctil (UserInputService.TouchMoved). /// Implement with touch (UserInputService.TouchMoved).
end

return MobileModule
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/ControlModule/ConsoleModule.luau ──
inline constexpr char LUAU_TEMPLATE_CONSOLE_MODULE_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/ControlModule/ConsoleModule.luau";
inline constexpr char LUAU_TEMPLATE_CONSOLE_MODULE[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- ConsoleModule.lua — ModuleScript — Modules/ControlModule/
-- Movimiento con mando (Xbox, PlayStation, genéricos).
-- /// Gamepad movement (Xbox, PlayStation, generic pads).

local ConsoleModule = {}

-- CONFIG. Deadzone = zona muerta del stick (0..1). /// CONFIG. Deadzone = stick dead zone (0..1).
ConsoleModule.WalkSpeed = 16
ConsoleModule.JumpPower = 50
ConsoleModule.Deadzone  = 0.2  -- ignora movimientos menores /// ignore smaller movements

function ConsoleModule:Initialize()
    dprint("[ConsoleModule] Modo Consola activo — Joystick izquierdo para mover")
end

function ConsoleModule:GetCurrentSpeed()
    return self.WalkSpeed
end

function ConsoleModule:GetMoveVector()
    return 0, 0  -- Impleméntalo con el gamepad de UserInputService. /// Implement with UserInputService gamepad input.
end

return ConsoleModule
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/ChatModule.luau ──
inline constexpr char LUAU_TEMPLATE_CHAT_MODULE_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/ChatModule.luau";
inline constexpr char LUAU_TEMPLATE_CHAT_MODULE[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- ChatModule.lua — ModuleScript — Modules/
-- Lógica del chat, en Luau. Guarda los mensajes y habla con TextChatService.
-- Edítalo a tu gusto.
-- /// Chat logic, in Luau. Keeps the messages and talks to TextChatService.
-- /// Edit it however you like.

local ChatModule = {}

local TextChatService = game:GetService("TextChatService")

-- CONFIG del chat. /// Chat CONFIG.
ChatModule.MaxMessages  = 50    -- máximo de mensajes guardados /// max messages kept
ChatModule.FadeDelay    = 8.0   -- segundos hasta desvanecer /// seconds until fade out
ChatModule.FilterWords  = true  -- filtrar palabras inapropiadas /// filter bad words

local messages  = {}
local chat_open = false
local initialized = false

-- Prepara el chat y escucha los mensajes que llegan. /// Sets up the chat and listens for incoming messages.
function ChatModule:Initialize()
    if initialized then return end
    initialized = true

    if TextChatService and TextChatService.MessageReceived then
        TextChatService.MessageReceived:Connect(function(msg)
            self:_addMessage(msg)
        end)
    end

    dprint("[ChatModule] Chat listo. / o Enter para abrir.")
end

function ChatModule:_addMessage(msg)
    -- MessageReceived pasa un TextChatMessage, como Roblox. /// MessageReceived passes a TextChatMessage, like Roblox.
    local text = msg
    if type(msg) == "table" then
        local prefix = msg.PrefixText or ""
        text = (prefix ~= "" and (prefix .. ": ") or "") .. (msg.Text or "")
    end
    if type(text) ~= "string" then return end
    if #messages >= self.MaxMessages then
        table.remove(messages, 1)
    end
    table.insert(messages, { text = text, time = 0.0 })
    dprint("[Chat] " .. text)
end

function ChatModule:SendMessage(text)
    if not text or text == "" then return end
    if TextChatService then
        TextChatService:SendMessage(text)
    end
    self:_addMessage(text)
end

function ChatModule:IsOpen()
    return chat_open
end

function ChatModule:Open()
    chat_open = true
end

function ChatModule:Close()
    chat_open = false
end

function ChatModule:GetMessages()
    return messages
end

return ChatModule
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/CameraModule.luau ──
inline constexpr char LUAU_TEMPLATE_CAMERA_MODULE_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/CameraModule.luau";
inline constexpr char LUAU_TEMPLATE_CAMERA_MODULE[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- CameraModule.lua — ModuleScript — Modules/
-- Controla cómo la cámara sigue al jugador. Cambia Mode para el comportamiento.
-- /// Controls how the camera follows the player. Change Mode for the behavior.

local CameraModule = {}

-- MODO: 1 = fija (sigue al instante) · 2 = suave (sigue con retraso) ·
--       3 = combinada (retraso al moverse, centra al parar).
-- /// MODE: 1 = fixed (instant follow) · 2 = smooth (follows with lag) ·
-- ///       3 = blended (lag while moving, recenters when stopped).
CameraModule.Mode = 3   -- ← cambia aquí el modo /// change the mode here

-- Aplica el modo al entrar. /// Applies the mode on start.
function CameraModule:Apply(player)
    if player then
        player.CameraMode = self.Mode
    end
end

-- Cambia el modo en caliente. /// Changes the mode at runtime.
function CameraModule:SetMode(mode)
    self.Mode = mode
    local Players = game:GetService("Players")
    local player  = Players.LocalPlayer
    if player then
        player.CameraMode = mode
    end
end

return CameraModule
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/SettingsModule.luau ──
inline constexpr char LUAU_TEMPLATE_SETTINGS_MODULE_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/SettingsModule.luau";
inline constexpr char LUAU_TEMPLATE_SETTINGS_MODULE[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- SettingsModule.lua — ModuleScript — StarterPlayerScripts/Modules
-- Ajustes tipo Roblox, EDITABLE. Lo carga el PlayerModule con Settings.Init(player).
local Settings = {}

local Workspace = game:GetService("Workspace")
local UIS = game:GetService("UserInputService")

-- ── Colores / estilo (edítalos a gusto) ──────────────────────────────
local PANEL_BG   = Color3.fromRGB(24, 27, 34)
local ROW_BG     = Color3.fromRGB(38, 42, 52)
local ACCENT     = Color3.fromRGB(0, 162, 255)
local TEXT       = Color3.fromRGB(240, 242, 245)

local gui, panel, open = nil, nil, false
local quality = Workspace:GetGraphicsQuality()
local camLocked = false
local dynJoy = true

local function rounded(inst, r)
    local c = Instance.new("UICorner"); c.CornerRadius = UDim.new(0, r or 8); c.Parent = inst
end

-- Crea una fila "Etiqueta  [ - ] valor [ + ]"
local function stepperRow(parent, y, label, getText, onMinus, onPlus)
    local row = Instance.new("Frame")
    row.BorderSizePixel = 0
    row.Size = UDim2.new(1, -20, 0, 40)
    row.Position = UDim2.new(0, 10, 0, y)
    row.BackgroundColor3 = ROW_BG
    row.Parent = parent
    rounded(row, 8)

    local lbl = Instance.new("TextLabel")
    lbl.BorderSizePixel = 0
    lbl.Font = Enum.Font.SourceSans
    lbl.TextSize = 14
    lbl.BackgroundTransparency = 1
    lbl.Size = UDim2.new(0.5, 0, 1, 0)
    lbl.Position = UDim2.new(0, 12, 0, 0)
    lbl.Text = label
    lbl.TextColor3 = TEXT
    lbl.TextXAlignment = Enum and Enum.TextXAlignment and Enum.TextXAlignment.Left or 0
    lbl.Parent = row

    local val = Instance.new("TextLabel")
    val.BorderSizePixel = 0
    val.Font = Enum.Font.SourceSans
    val.TextSize = 14
    val.BackgroundTransparency = 1
    val.Size = UDim2.new(0, 70, 1, 0)
    val.Position = UDim2.new(1, -140, 0, 0)
    val.TextColor3 = ACCENT
    val.Text = getText()
    val.Parent = row

    local function mkbtn(text, px, cb)
        local b = Instance.new("TextButton")
        b.BorderSizePixel = 0
        b.Font = Enum.Font.SourceSans
        b.TextSize = 14
        b.Size = UDim2.new(0, 34, 0, 30)
        b.Position = UDim2.new(1, px, 0.5, -15)
        b.BackgroundColor3 = ACCENT
        b.TextColor3 = TEXT
        b.Text = text
        b.Parent = row
        rounded(b, 6)
        b.MouseButton1Click:Connect(function() cb(); val.Text = getText() end)
    end
    mkbtn("-", -70, onMinus)
    mkbtn("+", -34, onPlus)
end

local function toggleRow(parent, y, label, getOn, onToggle)
    local row = Instance.new("Frame")
    row.BorderSizePixel = 0
    row.Size = UDim2.new(1, -20, 0, 40)
    row.Position = UDim2.new(0, 10, 0, y)
    row.BackgroundColor3 = ROW_BG
    row.Parent = parent
    rounded(row, 8)
    local lbl = Instance.new("TextLabel")
    lbl.BorderSizePixel = 0
    lbl.Font = Enum.Font.SourceSans
    lbl.TextSize = 14
    lbl.BackgroundTransparency = 1
    lbl.Size = UDim2.new(0.7, 0, 1, 0)
    lbl.Position = UDim2.new(0, 12, 0, 0)
    lbl.Text = label
    lbl.TextColor3 = TEXT
    lbl.Parent = row
    local b = Instance.new("TextButton")
    b.BorderSizePixel = 0
    b.Font = Enum.Font.SourceSans
    b.TextSize = 14
    b.Size = UDim2.new(0, 60, 0, 30)
    b.Position = UDim2.new(1, -70, 0.5, -15)
    b.Text = getOn() and "ON" or "OFF"
    b.BackgroundColor3 = getOn() and ACCENT or Color3.fromRGB(70, 74, 84)
    b.TextColor3 = TEXT
    b.Parent = row
    rounded(b, 6)
    b.MouseButton1Click:Connect(function()
        onToggle()
        b.Text = getOn() and "ON" or "OFF"
        b.BackgroundColor3 = getOn() and ACCENT or Color3.fromRGB(70, 74, 84)
    end)
end

local function build(player)
    gui = Instance.new("ScreenGui")
    gui.Name = "SettingsGui"
    gui.ZIndexBehavior = Enum.ZIndexBehavior.Sibling
    gui.Parent = player:WaitForChild("PlayerGui")

    -- Botón de engranaje siempre visible (como Roblox móvil)
    local gear = Instance.new("TextButton")
    gear.BorderSizePixel = 0
    gear.Font = Enum.Font.SourceSans
    gear.TextSize = 14
    gear.Name = "SettingsButton"
    gear.Size = UDim2.new(0, 42, 0, 42)
    gear.Position = UDim2.new(1, -54, 0, 12)
    gear.AnchorPoint = Vector2.new(0, 0)
    gear.BackgroundColor3 = PANEL_BG
    gear.TextColor3 = TEXT
    gear.Text = "*"
    gear.Parent = gui
    rounded(gear, 10)

    panel = Instance.new("Frame")
    panel.BorderSizePixel = 0
    panel.Name = "Panel"
    panel.Size = UDim2.new(0, 360, 0, 300)
    panel.Position = UDim2.new(0.5, 0, 0.5, 0)
    panel.AnchorPoint = Vector2.new(0.5, 0.5)
    panel.BackgroundColor3 = PANEL_BG
    panel.Visible = false
    panel.Parent = gui
    rounded(panel, 14)

    local title = Instance.new("TextLabel")
    title.BorderSizePixel = 0
    title.Font = Enum.Font.SourceSans
    title.TextSize = 14
    title.BackgroundTransparency = 1
    title.Size = UDim2.new(1, 0, 0, 40)
    title.Text = "Settings"
    title.TextColor3 = TEXT
    title.Parent = panel

    -- Calidad gráfica 1..10
    stepperRow(panel, 50, "Graphics Quality", function() return quality .. "/10" end,
        function() quality = math.max(1, quality - 1); Workspace:SetGraphicsQuality(quality) end,
        function() quality = math.min(10, quality + 1); Workspace:SetGraphicsQuality(quality) end)

    -- Bloquear cámara (3a persona se ve como 1a)
    toggleRow(panel, 100, "Lock Camera (1st person)", function() return camLocked end,
        function()
            camLocked = not camLocked
            player.CameraMode = camLocked and 1 or 0   -- Enum.CameraMode.LockFirstPerson / Classic
        end)

    -- Móvil: joystick dinámico (aparece donde tocas) vs fijo. Solo afecta al táctil.
    toggleRow(panel, 150, "Dynamic Joystick", function() return dynJoy end,
        function()
            dynJoy = not dynJoy
            local char = player.Character
            if char then char.JoystickDynamic = dynJoy end
        end)

    local close = Instance.new("TextButton")
    close.BorderSizePixel = 0
    close.Font = Enum.Font.SourceSans
    close.TextSize = 14
    close.Size = UDim2.new(0, 120, 0, 34)
    close.Position = UDim2.new(0.5, 0, 1, -46)
    close.AnchorPoint = Vector2.new(0.5, 0)
    close.BackgroundColor3 = ACCENT
    close.TextColor3 = TEXT
    close.Text = "Close"
    close.Parent = panel
    rounded(close, 8)

    gear.MouseButton1Click:Connect(function() Settings.Toggle() end)
    close.MouseButton1Click:Connect(function() Settings.Close() end)
end

function Settings.Open()  if panel then open = true;  panel.Visible = true  end end
function Settings.Close() if panel then open = false; panel.Visible = false end end
function Settings.Toggle() if open then Settings.Close() else Settings.Open() end end

function Settings.Init(player)
    if gui then return Settings end
    build(player)
    return Settings
end

return Settings
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/Menu.luau ──
inline constexpr char LUAU_TEMPLATE_MENU_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/Menu.luau";
inline constexpr char LUAU_TEMPLATE_MENU[] = R"LUAU(
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
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/Menu/MenuUi.luau ──
inline constexpr char LUAU_TEMPLATE_MENU_UI_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/Menu/MenuUi.luau";
inline constexpr char LUAU_TEMPLATE_MENU_UI[] = R"LUAU(
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
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/Menu/Settings.luau ──
inline constexpr char LUAU_TEMPLATE_MENU_SETTINGS_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/Menu/Settings.luau";
inline constexpr char LUAU_TEMPLATE_MENU_SETTINGS[] = R"LUAU(
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
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/Menu/Players.luau ──
inline constexpr char LUAU_TEMPLATE_MENU_PLAYERS_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/Menu/Players.luau";
inline constexpr char LUAU_TEMPLATE_MENU_PLAYERS[] = R"LUAU(
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
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/Menu/Gallery.luau ──
inline constexpr char LUAU_TEMPLATE_MENU_GALLERY_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/Menu/Gallery.luau";
inline constexpr char LUAU_TEMPLATE_MENU_GALLERY[] = R"LUAU(
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
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/Menu/Help.luau ──
inline constexpr char LUAU_TEMPLATE_MENU_HELP_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/Menu/Help.luau";
inline constexpr char LUAU_TEMPLATE_MENU_HELP[] = R"LUAU(
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
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/Menu/MenuUi/Languages/English.luau ──
inline constexpr char LUAU_TEMPLATE_LANG_ENGLISH_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/Menu/MenuUi/Languages/English.luau";
inline constexpr char LUAU_TEMPLATE_LANG_ENGLISH[] = R"LUAU(
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
)LUAU";

// ── StarterPlayer/StarterPlayerScripts/Modules/Menu/MenuUi/Languages/Spanish.luau ──
inline constexpr char LUAU_TEMPLATE_LANG_SPANISH_PATH[] = "StarterPlayer/StarterPlayerScripts/Modules/Menu/MenuUi/Languages/Spanish.luau";
inline constexpr char LUAU_TEMPLATE_LANG_SPANISH[] = R"LUAU(
-- > GodotLuau — PimpoliDev
-- Spanish.lua — ModuleScript — Modules/Menu/MenuUi/Languages
-- Traducción al español. Ver English.lua para las instrucciones.

return {
	TitlePlayers = "Jugadores",
	TitleSettings = "Configuración",
	TitleGallery = "Galería",
	TitleHelp = "Ayuda",

	LeaveGame = "Salir del juego",
	Reset = "Reaparecer",
	Resume = "Continuar",

	SecLanguage = "Idioma",
	SecGraphics = "Gráficos",
	SecAudio = "Audio",
	SecControls = "Vista y controles",

	RowLanguage = "Idioma del menú",
	RowMode = "Modo",
	RowQuality = "Nivel de calidad",
	RowRenderRes = "Resolución de render",
	RowUpscaler = "Reescalador",
	RowSharpness = "Nitidez",
	RowShadows = "Sombras",
	RowViewDist = "Distancia de vista",
	RowPixelTransp = "Transparencia por píxel",
	RowOcclusion = "Occlusion culling",
	RowOccSize = "Tamaño mín. de oclusor",
	RowOccQuality = "Calidad de oclusión",
	RowVSync = "V-Sync",
	SubVSync = "Limita los FPS a la tasa del monitor (quita el tearing)",
	RowMaxFPS = "FPS máximos",
	Unlimited = "Ilimitado",
	RowVolume = "Volumen",
	RowLockCam = "Bloquear cámara (1ª persona)",
	SubLockCam = "La 3ª persona se verá como 1ª persona",
	RowDynJoy = "Joystick dinámico (móvil)",
	SubDynJoy = "Aparece donde tocas, como en Roblox",

	ShadowNames = { "Apagadas", "Muy bajas", "Bajas", "Medias", "Altas", "Ultra" },

	TipLanguage = "Cambia el idioma de este menú. Añade más copiando un module en MenuUi/Languages.",
	TipMode = "Automatic: el motor ajusta la calidad solo. Manual: eliges un nivel. Custom: cada ajuste por separado.",
	TipQuality = "Nivel gráfico general, de 1 a 10. Pulsa un cuadradito o usa - y +. Solo editable en modo Manual.",
	TipRenderRes = "Dibuja el mundo 3D a menos resolución y lo reescala. Muchos FPS a cambio de algo de nitidez.",
	TipUpscaler = "Cómo se reescala la imagen. FSR 2 conserva más nitidez.",
	TipSharpness = "Nitidez extra que aplica el reescalador.",
	TipShadows = "Calidad de las sombras. Más alto = más suaves, pero cuestan FPS.",
	TipViewDist = "Hasta dónde ves el mundo, en studs desde tu posición. Menos = más rápido.",
	TipPixelTransp = "Transparencia borrando píxeles: mucho más rápida, con un punteado fino de cerca.",
	TipOcclusion = "No dibuja lo tapado por paredes. Ayuda en mapas de muros grandes; puede costar CPU.",
	TipOccSize = "Tamaño mínimo de una pieza para tapar lo que hay detrás.",
	TipOccQuality = "Precisión del cálculo de oclusión. Más precisa = más CPU.",
	TipVSync = "Sincroniza con el monitor. Quita el tearing pero limita (y puede partir) los FPS.",
	TipMaxFPS = "Tope de fotogramas por segundo. Ilimitado deja correr el juego al máximo.",
	TipVolume = "Volumen maestro de todo el juego.",
	TipLockCam = "Fija la cámara en primera persona aunque el juego sea en tercera.",
	TipDynJoy = "En móvil, el joystick aparece donde pongas el dedo.",

	SecScreenshots = "Capturas",
	RowCapture = "Tomar una captura",
	Capture = "Capturar",
	NoShots = "Aún no hay capturas",
	Saved = "guardadas",
	TipCapture = "Cierra el menú un instante, captura el juego y la guarda aquí.",

	SecControlsHelp = "Controles",
	SecAbout = "Acerca de",
	RowEngine = "Motor",
	RowDevice = "Dispositivo",
	RowTransparency = "Transparencia del menú",
	TipTransparency = "Cuánto se transparenta el panel del menú.",
	RowPerf = "Estadísticas de rendimiento",
	TipPerf = "Muestra FPS, tiempo de frame y ping en pantalla.",
	Delete = "Borrar",
	Close = "Cerrar",
	TipEngine = "Pulsa el botón para abrir la página del proyecto GodotLuau.",

	NoRebind = "Cambiar las teclas aún no está disponible",
}
)LUAU";

// ── ServerScriptService/GameManager.luau ──
inline constexpr char LUAU_TEMPLATE_GAME_MANAGER_PATH[] = "ServerScriptService/GameManager.luau";
inline constexpr char LUAU_TEMPLATE_GAME_MANAGER[] = R"LUAU(
-- GameManager.lua — ServerScript
-- El cerebro del servidor: jugadores, eventos remotos y rondas.
-- TODO lo que pase aqui es autoridad del servidor (anti-exploit).
local Players    = game:GetService("Players")
local RunService = game:GetService("RunService")
local RS         = game:GetService("ReplicatedStorage")

dprint("[Server] GameManager iniciado!")

-- ── Jugadores: entrada y salida ───────────────────────────────────
Players.PlayerAdded:Connect(function(player)
    dprint("[Server] + " .. player.Name .. " entro a la partida")
end)

Players.PlayerRemoving:Connect(function(player)
    dprint("[Server] - " .. player.Name .. " salio de la partida")
end)

-- ── RemoteEvents compartidos en ReplicatedStorage ─────────────────
local DamageEvent = Instance.new("RemoteEvent")
DamageEvent.Name   = "DamageEvent"
DamageEvent.Parent = RS

-- REGLA DE ORO multijugador: NUNCA confiar en lo que manda el cliente.
-- Validar siempre los datos antes de aplicarlos.
local MAX_DAMAGE = 35

DamageEvent.OnServerEvent:Connect(function(player, targetName, amount)
    -- Validacion anti-exploit
    if type(targetName) ~= "string" or type(amount) ~= "number" then return end
    amount = math.clamp(amount, 0, MAX_DAMAGE)

    dprint("[Server]", player.Name, "-> daño a", targetName, "(" .. amount .. ")")
    -- Aqui aplicarias el daño al objetivo, por ejemplo:
    -- local target = workspace:FindFirstChild(targetName)
    -- local hum = target and target:FindFirstChild("Humanoid")
    -- if hum then hum:TakeDamage(amount) end
end)

-- ── Sistema de rondas (esqueleto) ─────────────────────────────────
local ROUND_TIME = 120  -- segundos por ronda

task.spawn(function()
    while true do
        dprint("[Server] Ronda nueva! (" .. ROUND_TIME .. "s)")
        task.wait(ROUND_TIME)
        dprint("[Server] Fin de la ronda.")
        task.wait(5)  -- intermedio entre rondas
    end
end)

dprint("[Server] Juego listo!")
)LUAU";

#endif // GL_LUAU_TEMPLATES_GEN_H
