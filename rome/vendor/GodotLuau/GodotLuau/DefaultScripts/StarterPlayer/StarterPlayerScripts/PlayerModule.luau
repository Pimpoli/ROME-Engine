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
