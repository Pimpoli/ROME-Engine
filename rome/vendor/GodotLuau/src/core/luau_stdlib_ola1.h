// Stdlib de Luau, parte 4 de 4: la Ola 1 (datatypes, globales legacy y servicios que
// faltaban).
//
// No es un header normal: se incluye DENTRO del literal LUAU_STDLIB_CODE de
// luau_stdlib.h, y los literales de texto seguidos se juntan en uno
// solo al compilar. Va aparte porque el stdlib pasaba de 2.000 lineas.
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  GodotLuau — Ola 1: datatypes, globales legacy y servicios (Luau puro)
--  Funcional, no pulido. Añadido para cubrir "cosas que faltan".
-- ══════════════════════════════════════════════════════════════════════

-- ── RBXScriptSignal reutilizable (para respaldar servicios) ───────────
local function _mkSignal()
    local sig = { _h = {} }
    function sig:Connect(fn)
        -- _dueno: el script que conecta; el manejador corre como tarea suya y
        -- cae si muere (F8: la señal la comparten todos los del contexto).
        local c = { Connected = true, _fn = fn, _dueno = __gl_dueno and __gl_dueno() }
        function c:Disconnect()
            c.Connected = false
            for i, x in ipairs(sig._h) do if x == c then table.remove(sig._h, i) break end end
        end
        table.insert(sig._h, c)
        return c
    end
    function sig:Once(fn)
        local c
        c = sig:Connect(function(...) c:Disconnect() fn(...) end)
        return c
    end
    function sig:Wait()
        local co = coroutine.running()
        local c
        c = sig:Connect(function(...) c:Disconnect() pcall(task.spawn, co, ...) end)
        return coroutine.yield()
    end
    function sig:Fire(...)
        local snap = {}
        for _, c in ipairs(sig._h) do snap[#snap + 1] = c end
        for _, c in ipairs(snap) do
            if c.Connected then
                if c._dueno ~= nil then
                    if __gl_spawn_dueno(c._dueno, c._fn, ...) == nil then c:Disconnect() end
                else
                    task.spawn(c._fn, ...)
                end
            end
        end
    end
    return sig
end

-- ── Datatypes que faltaban ────────────────────────────────────────────
if not Ray then
    local MT = { __type = "Ray" }; MT.__index = MT
    function MT:ClosestPoint(p)
        local o, u = self.Origin, self.Direction.Unit
        return o + u * ((p - o):Dot(u))
    end
    function MT:Distance(p) return (self:ClosestPoint(p) - p).Magnitude end
    function MT:__tostring() return "Ray" end
    Ray = { new = function(origin, direction)
        return setmetatable({ Origin = origin, Direction = direction }, MT)
    end }
end

if not NumberRange then
    local MT = { __type = "NumberRange" }; MT.__index = MT
    function MT:__tostring() return tostring(self.Min) .. " " .. tostring(self.Max) end
    NumberRange = { new = function(a, b)
        if b == nil then b = a end
        return setmetatable({ Min = a, Max = b }, MT)
    end }
end

if not Vector3int16 then
    local MT = { __type = "Vector3int16" }; MT.__index = MT
    Vector3int16 = { new = function(x, y, z)
        return setmetatable({ X = math.floor(x or 0), Y = math.floor(y or 0), Z = math.floor(z or 0) }, MT)
    end }
end

if not Vector2int16 then
    local MT = { __type = "Vector2int16" }; MT.__index = MT
    Vector2int16 = { new = function(x, y)
        return setmetatable({ X = math.floor(x or 0), Y = math.floor(y or 0) }, MT)
    end }
end

if not Faces then
    local MT = { __type = "Faces" }; MT.__index = MT
    -- Enum.NormalId (numeracion de Roblox); llega como EnumItem o como numero.
    local _fnames = { [0]="Right", [1]="Top", [2]="Back", [3]="Left", [4]="Bottom", [5]="Front" }
    Faces = { new = function(...)
        local f = setmetatable({ Top=false, Bottom=false, Left=false, Right=false, Back=false, Front=false }, MT)
        for _, id in ipairs({...}) do
            local n = _fnames[type(id) == "table" and id.Value or id]
            if n then f[n] = true end
        end
        return f
    end }
end

if not Axes then
    local MT = { __type = "Axes" }; MT.__index = MT
    Axes = { new = function()
        return setmetatable({ X=false, Y=false, Z=false }, MT)
    end }
end

if not PhysicalProperties then
    local MT = { __type = "PhysicalProperties" }; MT.__index = MT
    PhysicalProperties = { new = function(density, friction, elasticity, fw, ew)
        return setmetatable({
            Density = density or 0.7, Friction = friction or 0.3, Elasticity = elasticity or 0.5,
            FrictionWeight = fw or 1, ElasticityWeight = ew or 1
        }, MT)
    end }
end

if not DateTime then
    local MT = { __type = "DateTime" }; MT.__index = MT
    function MT:ToIsoDate() return os.date("!%Y-%m-%dT%H:%M:%SZ", math.floor(self.UnixTimestamp)) end
    function MT:FormatUniversalTime(fmt) return os.date("!" .. fmt, math.floor(self.UnixTimestamp)) end
    function MT:ToUniversalTime()
        local t = os.date("!*t", math.floor(self.UnixTimestamp))
        return { Year=t.year, Month=t.month, Day=t.day, Hour=t.hour, Minute=t.min, Second=t.sec }
    end
    local function _mk(ts) return setmetatable({ UnixTimestamp = ts, UnixTimestampMillis = ts * 1000 }, MT) end
    DateTime = {
        now = function() return _mk(os.time()) end,
        fromUnixTimestamp = function(s) return _mk(s) end,
        fromUnixTimestampMillis = function(ms) return _mk(ms / 1000) end,
    }
end

-- ── Fix typeof: datatypes que no declaraban __type ───────────────────
if Region3 then Region3.__type = "Region3" end
if Rect then Rect.__type = "Rect" end
if NumberSequence then NumberSequence.__type = "NumberSequence" end
if ColorSequence then ColorSequence.__type = "ColorSequence" end
if NumberSequenceKeypoint then NumberSequenceKeypoint.__type = "NumberSequenceKeypoint" end
if ColorSequenceKeypoint then ColorSequenceKeypoint.__type = "ColorSequenceKeypoint" end
if BrickColor then BrickColor.__type = "BrickColor" end

-- ── Globales legacy ───────────────────────────────────────────────────
if not spawn then spawn = function(f, ...) return task.spawn(f, ...) end end
if not delay then delay = function(t, f) return task.delay(t, f) end end
Spawn = Spawn or spawn
Delay = Delay or delay
if not elapsedTime then elapsedTime = function() return os.clock() end end
if not settings then settings = function() return {} end end
-- UserSettings():IsUserFeatureEnabled(nombre) lo llama el Animate.lua que trae
-- CUALQUIER NPC de Roblox, dentro de un pcall. Devolvia una tabla pelada, asi
-- que el metodo no existia y el NPC se quedaba con la rama antigua del script.
if not UserSettings then
    local _us = {
        GetService = function() return {} end,
        IsUserFeatureEnabled = function(_, nombre) return false end,
    }
    UserSettings = function() return _us end
end
if task then
    task.synchronize = task.synchronize or function() end
    task.desynchronize = task.desynchronize or function() end
end

-- ── Servicios que faltaban (registrados en _G["__SVC_*"]) ─────────────
do  -- MessagingService: pub/sub ENTRE INSTANCIAS del mismo coordinador (1.14.16)
    -- Antes era pub/sub solo dentro del proceso: publicar no llegaba a los otros
    -- mundos, que es justo para lo que existe este servicio. El coordinador y sus
    -- instancias comparten user:// (ahi ya se escriben los latidos), asi que se
    -- usa un buzon por topico+emisor y cada instancia lee los de los demas.
    -- Sin coordinador (un solo servidor) sigue funcionando: es pub/sub local.
    local topics = {}      -- topic -> lista de conexiones
    local seen = {}        -- "topic|archivo" -> ultimo seq ya entregado
    local polling = false
    local S = {}
    -- Un buzon por VM: al leer se salta el propio. Desde F8 hay una VM por
    -- contexto (no por script), asi que todos los scripts del servidor
    -- comparten buzon y la entrega local va directa (_deliver).
    local _vmid = tostring((_GL_VM_ID and _GL_VM_ID()) or 0)

    local function _box(topic, who)
        return "user://msg_" .. topic .. "__" .. who .. ".json"
    end
    local function _me()
        local id = game and game.JobId or ""
        return ((id ~= "" and id) or "local") .. "_" .. _vmid
    end
    local function _deliver(topic, msg)
        local l = topics[topic]
        if not l then return end
        local snap = {}
        for _, c in ipairs(l) do snap[#snap + 1] = c end
        for _, c in ipairs(snap) do
            if c.Connected then
                -- Como tarea de quien se suscribio; si murio, la suscripcion cae.
                if c._dueno ~= nil then
                    if __gl_spawn_dueno(c._dueno, c._cb, msg) == nil then c:Disconnect() end
                else
                    task.spawn(c._cb, msg)
                end
            end
        end
    end
    -- Lee los buzones de las OTRAS instancias y entrega lo que no se haya visto.
    -- El listado de user:// es un DirAccess completo, y ese directorio crece con
    -- un ds_*.json POR CLAVE de DataStore de todas las partidas. Hacerlo 2 veces
    -- por segundo era un tiron periodico gratis: los buzones se descubren muy de
    -- vez en cuando, asi que la lista se cachea 5 s. Los mensajes se siguen
    -- leyendo del archivo ya conocido en cada sondeo.
    local _lista_cache, _lista_t = nil, -1e9
    local function _lista_buzones()
        local ahora = os.clock()
        if _lista_cache and (ahora - _lista_t) < 5.0 then return _lista_cache end
        local todo = _FileAccess.list("user://")
        local buzones = {}
        for _, fname in ipairs(todo) do
            if string.match(fname, "^msg_") then buzones[#buzones + 1] = fname end
        end
        _lista_cache, _lista_t = buzones, ahora
        return buzones
    end

    local function _poll()
        if not _FileAccess or not _FileAccess.list then return end
        local mine = _me()
        for _, fname in ipairs(_lista_buzones()) do
            local topic, who = string.match(fname, "^msg_(.+)__(.+)%.json$")
            if topic and who and who ~= mine and topics[topic] then
                local raw = _FileAccess.read("user://" .. fname)
                local ok, list = pcall(function() return _JSON and _JSON.decode(raw) end)
                if ok and type(list) == "table" then
                    local mark = topic .. "|" .. fname
                    -- Por SECUENCIA, no por indice: el buzon se recorta y los
                    -- indices se desplazarian (se perderian o repetirian mensajes).
                    local last = seen[mark]
                    if last == nil then   -- 1a vez: solo lo que llegue a partir de ahora
                        last = (list[#list] and list[#list].seq) or 0
                    end
                    for i = 1, #list do
                        local m = list[i]
                        if m and m.seq and m.seq > last then
                            _deliver(topic, { Data = m.Data, Sent = m.Sent })
                            last = m.seq
                        end
                    end
                    seen[mark] = last
                end
            end
        end
    end
    local function _ensure_polling()
        if polling then return end
        polling = true
        -- Del SISTEMA del contexto: si fuera del primer script que se
        -- suscribe, al morir ese script nadie mas recibiria mensajes.
        ;(__gl_sistema or task.spawn)(function()
            task.wait(math.random() * 0.5)
            while true do
                task.wait(0.5)
                pcall(_poll)
            end
        end)
    end

    function S:SubscribeAsync(topic, cb)
        topics[topic] = topics[topic] or {}
        local conn = { Connected = true, _cb = cb, _dueno = __gl_dueno and __gl_dueno() }
        function conn:Disconnect()
            conn.Connected = false
            local l = topics[topic]
            if l then for i, c in ipairs(l) do if c == conn then table.remove(l, i) break end end end
        end
        table.insert(topics[topic], conn)
        _ensure_polling()
        return conn
    end
    function S:PublishAsync(topic, message)
        local msg = { Data = message, Sent = os.time() }
        _deliver(topic, msg)   -- entrega local inmediata (como antes)
        -- ...y dejarlo en MI buzon para que lo lean las otras instancias.
        if not (_FileAccess and _JSON) then return end
        local path = _box(topic, _me())
        local list = {}
        local raw = _FileAccess.read(path)
        if raw and raw ~= "" then
            local ok, prev = pcall(_JSON.decode, raw)
            if ok and type(prev) == "table" then list = prev end
        end
        -- Secuencia creciente: sobrevive al recorte del buzon.
        local seq = ((list[#list] and list[#list].seq) or 0) + 1
        table.insert(list, { seq = seq, Data = message, Sent = msg.Sent })
        -- Solo se guarda una cola corta: es un buzon, no un historial.
        while #list > 64 do table.remove(list, 1) end
        pcall(function() _FileAccess.write(path, _JSON.encode(list)) end)
    end
    _G["__SVC_MessagingService"] = S
end

do  -- MemoryStoreService: mapas/colas COMPARTIDOS entre instancias (1.14.16)
    -- Antes vivia solo en memoria y, peor, GetSortedMap("x") devolvia un mapa
    -- NUEVO en cada llamada: ni siquiera era consistente dentro del mismo
    -- servidor. Ahora se respalda en user://, que el coordinador y sus mundos
    -- comparten, con expiracion real (el MemoryStore de Roblox es efimero).
    local _maps = {}   -- name -> mapa (uno por nombre, como Roblox)

    local function _path(name) return "user://mem_" .. name .. ".json" end
    local function _read(name)
        if not (_FileAccess and _JSON) then return {} end
        local raw = _FileAccess.read(_path(name))
        if not raw or raw == "" then return {} end
        local ok, d = pcall(_JSON.decode, raw)
        return (ok and type(d) == "table") and d or {}
    end
    local function _write(name, d)
        if not (_FileAccess and _JSON) then return end
        pcall(function() _FileAccess.write(_path(name), _JSON.encode(d)) end)
    end
    local function _alive(e)
        return e ~= nil and (e.exp == nil or e.exp == 0 or e.exp > os.time())
    end
    local function _newMap(name)
        local m = {}
        -- Se lee del archivo en CADA acceso: otra instancia pudo cambiarlo y una
        -- cache en memoria daria un valor viejo sin que nada avisara.
        function m:SetAsync(k, v, expiration)
            local d = _read(name)
            d[tostring(k)] = { v = v, exp = expiration and (os.time() + expiration) or 0 }
            _write(name, d)
            return true
        end
        function m:GetAsync(k)
            local e = _read(name)[tostring(k)]
            if _alive(e) then return e.v end
            return nil
        end
        function m:RemoveAsync(k)
            local d = _read(name)
            d[tostring(k)] = nil
            _write(name, d)
        end
        function m:UpdateAsync(k, cb, expiration)
            local d = _read(name)
            local sk = tostring(k)
            local e = d[sk]
            local old = _alive(e) and e.v or nil
            local nv = cb(old)
            if nv ~= nil then
                d[sk] = { v = nv, exp = expiration and (os.time() + expiration) or (e and e.exp or 0) }
                _write(name, d)
            end
            return nv
        end
        function m:GetRangeAsync()
            local out = {}
            for k, e in pairs(_read(name)) do
                if _alive(e) then out[#out + 1] = { key = k, value = e.v } end
            end
            return out
        end
        return m
    end
    local function _map(name)
        _maps[name] = _maps[name] or _newMap(name)
        return _maps[name]
    end
    local S = {}
    function S:GetSortedMap(name) return _map(name) end
    function S:GetHashMap(name) return _map(name) end
    function S:GetQueue(name)
        local q = {}
        local Q = {}
        function Q:AddAsync(v) table.insert(q, v) end
        function Q:ReadAsync(count)
            local out = {}
            for _ = 1, (count or 1) do if q[1] then table.insert(out, table.remove(q, 1)) end end
            return out
        end
        function Q:RemoveAsync() end
        return Q
    end
    _G["__SVC_MemoryStoreService"] = S
end

do  -- LogService
    local S = { MessageOut = _mkSignal() }
    local history = {}
    function S:GetLogHistory() return history end
    _G["__SVC_LogService"] = S
end

do  -- GroupService
    local S = {}
    function S:GetGroupInfoAsync(groupId)
        return { Name = "Group", Id = groupId, Owner = {}, EmblemUrl = "", Description = "" }
    end
    function S:GetGroupsAsync(userId) return {} end
    _G["__SVC_GroupService"] = S
end

do  -- PolicyService
    local S = {}
    function S:GetPolicyInfoForPlayerAsync(player)
        return {
            AllowedExternalLinkReferences = {}, ArePaidRandomItemsRestricted = false,
            IsPaidItemTradingAllowed = true, IsSubjectToChinaPolicies = false,
        }
    end
    _G["__SVC_PolicyService"] = S
end

do  -- Stats
    local S = { DataReceiveKbps = 0, DataSendKbps = 0, HeartbeatTimeMs = 0, PhysicsReceiveKbps = 0, PhysicsSendKbps = 0 }
    function S:GetTotalMemoryUsageMb() return 0 end
    _G["__SVC_Stats"] = S
end

do  -- GuiService — AMPLIA la tabla definida arriba. Asignar una tabla NUEVA
    -- aqui la pisaba entera y MenuOpened/MenuClosed/SetMenuIsOpen... morian
    -- con "attempt to index nil" (el comentario de arriba ya anunciaba que
    -- este bloque solo venia a anadir GetGuiInset).
    local S = _G["__SVC_GuiService"] or {}
    if S.MenuIsOpen == nil then S.MenuIsOpen = false end
    -- 0 y no 36: en Roblox el inset es la barra superior que DESPLAZA las UIs,
    -- pero aqui AbsolutePosition (get_global_position) y GetMouseLocation viven
    -- en el MISMO espacio de viewport sin desplazar. Devolver 36 metia 36 px de
    -- error en todo script que hace `mouse - GetGuiInset()` contra
    -- AbsolutePosition (hover que falla en una franja, punteros de tutorial
    -- desplazados). Si algun dia se dibuja una topbar real, esto y el
    -- IgnoreGuiInset de ScreenGui se cambian juntos.
    function S:GetGuiInset() return Vector2.new(0, 0), Vector2.new(0, 0) end
    function S:GetScreenResolution() return Vector2.new(1920, 1080) end
    function S:IsTenFootInterface() return false end
    -- TopbarInset: la franja de la barra de arriba que NO ocupan los botones del
    -- motor, en pixeles de pantalla. En Roblox (PC, medido el 08/10) Min = (164, 0)
    -- y Max = (ancho, 58), y los juegos ponen ahi sus botones (Build a Mountain,
    -- su menu: con la franja vacia lo dejaba bajo el boton de chat del motor, sin
    -- poder pulsarlo). La barra de GodotLuau (MenuUi) dice donde acaba con
    -- _glFijarBarra; sin barra, vacia, como en Roblox con TopbarEnabled = false.
    local barra = nil   -- { derecha = px, alto = px }
    local function insetActual()
        if not barra then return Rect.new(0, 0, 0, 0) end
        local cam = workspace.CurrentCamera
        local ancho = cam and cam.ViewportSize.X or 0
        return Rect.new(barra.derecha, 0, math.max(barra.derecha, ancho), barra.alto)
    end
    local viejo_mt = getmetatable(S)
    setmetatable(S, { __index = function(t, k)
        if k == "TopbarInset" then return insetActual() end
        if k == "ViewportDisplaySize" then
            -- En el servidor no hay pantalla (y el UIS solo existe en el cliente).
            if not __IS_CLIENT then return Enum.DisplaySize.Large end
            -- Small en un movil (pantalla tactil); en PC Roblox da Medium (08/10).
            -- Large es el de una tele: Build a Mountain ponia su menu de tele.
            local ok, tactil = pcall(function() return game:GetService("UserInputService").TouchEnabled end)
            return (ok and tactil) and Enum.DisplaySize.Small or Enum.DisplaySize.Medium
        end
        local base = viejo_mt and viejo_mt.__index
        if type(base) == "function" then return base(t, k) end
        if type(base) == "table" then return base[k] end
        return nil
    end })
    local senales = {}
    function S:GetPropertyChangedSignal(prop)
        local s = senales[prop]
        if not s then s = _mkSignal(); senales[prop] = s end
        return s
    end
    local function avisar(prop)
        local s = senales[prop]
        if s then s:Fire() end
    end
    -- La llama la barra del motor al montarse (derecha = donde acaba su ultimo
    -- boton) o al quitarse (nil). TopbarInset cambia tambien con el ancho de la
    -- ventana: los juegos escuchan su senal para recolocar.
    local vigilando = false
    function S:_glFijarBarra(derecha, alto)
        barra = derecha and { derecha = derecha, alto = alto or 58 } or nil
        if barra and not vigilando and __IS_CLIENT then
            vigilando = true
            pcall(function()
                workspace.CurrentCamera:GetPropertyChangedSignal("ViewportSize"):Connect(function()
                    avisar("TopbarInset")
                end)
            end)
        end
        avisar("TopbarInset")
    end
    _G["__SVC_GuiService"] = S
end

do  -- AnalyticsService: los eventos de analitica van a la nube de Roblox; aqui
    -- no hay a donde mandarlos, asi que se aceptan y no hacen nada (antes
    -- GetService daba nil y el modulo de analitica entero moria).
    local S = { Name = "AnalyticsService", ClassName = "AnalyticsService" }
    local function nada() end
    for _, m in ipairs({ "LogOnboardingFunnelStepEvent", "LogFunnelStepEvent", "LogEconomyEvent",
                         "LogProgressionEvent", "LogProgressionStartEvent", "LogProgressionCompleteEvent",
                         "LogProgressionFailEvent", "LogCustomEvent", "FireCustomEvent", "FireEvent",
                         "FireInGameEconomyEvent", "FireLogEvent", "FirePlayerProgressionEvent" }) do
        S[m] = nada
    end
    function S:IsA(c) return c == "AnalyticsService" or c == "Instance" end
    _G["__SVC_AnalyticsService"] = S
end

do  -- KeyframeSequenceProvider y AnimationClipProvider (F7). Registrar una
    -- KeyframeSequence da el id "rbxtemp://N" que entiende LoadAnimation. Las
    -- de un id de la nube solo existen aqui si vinieron en el .gdlu
    -- (GDLU_Animaciones, las exporta el GodotLuau Exporter desde Studio).
    local function deGdlu(id)
        local n = string.match(tostring(id), "(%d+)%D*$")
        local carpeta = n and game:FindFirstChild("GDLU_Animaciones")
        local k = carpeta and carpeta:FindFirstChild(n)
        if not k then
            error("Animation " .. tostring(id) .. " is not available offline (export it into the .gdlu with the GodotLuau Exporter)", 3)
        end
        return k:Clone()
    end
    local K = { Name = "KeyframeSequenceProvider", ClassName = "KeyframeSequenceProvider" }
    function K:RegisterKeyframeSequence(kfs) return __gl_kfs("registrar", kfs) end
    function K:RegisterActiveKeyframeSequence(kfs) return __gl_kfs("registrar", kfs) end
    function K:GetKeyframeSequenceAsync(id) return deGdlu(id) end
    function K:GetKeyframeSequence(id) return deGdlu(id) end
    function K:IsA(c) return c == "KeyframeSequenceProvider" or c == "Instance" end
    _G["__SVC_KeyframeSequenceProvider"] = K
    local A = { Name = "AnimationClipProvider", ClassName = "AnimationClipProvider" }
    function A:RegisterAnimationClip(clip) return __gl_kfs("registrar", clip) end
    function A:RegisterActiveAnimationClip(clip) return __gl_kfs("registrar", clip) end
    function A:GetAnimationClipAsync(id) return deGdlu(id) end
    function A:IsA(c) return c == "AnimationClipProvider" or c == "Instance" end
    _G["__SVC_AnimationClipProvider"] = A
end

-- utf8.graphemes (extension de Roblox a Luau): iterador de (inicio, fin) en
-- bytes de cada grafema. Un grafema es un caracter base con lo que va pegado:
-- marcas combinantes, selectores de variante, tonos de piel, lo unido por ZWJ
-- y la pareja de banderas.
if utf8 and not utf8.graphemes then
    local function pegado(c)
        return (c >= 0x0300 and c <= 0x036F) or (c >= 0x1AB0 and c <= 0x1AFF) or (c >= 0x1DC0 and c <= 0x1DFF)
            or (c >= 0x20D0 and c <= 0x20FF) or (c >= 0xFE20 and c <= 0xFE2F) or (c >= 0xFE00 and c <= 0xFE0F)
            or (c >= 0x1F3FB and c <= 0x1F3FF) or (c >= 0xE0020 and c <= 0xE007F) or c == 0x200D
    end
    local function bandera(c) return c >= 0x1F1E6 and c <= 0x1F1FF end
    function utf8.graphemes(s, i, j)
        i = i or 1
        j = j or #s
        local pos = i
        -- UTF-8 roto: un grafema por byte (utf8.codepoint fallaria a mitad).
        if not utf8.len(s, i, j) then
            return function()
                if pos > j then return nil end
                pos += 1
                return pos - 1, pos - 1
            end
        end
        return function()
            if pos > j then return nil end
            local ini = pos
            local c = utf8.codepoint(s, pos)
            local antes_zwj = false
            local banderas = bandera(c) and 1 or 0
            pos = utf8.offset(s, 2, pos) or (j + 1)
            while pos <= j do
                local d = utf8.codepoint(s, pos)
                local une = pegado(d) or antes_zwj or (banderas == 1 and bandera(d))
                if not une then break end
                if bandera(d) then banderas += 1 end
                antes_zwj = d == 0x200D
                pos = utf8.offset(s, 2, pos) or (j + 1)
            end
            return ini, pos - 1
        end
    end
end

do  -- ProximityPromptService
    local S = {
        PromptTriggered = _mkSignal(), PromptTriggerEnded = _mkSignal(),
        PromptShown = _mkSignal(), PromptHidden = _mkSignal(),
        PromptButtonHoldBegan = _mkSignal(), PromptButtonHoldEnded = _mkSignal(),
    }
    -- Los eventos (aparecer / ocultarse / activarse un cartel) los genera C++ y
    -- se recogen aqui. Hace falta ir a por ellos porque el servicio es una tabla
    -- de Luau y hay una VM POR SCRIPT: desde C++ no hay a quien llamar.
    --
    -- El sondeo arranca con la PRIMERA conexion, no antes. Es del SISTEMA del
    -- contexto (F8): si fuera del script que conecto primero, al morir ese
    -- script los carteles dejarian de avisar a todos.
    local desde, corriendo = 0, false
    local function arrancar()
        if corriendo or not __GL_PROMPT_EVENTS then return end
        corriendo = true
        ;(__gl_sistema or task.spawn)(function()
            while true do
                task.wait(0.05)
                local ev, ultimo = __GL_PROMPT_EVENTS(desde)
                desde = ultimo or desde
                for _, e in ipairs(ev or {}) do
                    if e.tipo == 0 then
                        S.PromptShown:Fire(e.prompt, Enum.ProximityPromptInputType.Keyboard)
                    elseif e.tipo == 1 then
                        S.PromptHidden:Fire(e.prompt)
                    else
                        S.PromptTriggered:Fire(e.prompt, e.jugador)
                    end
                end
            end
        end)
    end
    for _, nombre in ipairs({"PromptShown", "PromptHidden", "PromptTriggered"}) do
        local sig = S[nombre]
        local connectar = sig.Connect
        function sig:Connect(fn) arrancar() return connectar(self, fn) end
        local unaVez = sig.Once
        function sig:Once(fn) arrancar() return unaVez(self, fn) end
        local esperar = sig.Wait
        function sig:Wait() arrancar() return esperar(self) end
    end
    _G["__SVC_ProximityPromptService"] = S
end

do  -- MarketplaceService: compras de PRUEBA con el estado en C++ (gl_tienda.h)
    -- Cada script tiene su VM y antes cada uno tenia su tienda: el
    -- ProcessReceipt del servidor no veia las compras de un LocalScript y un
    -- pase comprado solo existia para el script que lo compro. Ahora ProcessReceipt
    -- (uno, el del servidor), la propiedad (en disco) y las señales *Finished
    -- (en cada VM que escucha) viven en __gl_mps.
    local MPS = { Name = "MarketplaceService", ClassName = "MarketplaceService" }
    local api = __gl_mps
    local receiptFn = nil
    local escuchando = false
    local senales = {}
    local function senal(tipo)
        local s = _mkSignal()
        local conectar = s.Connect
        -- La VM se apunta a los avisos al conectar la primera vez (Once y Wait
        -- pasan por Connect). La escucha es del SISTEMA del contexto, no del
        -- script que conecto primero (F8).
        function s:Connect(fn)
            if not escuchando and api then
                escuchando = true
                ;(__gl_sistema or task.spawn)(function()
                    api.escuchar(function(t, ...)
                        local x = senales[t]
                        if x then x:Fire(...) end
                    end)
                end)
            end
            return conectar(self, fn)
        end
        senales[tipo] = s
        return s
    end
    MPS.PromptProductPurchaseFinished      = senal("product")
    MPS.PromptGamePassPurchaseFinished     = senal("gamepass")
    MPS.PromptPurchaseFinished             = senal("asset")
    MPS.PromptBundlePurchaseFinished       = senal("bundle")
    MPS.PromptPremiumPurchaseFinished      = _mkSignal()
    MPS.PromptSubscriptionPurchaseFinished = _mkSignal()
    MPS.PromptRobloxSubscriptionPurchaseFinished = _mkSignal()

    local function jugador(p)
        if p ~= nil then return p end
        local ok, lp = pcall(function() return game:GetService("Players").LocalPlayer end)
        return ok and lp or nil
    end
    local function uidDe(p)
        if type(p) == "number" then return p end
        local ok, id = pcall(function() return p.UserId end)
        return (ok and tonumber(id)) or 0
    end
    local function pedir(tipo, p, id)
        if api then api.pedir(tipo, jugador(p), tonumber(id) or 0) end
    end

    function MPS:PromptProductPurchase(player, productId) pedir("product", player, productId) end
    function MPS:PromptGamePassPurchase(player, gamePassId) pedir("gamepass", player, gamePassId) end
    function MPS:PromptPurchase(player, assetId) pedir("asset", player, assetId) end
    function MPS:PromptBundlePurchase(player, bundleId) pedir("bundle", player, bundleId) end
    -- Premium y suscripciones no se pueden comprar sin Roblox: se cierran solas.
    function MPS:PromptPremiumPurchase(player)
        task.defer(function() MPS.PromptPremiumPurchaseFinished:Fire() end)
    end
    function MPS:PromptSubscriptionPurchase(player, subscriptionId)
        task.defer(function()
            MPS.PromptSubscriptionPurchaseFinished:Fire(jugador(player), subscriptionId, false)
        end)
    end

    function MPS:UserOwnsGamePassAsync(userId, gamePassId)
        return api ~= nil and api.tiene("gamepass", tonumber(userId) or 0, tonumber(gamePassId) or 0)
    end
    function MPS:PlayerOwnsAsset(player, assetId)
        return api ~= nil and api.tiene("asset", uidDe(player), tonumber(assetId) or 0)
    end
    MPS.PlayerOwnsAssetAsync = MPS.PlayerOwnsAsset
    function MPS:PlayerOwnsBundle(player, bundleId)
        return api ~= nil and api.tiene("bundle", uidDe(player), tonumber(bundleId) or 0)
    end
    function MPS:IsPlayerSubscribed(player, subscriptionId) return false end
    function MPS:GetUserSubscriptionStatusAsync(user, subscriptionId)
        return { IsSubscribed = false, IsRenewing = false }
    end

    -- Sin conexion con Roblox no hay nombres ni precios reales.
    local nombres = { [0] = "Item", [1] = "Developer Product", [2] = "Game Pass", [3] = "Subscription", [4] = "Bundle" }
    function MPS:GetProductInfo(assetId, infoType)
        local t = tonumber(infoType) or 0
        local id = tonumber(assetId) or 0
        return {
            Name = (nombres[t] or "Item") .. " " .. tostring(assetId),
            Description = "",
            PriceInRobux = 0,
            IconImageAssetId = 0,
            IsForSale = true,
            IsPublicDomain = false,
            IsLimited = false,
            IsLimitedUnique = false,
            IsNew = false,
            Remaining = 0,
            Sales = 0,
            MinimumMembershipLevel = 0,
            Creator = { Id = 0, Name = "", CreatorType = "User", CreatorTargetId = 0, HasVerifiedBadge = false },
            AssetId = (t == 1) and 0 or id,
            ProductId = id,
            TargetId = id,
        }
    end
    function MPS:GetProductInfoAsync(assetId, infoType) return self:GetProductInfo(assetId, infoType) end
    function MPS:GetDeveloperProductsAsync() return _ds_paginas({}, 50) end
    function MPS:IsA(c) return c == "MarketplaceService" or c == "Instance" end
    function MPS:GetFullName() return "MarketplaceService" end

    -- ProcessReceipt: el que asigna el servidor se apunta en C++. El envoltorio
    -- dice si el recibo quedo concedido; si no, se reintenta luego.
    setmetatable(MPS, {
        __index = function(_, k)
            if k == "ProcessReceipt" then return receiptFn end
            return nil
        end,
        __newindex = function(t, k, v)
            if k ~= "ProcessReceipt" then rawset(t, k, v) return end
            receiptFn = v
            if not api then return end
            if type(v) ~= "function" then api.recibos(nil) return end
            api.recibos(function(info)
                pcall(function() info.PlaceIdWherePurchased = game.PlaceId end)
                local ok, r = pcall(v, info)
                -- El EnumItem, o su numero (1) si el juego lo devuelve a pelo.
                local dado = ok and (r == Enum.ProductPurchaseDecision.PurchaseGranted or r == 1)
                api.recibo_hecho(info.PurchaseId, dado)
                if not ok then error(r, 0) end
            end)
        end,
    })
    _G["__SVC_MarketplaceService"] = MPS
end

do  -- BadgeService: persistir medallas otorgadas (antes AwardBadge solo print)
    local BS = _G["__SVC_BadgeService"]
    if BS then
        BS._awarded = BS._awarded or {}
        function BS:AwardBadge(userId, badgeId)
            BS._awarded[tostring(userId) .. ":" .. tostring(badgeId)] = true
            return true
        end
        function BS:UserHasBadgeAsync(userId, badgeId)
            return BS._awarded[tostring(userId) .. ":" .. tostring(badgeId)] == true
        end
        -- Los nombres modernos (los usa el codigo de hoy de Build A Mountain).
        function BS:AwardBadgeAsync(userId, badgeId)
            return BS:AwardBadge(userId, badgeId)
        end
        function BS:CheckUserBadgesAsync(userId, badgeIds)
            local tiene = {}
            for _, id in ipairs(badgeIds or {}) do
                if BS:UserHasBadgeAsync(userId, id) then table.insert(tiene, id) end
            end
            return tiene
        end
    end
end

-- ── Instance.new: clases nuevas en Luau puro (BindableFunction, etc.) ──
do
    local _prev_new = Instance.new
    Instance.new = function(className, parent)
        -- BindableFunction YA ES UNA INSTANCE DE VERDAD (se quito el atajo de
        -- tabla que habia aqui). La tabla existia porque la escritura de
        -- OnInvoke no se guardaba; la causa resulto ser que se anotaba la
        -- CORRUTINA del script en vez de su estado principal, y gl_state_alive
        -- solo conoce los principales. Arreglado eso, la Instance real trae
        -- ademas Parent, FindFirstChild y Destroy, que la tabla no tenia.
        if className == "UnreliableRemoteEvent" then
            local ev = _prev_new("UnreliableRemoteEvent", parent)
            if ev then pcall(function() ev.Name = "UnreliableRemoteEvent" end) end
            return ev
        end
        -- Los parametros de TextService:GetTextBoundsAsync. No existia y el
        -- ProximityPromptScript OFICIAL de Roblox (el de cualquier place con
        -- prompts propios) moria al montar cada cartel. Width 0 = sin ajuste.
        if className == "GetTextBoundsParams" then
            local p = { Name = "GetTextBoundsParams", ClassName = "GetTextBoundsParams",
                        Text = "", Size = 14, Width = 0, RichText = false, Font = Font.new() }
            function p:IsA(c) return c == "GetTextBoundsParams" or c == "Instance" end
            return p
        end
        return _prev_new(className, parent)
    end
end

)LUAU"
