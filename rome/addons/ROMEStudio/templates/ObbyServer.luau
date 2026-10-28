-- Classic Obby | Script | ServerScriptService
-- Authoritative contact damage, checkpoints and respawn. No RemoteEvents are
-- used: the server observes actual characters and static hazard Parts itself.
local RunService = game:GetService("RunService")
if not RunService:IsServer() then return end

local Players = game:GetService("Players")
local course = workspace:WaitForChild("ClassicObby")
local progress = {}
local checkpointParts = {}
local registered = {}
Players.RespawnTime = 1.5

local function getCharacter(hit)
    local node = hit
    while node and node ~= workspace do
        local humanoid = node:FindFirstChild("Humanoid")
        if humanoid then return node, humanoid end
        node = node.Parent
    end
    return nil, nil
end

local function kill(hit)
    local character, humanoid = getCharacter(hit)
    if not character or not humanoid or humanoid.Health <= 0 then return end
    -- Only player characters are part of this course; decorative objects and
    -- NPCs cannot advance a player's checkpoint or trigger an unrelated kill.
    if Players:GetPlayerFromCharacter(character) then
        humanoid.Health = 0
    end
end

for _, part in ipairs(course:GetDescendants()) do
    if part:IsA("BasePart") then
        if part:GetAttribute("ROMEHazard") then
            part.Touched:Connect(kill)
        end
        local checkpoint = part:GetAttribute("ROMECheckpoint")
        if checkpoint then
            checkpointParts[checkpoint] = part
            part.Touched:Connect(function(hit)
                local character, humanoid = getCharacter(hit)
                if not character or not humanoid or humanoid.Health <= 0 then return end
                local player = Players:GetPlayerFromCharacter(character)
                if not player then return end
                local saved = progress[player.UserId] or 0
                -- Adjacent progression prevents accidentally skipping a stage.
                if checkpoint == saved + 1 then
                    progress[player.UserId] = checkpoint
                    player:SetAttribute("ROMEObbyCheckpoint", checkpoint)
                end
            end)
        end
    end
end

local function onCharacter(player, character)
    local root = character:WaitForChild("HumanoidRootPart")
    local saved = progress[player.UserId] or 0
    local checkpoint = checkpointParts[saved]
    if checkpoint and root then
        -- Moving a character during a physics contact callback is unsafe;
        -- defer respawning until the normal task scheduler runs.
        task.defer(function()
            if character.Parent then
                character:PivotTo(checkpoint.CFrame + Vector3.new(0, 4, 0))
                root.AssemblyLinearVelocity = Vector3.new(0, 0, 0)
            end
        end)
    end
end

local function addPlayer(player)
    if registered[player.UserId] then return end
    registered[player.UserId] = true
    progress[player.UserId] = 0
    player:SetAttribute("ROMEObbyCheckpoint", 0)
    player.CharacterAdded:Connect(function(character)
        onCharacter(player, character)
    end)
    if player.Character then onCharacter(player, player.Character) end
end
Players.PlayerAdded:Connect(addPlayer)
Players.PlayerRemoving:Connect(function(player)
    registered[player.UserId] = nil
    progress[player.UserId] = nil
end)
for _, player in ipairs(Players:GetPlayers()) do addPlayer(player) end

-- Falling is checked by the server even if a fast fall misses the contact
-- sensor. This also handles falling beyond the edge of the decorative void.
local elapsed = 0
RunService.Heartbeat:Connect(function(dt)
    elapsed = elapsed + dt
    if elapsed < 0.1 then return end
    elapsed = 0
    for _, player in ipairs(Players:GetPlayers()) do
        local character = player.Character
        local root = character and character:FindFirstChild("HumanoidRootPart")
        local humanoid = character and character:FindFirstChild("Humanoid")
        if root and humanoid and root.Position.Y < -10 and humanoid.Health > 0 then
            humanoid.Health = 0
        end
    end
end)
