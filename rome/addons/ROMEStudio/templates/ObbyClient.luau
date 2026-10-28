-- Classic Obby | LocalScript | StarterPlayerScripts
-- Only the platform animation and HUD run here. Damage and checkpoints are
-- decided by ObbyServer; no client message can request a death or checkpoint.
local RunService = game:GetService("RunService")
if not RunService:IsClient() then return end

local Players = game:GetService("Players")
local player = Players.LocalPlayer
local course = workspace:WaitForChild("ClassicObby")
local movers = {}
for _, part in ipairs(course:GetDescendants()) do
    local axis = part:GetAttribute("ROMEMotionAxis")
    if axis and part:IsA("BasePart") then
        table.insert(movers, {
            part = part,
            origin = part.Position,
            axis = axis,
            period = part:GetAttribute("ROMEMotionPeriod") or 6,
            phase = part:GetAttribute("ROMEMotionPhase") or 0,
        })
    end
end

-- A shared clock keeps the route predictable when another player joins.
-- Carry only the local character that is standing on this platform. A raycast
-- excludes the character itself; jumping immediately detaches the passenger.
local groundQuery = RaycastParams.new()
groundQuery.FilterType = Enum.RaycastFilterType.Exclude
RunService.Heartbeat:Connect(function()
    local time = workspace:GetServerTimeNow()
    local character = player.Character
    local root = character and character:FindFirstChild("HumanoidRootPart")
    local humanoid = character and character:FindFirstChild("Humanoid")
    local ground = nil
    if root and humanoid and humanoid.Health > 0
        and humanoid.FloorMaterial ~= Enum.Material.Air
        and root.AssemblyLinearVelocity.Y <= 0.5 then
        groundQuery.FilterDescendantsInstances = {character}
        ground = workspace:Raycast(root.Position, Vector3.new(0, -5, 0), groundQuery)
    end
    for _, mover in ipairs(movers) do
        local offset = math.sin(time * math.pi * 2 / mover.period + mover.phase)
        local previous = mover.part.Position
        local nextPosition = mover.origin + mover.axis * offset
        mover.part.Position = nextPosition
        if ground and ground.Instance == mover.part then
            root.CFrame = root.CFrame + (nextPosition - previous)
        end
    end
end)

local gui = Instance.new("ScreenGui")
gui.Name = "ClassicObbyHUD"
gui.ResetOnSpawn = false
gui.Parent = player:WaitForChild("PlayerGui")
local panel = Instance.new("Frame")
panel.Name = "Progress"
panel.Size = UDim2.new(0, 340, 0, 78)
panel.Position = UDim2.new(0.5, -170, 0, 18)
panel.BackgroundColor3 = Color3.fromRGB(23, 32, 49)
panel.BackgroundTransparency = 0.12
panel.BorderSizePixel = 0
panel.Parent = gui
local corner = Instance.new("UICorner")
corner.CornerRadius = UDim.new(0, 12)
corner.Parent = panel
local title = Instance.new("TextLabel")
title.Name = "Stage"
title.Position = UDim2.new(0, 14, 0, 9)
title.Size = UDim2.new(1, -28, 0, 27)
title.BackgroundTransparency = 1
title.TextSize = 19
title.TextColor3 = Color3.fromRGB(242, 245, 252)
title.Parent = panel
local hint = Instance.new("TextLabel")
hint.Name = "Hint"
hint.Position = UDim2.new(0, 14, 0, 40)
hint.Size = UDim2.new(1, -28, 0, 20)
hint.BackgroundTransparency = 1
hint.TextSize = 13
hint.TextColor3 = Color3.fromRGB(181, 196, 220)
hint.Parent = panel
local names = {"Saltos", "Láseres", "Plataformas móviles", "Equilibrio", "Zigzag", "La cima"}
local function refresh()
    local progress = player:GetAttribute("ROMEObbyCheckpoint") or 0
    if progress >= 6 then
        title.Text = "¡RECORRIDO COMPLETADO!"
        hint.Text = "6 / 6  ·  Has llegado a la cima"
    else
        title.Text = string.format("%02d / 06  ·  %s", progress + 1, names[progress + 1])
        hint.Text = "Rojo = peligro  ·  Cada isla guarda tu avance"
    end
end
player:GetAttributeChangedSignal("ROMEObbyCheckpoint"):Connect(refresh)
refresh()
