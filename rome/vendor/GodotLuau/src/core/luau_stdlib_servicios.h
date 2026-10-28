// Stdlib de Luau, parte 2 de 4: los servicios en Luau: Debris, los que no tienen backend,
// CollectionService, DataStoreService, HttpService, ContextActionService,
// PathfindingService, PhysicsService, TeleportService, BadgeService,
// SocialService, GuiService, InsertService y ScriptContext.
//
// No es un header normal: se incluye DENTRO del literal LUAU_STDLIB_CODE de
// luau_stdlib.h, y los literales de texto seguidos se juntan en uno
// solo al compilar. Va aparte porque el stdlib pasaba de 2.000 lineas.
R"LUAU(

-- ══════════════════════════════════════════════════════════════════════
--  Debris — automatically removes instances after N seconds
-- ////
--  Debris — elimina instancias automáticamente tras N segundos
-- ══════════════════════════════════════════════════════════════════════
local Debris = {}
Debris.__index = Debris

function Debris:AddItem(instance, lifetime)
    -- Del SISTEMA del contexto: el objeto se borra aunque el script que lo
    -- apunto ya no exista (con una VM por script se quedaba para siempre).
    (__gl_sistema or task.spawn)(function()
        task.wait(lifetime or 10)   -- 10 s por defecto, como Roblox
        if instance ~= nil then
            pcall(function() instance:Destroy() end)
        end
    end)
end

_G["__SVC_Debris"] = Debris

-- ══════════════════════════════════════════════════════════════════════
--  Servicios sin backend: ContentProvider, VRService, HapticService,
--  LocalizationService y Chat (el legado). GetService devolvia nil y el
--  script moria en la primera linea que los usaba: la pantalla de carga de
--  casi cualquier place empieza con ContentProvider:PreloadAsync.
-- ══════════════════════════════════════════════════════════════════════
-- Lo minimo de Instance para un servicio hecho en Lua (el PlayerModule hace
-- VRService:GetPropertyChangedSignal("VREnabled")).
local function _gl_servicio_lua(nombre, t)
    t.Name = nombre
    t.ClassName = nombre
    function t:IsA(c) return c == nombre or c == "Instance" end
    function t:GetPropertyChangedSignal() return _gl_signal() end
    t.Changed = _gl_signal()
    _G["__SVC_" .. nombre] = t
    return t
end

-- Las imagenes se cargan en segundo plano (gl_streaming_texturas.h).
-- PreloadAsync las pide YA y espera a que lleguen, como en Roblox, avisando de
-- cada asset al terminar; asi una pantalla de carga sale entera y no a trozos.
-- Lo que no es imagen (sonidos, animaciones, mallas) o no esta cuenta como
-- cargado al momento. Una Instance trae tambien a sus descendientes.
local ContentProvider = _gl_servicio_lua("ContentProvider", {
    BaseUrl = "https://www.roblox.com/", RequestQueueSize = 0,
})
local _cp_props = {
    ImageLabel = { "Image" }, ImageButton = { "Image" }, Decal = { "Texture" }, Texture = { "Texture" },
    MeshPart = { "TextureID", "MeshId" }, SpecialMesh = { "TextureId", "MeshId" }, Sound = { "SoundId" },
    Animation = { "AnimationId" }, ParticleEmitter = { "Texture" }, Beam = { "Texture" }, Trail = { "Texture" },
    Sky = { "SkyboxBk", "SkyboxDn", "SkyboxFt", "SkyboxLf", "SkyboxRt", "SkyboxUp", "SunTextureId", "MoonTextureId" },
}
local function _cp_anota(inst, ids, vistos)
    local props = _cp_props[inst.ClassName]
    if not props then return end
    for _, prop in ipairs(props) do
        local ok, v = pcall(function() return inst[prop] end)
        if ok and type(v) == "string" and v ~= "" and not vistos[v] then
            vistos[v] = true
            table.insert(ids, v)
        end
    end
end
function ContentProvider:PreloadAsync(contentIds, callback)
    if type(contentIds) ~= "table" then return end
    local ids, vistos = {}, {}
    for _, item in ipairs(contentIds) do
        if typeof(item) == "Instance" then
            _cp_anota(item, ids, vistos)
            for _, d in ipairs(item:GetDescendants()) do _cp_anota(d, ids, vistos) end
        else
            local id = tostring(item)
            if not vistos[id] then vistos[id] = true; table.insert(ids, id) end
        end
    end
    local pendientes = {}
    for _, id in ipairs(ids) do
        if __gl_tex_precargar(id) == 1 then
            table.insert(pendientes, id)
        elseif callback then
            callback(id, Enum.AssetFetchStatus.Success)
        end
    end
    local t0 = os.clock()
    while #pendientes > 0 and os.clock() - t0 < 10 do
        task.wait()
        for i = #pendientes, 1, -1 do
            local e = __gl_tex_estado(pendientes[i])
            if e ~= 1 then
                local id = table.remove(pendientes, i)
                if callback then
                    callback(id, if e == 2 then Enum.AssetFetchStatus.Success else Enum.AssetFetchStatus.Failure)
                end
            end
        end
    end
    for _, id in ipairs(pendientes) do
        if callback then callback(id, Enum.AssetFetchStatus.TimedOut) end
    end
end
function ContentProvider:GetAssetFetchStatus() return Enum.AssetFetchStatus.Success end
function ContentProvider:GetAssetFetchStatusChangedSignal() return _gl_signal() end
function ContentProvider:ListEncryptedAssets() return {} end
function ContentProvider:RegisterEncryptedAsset() end
function ContentProvider:UnregisterEncryptedAsset() end
function ContentProvider:SetBaseUrl(url) self.BaseUrl = url end
ContentProvider.AssetFetchFailed = _gl_signal()

local VRService = _gl_servicio_lua("VRService", {
    VREnabled = false, FadeOutViewOnCollision = true, ThirdPersonFollowCamEnabled = false,
})
function VRService:GetUserCFrame() return CFrame.new() end
function VRService:GetUserCFrameEnabled() return false end
function VRService:RecenterUserHeadCFrame() end
function VRService:RequestNavigation() end
function VRService:SetTouchpadMode() end
function VRService:GetTouchpadMode() return nil end
VRService.UserCFrameChanged   = _gl_signal()
VRService.UserCFrameEnabled   = _gl_signal()
VRService.NavigationRequested = _gl_signal()
VRService.TouchpadModeChanged = _gl_signal()

local HapticService = _gl_servicio_lua("HapticService", {})
function HapticService:IsVibrationSupported() return false end
function HapticService:IsMotorSupported() return false end
function HapticService:SetMotor() end
function HapticService:GetMotor() return 0 end

-- Sin tablas de traduccion: el texto sale tal cual.
local LocalizationService = _gl_servicio_lua("LocalizationService", {
    RobloxLocaleId = "en-us", SystemLocaleId = "en-us",
})
local function _gl_traductor(locale)
    local t = { LocaleId = locale or "en-us" }
    function t:Translate(_, text) return text end
    function t:FormatByKey(key) return key end
    function t:RobloxOnlyTranslate(_, text) return text end
    return t
end
function LocalizationService:GetTranslatorForPlayerAsync() return _gl_traductor(self.RobloxLocaleId) end
function LocalizationService:GetTranslatorForPlayer() return _gl_traductor(self.RobloxLocaleId) end
function LocalizationService:GetTranslatorForLocaleAsync(locale) return _gl_traductor(locale) end
function LocalizationService:GetCountryRegionForPlayerAsync() return "US" end
function LocalizationService:GetCorescriptLocalizations() return {} end
function LocalizationService:GetTableEntries() return {} end

-- El Chat legado (el moderno es TextChatService). Sin moderacion: el texto se
-- devuelve tal cual.
local ChatLegado = _gl_servicio_lua("Chat", { BubbleChatEnabled = false, LoadDefaultChat = true })
function ChatLegado:Chat() end
function ChatLegado:FilterStringAsync(s) return s end
function ChatLegado:FilterStringForBroadcast(s) return s end
function ChatLegado:CanUserChatAsync() return true end
function ChatLegado:CanUsersChatAsync() return true end
function ChatLegado:RegisterChatCallback() end
function ChatLegado:InvokeChatCallback() return nil end
function ChatLegado:SetBubbleChatSettings() end
ChatLegado.Chatted = _gl_signal()

-- ══════════════════════════════════════════════════════════════════════
--  CollectionService — sistema de tags en instancias (reescrito en 1.17)
--  https://create.roblox.com/docs/reference/engine/classes/CollectionService
-- ══════════════════════════════════════════════════════════════════════
-- El registro de VERDAD vive en el nodo C++ CollectionService (roblox_input.h):
-- es el unico sitio comun a TODAS las VMs (cada script tiene la suya) y el que
-- conoce las etiquetas del place importado (metadato __rbx_tags). La version
-- anterior era una tabla local por VM: un AddTag de un script era invisible
-- para los demas, y los tags importados no aparecian NUNCA.
--
-- Las señales son por VM (los callbacks son funciones de Lua): los cambios
-- hechos en esta misma VM disparan al momento; los del resto (otras VMs,
-- clones que entran al arbol al respawnear) los detecta un sondeo que compara
-- GetTagged por tag vigilado. Connect devuelve una conexion de verdad
-- (_gl_signal): antes devolvia nil y el primer :Disconnect() mataba el script.
local CollectionService = {}
CollectionService.__index = CollectionService

-- Respaldo Lua puro por si no hay backend C++ (proyecto sin importar y sin la
-- clase registrada): mismo comportamiento que antes, pero con señales reales.
local _ccs_tags      = {}   -- {[tag] = {[instance] = true}}
local _ccs_inst_tags = {}   -- {[instance] = {[tag] = true}}

local _ccs_node = nil
local function _ccs_backend()
    if _ccs_node ~= nil then
        local ok, alive = pcall(function() return _ccs_node.Name ~= nil end)
        if ok and alive then return _ccs_node end
        _ccs_node = nil   -- el nodo murio (cambio de escena): re-resolver
    end
    if not game then return nil end
    local ok, found = pcall(function() return game:FindFirstChild("CollectionService") end)
    if ok and found then
        _ccs_node = found
        return _ccs_node
    end
    -- No existe (place sin el servicio serializado): se crea UNA vez y se
    -- cuelga de game, para que todas las VMs encuentren el mismo registro.
    if _InstanceNew then
        local ok2, created = pcall(function()
            local n = _InstanceNew("CollectionService")
            if not n then return nil end
            n.Name = "CollectionService"
            n.Parent = game
            return n
        end)
        if ok2 and created then
            _ccs_node = created
            return _ccs_node
        end
    end
    return nil
end

local _ccs_added_sigs   = {}   -- tag → señal (_gl_signal)
local _ccs_removed_sigs = {}
local _ccs_seen         = {}   -- tag vigilado → { [GetDebugId] = instancia }
local _ccs_polling      = false

local function _ccs_id(inst)
    local ok, id = pcall(function() return inst:GetDebugId() end)
    if ok and id then return id end
    return nil
end

-- Foto actual del tag por id de instancia. Clave por GetDebugId y no por el
-- userdata: cada GetTagged crea envoltorios NUEVOS del mismo nodo, asi que el
-- userdata no sirve como identidad entre dos sondeos.
local function _ccs_snapshot(tag)
    local map = {}
    for _, inst in ipairs(CollectionService:GetTagged(tag)) do
        local id = _ccs_id(inst)
        if id then map[id] = inst end
    end
    return map
end

local function _ccs_watch(tag)
    if _ccs_seen[tag] then return end
    -- La foto inicial se toma al crear la señal: como en Roblox, solo se
    -- disparan las incorporaciones FUTURAS, no lo que ya estaba etiquetado.
    _ccs_seen[tag] = _ccs_snapshot(tag)
    if _ccs_polling then return end
    _ccs_polling = true
    -- Del SISTEMA del contexto (F8), no del primer script que lo pidio.
    ;(__gl_sistema or task.spawn)(function()
        while true do
            task.wait(0.25)
            for tg, seen in pairs(_ccs_seen) do
                local ok, now = pcall(_ccs_snapshot, tg)
                if ok then
                    for id, inst in pairs(now) do
                        if seen[id] == nil then
                            seen[id] = inst
                            local sig = _ccs_added_sigs[tg]
                            if sig then sig:Fire(inst) end
                        end
                    end
                    for id, inst in pairs(seen) do
                        if now[id] == nil then
                            seen[id] = nil
                            local sig = _ccs_removed_sigs[tg]
                            if sig then sig:Fire(inst) end
                        end
                    end
                end
            end
        end
    end)
end

-- Disparo inmediato en ESTA VM tras un Add/RemoveTag local (el sondeo se
-- entera igual, pero 0,25 s tarde; y Roblox dispara sincrono).
local function _ccs_fire_added(tag, instance)
    local id = _ccs_id(instance)
    if id and _ccs_seen[tag] then _ccs_seen[tag][id] = instance end
    local sig = _ccs_added_sigs[tag]
    if sig then sig:Fire(instance) end
end

local function _ccs_fire_removed(tag, instance)
    local id = _ccs_id(instance)
    if id and _ccs_seen[tag] then _ccs_seen[tag][id] = nil end
    local sig = _ccs_removed_sigs[tag]
    if sig then sig:Fire(instance) end
end

-- ¿Esta la instancia dentro del DataModel con este tag? Las señales solo se
-- disparan en ese caso ("The tag is assigned to an instance within the
-- DataModel", doc de GetInstanceAddedSignal).
local function _ccs_in_datamodel(instance)
    local ok, r = pcall(function() return instance:IsDescendantOf(game) end)
    return ok and r == true
end

function CollectionService:AddTag(instance, tag)
    if instance == nil or tag == nil then return end
    local cpp = _ccs_backend()
    local had
    if cpp then
        had = cpp:HasTag(instance, tag)
        if not had then cpp:AddTag(instance, tag) end
    else
        had = _ccs_inst_tags[instance] ~= nil and _ccs_inst_tags[instance][tag] == true
        if not had then
            if not _ccs_tags[tag] then _ccs_tags[tag] = {} end
            _ccs_tags[tag][instance] = true
            if not _ccs_inst_tags[instance] then _ccs_inst_tags[instance] = {} end
            _ccs_inst_tags[instance][tag] = true
        end
    end
    -- "doing nothing if the tag is already applied to that instance" (doc de AddTag)
    if not had and _ccs_in_datamodel(instance) then
        _ccs_fire_added(tag, instance)
    end
end

function CollectionService:RemoveTag(instance, tag)
    if instance == nil or tag == nil then return end
    local cpp = _ccs_backend()
    local had
    if cpp then
        had = cpp:HasTag(instance, tag)
        if had then cpp:RemoveTag(instance, tag) end
    else
        had = _ccs_inst_tags[instance] ~= nil and _ccs_inst_tags[instance][tag] == true
        if _ccs_tags[tag] then _ccs_tags[tag][instance] = nil end
        if _ccs_inst_tags[instance] then _ccs_inst_tags[instance][tag] = nil end
    end
    if had and _ccs_in_datamodel(instance) then
        _ccs_fire_removed(tag, instance)
    end
end

function CollectionService:HasTag(instance, tag)
    local cpp = _ccs_backend()
    if cpp then return cpp:HasTag(instance, tag) end
    return _ccs_inst_tags[instance] ~= nil and _ccs_inst_tags[instance][tag] == true
end

function CollectionService:GetTagged(tag)
    local cpp = _ccs_backend()
    if cpp then return cpp:GetTagged(tag) end
    local result = {}
    if _ccs_tags[tag] then
        for inst in pairs(_ccs_tags[tag]) do table.insert(result, inst) end
    end
    return result
end

function CollectionService:GetTags(instance)
    local cpp = _ccs_backend()
    if cpp then return cpp:GetTags(instance) end
    local result = {}
    if _ccs_inst_tags[instance] then
        for tag in pairs(_ccs_inst_tags[instance]) do table.insert(result, tag) end
    end
    return result
end

-- Roblox: "fires under two conditions: The tag is assigned to an instance
-- within the DataModel using AddTag() or an instance with the given tag is
-- added as a descendant of the DataModel".
-- https://create.roblox.com/docs/reference/engine/classes/CollectionService#GetInstanceAddedSignal
function CollectionService:GetInstanceAddedSignal(tag)
    local sig = _ccs_added_sigs[tag]
    if not sig then
        sig = _gl_signal()
        _ccs_added_sigs[tag] = sig
        _ccs_watch(tag)
    end
    return sig
end

function CollectionService:GetInstanceRemovedSignal(tag)
    local sig = _ccs_removed_sigs[tag]
    if not sig then
        sig = _gl_signal()
        _ccs_removed_sigs[tag] = sig
        _ccs_watch(tag)
    end
    return sig
end

_G["__SVC_CollectionService"] = CollectionService

-- ══════════════════════════════════════════════════════════════════════
--  DataStoreService — persistencia de datos (local + HTTP opcional)
-- ══════════════════════════════════════════════════════════════════════
local DataStoreService = {}
DataStoreService.__index = DataStoreService
local _ds_stores  = {}
local _ds_ordered = {}

-- OJO: si el encode falla NO se devuelve "{}". Antes si, y eso escribia un
-- perfil VACIO encima del bueno: al recargar, el juego lo veia invalido y
-- reiniciaba los datos del jugador — perdida total y sin un solo error. Roblox
-- lanza en ese caso ("Cannot store ... in DataStore"), asi que aqui igual: el
-- pcall del juego lo recoge, reintenta y avisa.
local function _ds_safe_encode(v)
    if not _JSON then return tostring(v) end
    local ok, r = pcall(_JSON.encode, v)
    if not ok or type(r) ~= "string" then
        error("Cannot store value in DataStore (failed to serialize)", 2)
    end
    return r
end

local function _ds_safe_decode(s)
    if not s or s == "" then return nil end
    if not _JSON then return nil end
    local ok, r = pcall(_JSON.decode, s)
    return ok and r or nil
end

-- Los nombres de store y las claves de Roblox admiten caracteres que NO valen
-- en una ruta de archivo (/, \, :, ?, *...). Sin sanear, esas claves generaban
-- una ruta invalida y el guardado se perdia sin avisar. Ademas se separan con
-- un caracter que no puede aparecer en el texto saneado, para que
-- GetDataStore("A"):Get("B_C") y GetDataStore("A_B"):Get("C") no compartan
-- archivo.
local function _ds_sane(t)
    return (tostring(t):gsub("[^%w%-]", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

local function _ds_path(storeName, key)
    return "user://ds_" .. _ds_sane(storeName) .. "__" .. _ds_sane(key) .. ".json"
end

-- La ruta de ANTES de sanear. Se sigue leyendo si la nueva todavia no existe:
-- cambiar el nombre del archivo sin esto le borraria la partida a todo el que
-- actualice GodotLuau. La primera escritura ya deja el archivo en la ruta nueva.
local function _ds_path_legacy(storeName, key)
    return "user://ds_" .. tostring(storeName) .. "_" .. tostring(key) .. ".json"
end

local function _ds_leer(storeName, key)
    if not _FileAccess then return nil end
    local ruta = _ds_path(storeName, key)
    local c = _FileAccess.read(ruta)
    if c and c ~= "" then return c end
    local vieja = _ds_path_legacy(storeName, key)
    if vieja ~= ruta then
        c = _FileAccess.read(vieja)
        if c and c ~= "" then return c end
    end
    return nil
end

-- En Roblox tocar un DataStore desde el cliente es error. Aquí ademas TODAS las
-- ventanas del proyecto comparten user://, asi que un cliente escribiendo
-- pisaria los archivos del servidor. Rol 2 = cliente puro.
local function _ds_assert_server(what)
    if _GL_NET_ROLE and _GL_NET_ROLE() == 2 then
        error(what .. " can only be called from the server (DataStore is not accessible to clients)", 3)
    end
end

-- Paginas como DataStorePages: IsFinished cambia al avanzar. Antes era un campo
-- fijo, y el bucle tipico (leer pagina, si no IsFinished avanzar) no acababa
-- nunca con dos paginas o mas.
local function _ds_paginas(entries, pageSize)
    local ps = math.max(1, math.floor(tonumber(pageSize) or 50))
    local pages = {}
    for i = 1, #entries, ps do
        pages[#pages + 1] = table.move(entries, i, math.min(i + ps - 1, #entries), 1, {})
    end
    if #pages == 0 then pages[1] = {} end
    local idx = 1
    local p = {}
    function p:GetCurrentPage() return pages[idx] end
    function p:AdvanceToNextPageAsync() if idx < #pages then idx += 1 end end
    return setmetatable(p, { __index = function(_, k)
        if k == "IsFinished" then return idx >= #pages end
        return nil
    end })
end

local function _make_datastore(name)
    local store = { _name = name, _cache = {} }

    function store:GetAsync(key)
        _ds_assert_server("GetAsync")
        local sk = tostring(key)
        -- Se lee SIEMPRE del archivo (1.14.16). Antes la cache en memoria ganaba
        -- para siempre: con el coordinador y sus mundos compartiendo user://,
        -- otra instancia podia haber guardado datos nuevos y esta seguiria
        -- devolviendo los viejos sin que nada avisara. La cache queda solo como
        -- respaldo por si el archivo no se puede leer.
        if _FileAccess then
            local content = _ds_leer(self._name, sk)
            if content and content ~= "" then
                local v = _ds_safe_decode(content)
                -- HAY archivo pero no parsea (JSON truncado por un cierre a
                -- medias): eso NO es "jugador nuevo". Devolver nil aqui hacia
                -- que el juego creara un perfil en blanco y pisara los datos
                -- buenos. En Roblox una lectura fallida LANZA y el juego
                -- reintenta, que es lo que hay que imitar.
                if v == nil then
                    error("DataStore request failed (stored value is corrupt)", 2)
                end
                self._cache[sk] = v
                return v
            end
        end
        return self._cache[sk]
    end

    function store:SetAsync(key, value)
        _ds_assert_server("SetAsync")
        local sk = tostring(key)
        self._cache[sk] = value
        if _FileAccess then
            local json = _ds_safe_encode(value)
            -- Si el contenido no cambio, no se toca el disco. Los autosaves
            -- periodicos reescriben el perfil ENTERO cada N segundos aunque no
            -- haya cambiado nada, y eso es I/O sincrono en el hilo principal.
            self._ultimo = self._ultimo or {}
            if self._ultimo[sk] ~= json then
                _FileAccess.write(_ds_path(self._name, sk), json)
                self._ultimo[sk] = json
            end
        end
    end

    function store:UpdateAsync(key, transform)
        local old = self:GetAsync(key)
        local new_val = transform(old)
        if new_val ~= nil then self:SetAsync(key, new_val) end
        return new_val
    end

    function store:IncrementAsync(key, delta)
        local val = (self:GetAsync(key) or 0) + (delta or 1)
        self:SetAsync(key, val)
        return val
    end

    function store:RemoveAsync(key)
        _ds_assert_server("RemoveAsync")
        local sk = tostring(key)
        -- Lo que habia, del disco: la cache de esta VM no tiene lo que guardo
        -- otro script o una partida anterior.
        local okv, old = pcall(self.GetAsync, self, sk)
        if not okv then old = self._cache[sk] end
        self._cache[sk] = nil
        if self._ultimo then self._ultimo[sk] = nil end
        if _FileAccess then
            -- Tambien la ruta vieja: GetAsync la sigue leyendo si falta la nueva,
            -- y la clave borrada volveria a aparecer.
            pcall(function()
                _FileAccess.delete(_ds_path(self._name, sk))
                local vieja = _ds_path_legacy(self._name, sk)
                if _FileAccess.exists(vieja) then _FileAccess.delete(vieja) end
            end)
        end
        return old
    end

    -- Todas las claves guardadas de ESTE store: las de esta VM y las de
    -- cualquier otro script, servidor o partida anterior (comparten user://).
    function store:_entradas_disco()
        local out = {}
        local ok, files = pcall(_FileAccess.list, "user://")
        if not ok or type(files) ~= "table" then return out end
        local pre = "ds_" .. _ds_sane(self._name) .. "__"
        for _, f in ipairs(files) do
            if string.sub(f, 1, #pre) == pre and string.sub(f, -5) == ".json" then
                local sk = string.gsub(string.sub(f, #pre + 1, -6), "%%(%x%x)", function(h)
                    return string.char(tonumber(h, 16))
                end)
                local v = _ds_safe_decode(_FileAccess.read("user://" .. f))
                if v ~= nil then out[sk] = v end
            end
        end
        return out
    end

    function store:GetSortedAsync(ascending, pageSize, minValue, maxValue)
        _ds_assert_server("GetSortedAsync")
        -- Del DISCO, no de la cache: antes una tabla de clasificacion solo veia
        -- lo que habia guardado este mismo script en esta partida.
        local datos = (_FileAccess and _FileAccess.list) and self:_entradas_disco() or self._cache
        local entries = {}
        for k, v in pairs(datos) do
            if type(v) == "number"
               and (minValue == nil or v >= minValue)
               and (maxValue == nil or v <= maxValue) then
                entries[#entries + 1] = { key = k, value = v }
            end
        end
        table.sort(entries, function(a, b)
            if a.value == b.value then return a.key < b.key end
            if ascending then return a.value < b.value end
            return a.value > b.value
        end)
        return _ds_paginas(entries, pageSize)
    end

    -- Versionado: sin backend de versiones, se expone la versión actual como
    -- única, para que ProfileService-like que llama ListVersionsAsync no rompa.
    function store:ListVersionsAsync(key, sortDirection, minDate, maxDate, pageSize)
        local val = self:GetAsync(key)
        local page = {}
        if val ~= nil then
            page[1] = { Version = "1", CreatedTime = 0, IsDeleted = false }
        end
        return {
            GetCurrentPage = function() return page end,
            AdvanceToNextPageAsync = function() end,
            IsFinished = true,
        }
    end
    function store:GetVersionAsync(key, version)
        return self:GetAsync(key)
    end
    function store:RemoveVersionAsync(key, version)
        return self:RemoveAsync(key)
    end

    return store
end

function DataStoreService:GetDataStore(name, scope)
    local key = scope and (name .. "_" .. scope) or name
    if not _ds_stores[key] then _ds_stores[key] = _make_datastore(key) end
    return _ds_stores[key]
end

function DataStoreService:GetOrderedDataStore(name, scope)
    local key = scope and (name .. "_" .. scope) or name
    if not _ds_ordered[key] then _ds_ordered[key] = _make_datastore("__ord_" .. key) end
    return _ds_ordered[key]
end

function DataStoreService:GetGlobalDataStore()
    return self:GetDataStore("__global")
end

function DataStoreService:GetRequestBudgetForRequestType(requestType)
    return 60
end

_G["__SVC_DataStoreService"] = DataStoreService
)LUAU"
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  HttpService — HTTP requests and JSON serialization
-- ////
--  HttpService — peticiones HTTP y serialización JSON
-- ══════════════════════════════════════════════════════════════════════
local HttpService = {}
HttpService.__index = HttpService
HttpService.HttpEnabled = true

function HttpService:JSONEncode(value)
    local ok, r = pcall(_JSON and _JSON.encode or function() return "{}" end, value)
    return ok and r or "{}"
end

function HttpService:JSONDecode(json)
    if not json or json == "" then return {} end
    local ok, r = pcall(_JSON and _JSON.decode or function() return {} end, json)
    return ok and r or {}
end

function HttpService:GetAsync(url, noCache, headers)
    if _HTTP then
        local ok, r = pcall(_HTTP.get, url, headers)
        if ok then return r end
    end
    warn("[HttpService] GetAsync sin conexión: " .. tostring(url))
    return ""
end

function HttpService:PostAsync(url, data, contentType, compress, headers)
    if _HTTP then
        local ok, r = pcall(_HTTP.post, url, data or "", contentType or "application/json", headers)
        if ok then return r end
    end
    warn("[HttpService] PostAsync sin conexión: " .. tostring(url))
    return ""
end

function HttpService:RequestAsync(options)
    local method  = (options.Method or "GET"):upper()
    local url     = options.Url or ""
    local body    = options.Body
    local headers = options.Headers
    if _HTTP and _HTTP.request then
        local ok, r = pcall(_HTTP.request, method, url, body, headers)
        if ok and type(r) == "table" then
            return {
                Success       = r.Success,
                StatusCode    = r.StatusCode,
                StatusMessage = r.StatusMessage,
                Body          = r.Body,
                Headers       = {},
            }
        end
    end
    warn("[HttpService] RequestAsync sin conexión: " .. tostring(url))
    return { Success = false, StatusCode = 0, StatusMessage = "No HTTP", Body = "" }
end

-- Como Roblox: UUID v4 en MAYUSCULAS y, salvo que se pase false, entre llaves
-- ("{0F8B...}"). Antes salia en minusculas y sin llaves por defecto, y el que
-- comparaba o recortaba el GUID con el formato de Roblox fallaba.
function HttpService:GenerateGUID(wrapInCurlyBraces)
    local function h(n) return string.format("%0" .. n .. "X", math.random(0, 16 ^ n - 1)) end
    local guid = h(4) .. h(4) .. "-" .. h(4) .. "-4" .. h(3) .. "-"
        .. string.format("%X", math.random(8, 11)) .. h(3) .. "-" .. h(4) .. h(4) .. h(4)
    if wrapInCurlyBraces == false then return guid end
    return "{" .. guid .. "}"
end

function HttpService:UrlEncode(input)
    return (tostring(input):gsub("[^%w%-_%.~]", function(c)
        return string.format("%%%02X", string.byte(c))
    end))
end

_G["__SVC_HttpService"] = HttpService

-- ══════════════════════════════════════════════════════════════════════
--  ContextActionService — binding de acciones a teclas
-- ══════════════════════════════════════════════════════════════════════
local ContextActionService = {}
ContextActionService.__index = ContextActionService
local _cas_actions = {}

-- ── Despacho REAL ────────────────────────────────────────────────────
-- Antes BindAction solo GUARDABA la accion: ninguna tecla la disparaba
-- jamas (los hotbars de inventario de los places reales, que seleccionan
-- la Tool con ContextActionService + Enum.KeyCode.One..Nine, estaban
-- muertos). Se engancha a UserInputService en el primer BindAction (lazy:
-- al cargar el stdlib el arbol puede no tener UIS todavia).
local _cas_hooked = false

local function _cas_coincide(k, item)
    -- k = lo que se paso a BindAction (EnumItem de KeyCode/UserInputType, o
    -- un numero pelado); item = input.KeyCode / input.UserInputType.
    if type(item) ~= "table" then return false end
    if type(k) == "table" then
        -- El __eq de EnumItem compara solo Value: sin mirar EnumType, un
        -- KeyCode y un UserInputType con el mismo numero se cruzarian.
        if rawget(k, "EnumType") ~= rawget(item, "EnumType") then return false end
        return k == item
    end
    if type(k) == "number" then return rawget(item, "Value") == k end
    return false
end

local function _cas_dispatch(state, input)
    -- Orden: prioridad alta primero (BindActionAtPriority); resto, da igual.
    local lista = {}
    for name, a in pairs(_cas_actions) do
        lista[#lista + 1] = { name = name, a = a }
    end
    table.sort(lista, function(x, y) return (x.a.priority or 2000) > (y.a.priority or 2000) end)
    for _, e in ipairs(lista) do
        for _, k in ipairs(e.a.keys) do
            if _cas_coincide(k, input.KeyCode) or _cas_coincide(k, input.UserInputType) then
                local ok, r = pcall(e.a.func, e.name, state, input)
                -- Como Roblox: devolver Pass deja seguir; nil o Sink corta.
                if not ok or r ~= Enum.ContextActionResult.Pass then return end
                break
            end
        end
    end
end

local function _cas_hook()
    if _cas_hooked then return end
    local ok, UIS = pcall(function() return game:GetService("UserInputService") end)
    if not ok or UIS == nil then return end
    _cas_hooked = true
    -- Del SISTEMA del contexto (F8): si fueran del primer script que hizo
    -- BindAction, al morir ese script ninguna accion volveria a saltar.
    ;(__gl_sistema or task.spawn)(function()
        UIS.InputBegan:Connect(function(input) _cas_dispatch(Enum.UserInputState.Begin, input) end)
        UIS.InputChanged:Connect(function(input) _cas_dispatch(Enum.UserInputState.Change, input) end)
        UIS.InputEnded:Connect(function(input) _cas_dispatch(Enum.UserInputState.End, input) end)
    end)
end

function ContextActionService:BindAction(name, func, createButton, ...)
    _cas_actions[name] = { func = func, keys = {...}, button = createButton }
    _cas_hook()
end

function ContextActionService:UnbindAction(name)
    _cas_actions[name] = nil
end

function ContextActionService:BindActionAtPriority(name, func, createButton, priority, ...)
    _cas_actions[name] = { func = func, keys = {...}, button = createButton, priority = priority }
    _cas_hook()
end

function ContextActionService:GetBoundActionInfo(name)
    return _cas_actions[name]
end

function ContextActionService:GetAllBoundActionInfo()
    return _cas_actions
end

function ContextActionService:CallFunction(name, state, inputObject)
    local action = _cas_actions[name]
    if action and action.func then
        pcall(action.func, name, state, inputObject)
    end
end

function ContextActionService:SetTitle(name, title) end
function ContextActionService:SetImage(name, image) end
function ContextActionService:SetPosition(name, position) end

_G["__SVC_ContextActionService"] = ContextActionService

-- ══════════════════════════════════════════════════════════════════════
--  PathfindingService — agent navigation (functional stub)
-- ////
--  PathfindingService — navegación de agentes (stub funcional)
-- ══════════════════════════════════════════════════════════════════════
local PathfindingService = {}
PathfindingService.__index = PathfindingService

-- PathWaypoint.new(posicion, accion, etiqueta), el tipo de Roblox.
if not PathWaypoint then
    local MT = { __type = "PathWaypoint" }
    MT.__index = MT
    function MT:__tostring() return "PathWaypoint" end
    PathWaypoint = { new = function(position, action, label)
        return setmetatable({ Position = position or Vector3.new(0, 0, 0),
            Action = action or Enum.PathWaypointAction.Walk, Label = label or "" }, MT)
    end }
end

-- Un camino de verdad (gl_navegacion.h): ComputeAsync hornea una malla de
-- navegacion en la zona y cede hasta tenerla. Antes era una linea recta de dos
-- puntos que atravesaba paredes, y su Status era un numero: con los Enum ya
-- convertidos en EnumItem, `path.Status ~= Enum.PathStatus.Success` daba
-- SIEMPRE true y los juegos tiraban todos los caminos.
function PathfindingService:CreatePath(agentParameters)
    local params = agentParameters or {}
    local function opcion(k, def)
        local v = params[k]
        if v == nil then return def end
        return v
    end
    local path = {
        Status = Enum.PathStatus.NoPath,
        ClassName = "Path",
        Name = "Path",
        Blocked = _gl_signal(),
        Unblocked = _gl_signal(),
    }
    local waypoints = {}
    local vivo = true

    function path:IsA(c) return c == "Path" or c == "Instance" end
    function path:Destroy() vivo = false; waypoints = {} end

    function path:GetWaypoints()
        local copia = {}
        for i, w in ipairs(waypoints) do copia[i] = w end
        return copia
    end

    function path:ComputeAsync(startPos, goalPos)
        if typeof(startPos) ~= "Vector3" or typeof(goalPos) ~= "Vector3" then
            error("ComputeAsync expects two Vector3 positions", 2)
        end
        waypoints = {}
        self.Status = Enum.PathStatus.NoPath
        local api = __gl_nav
        local partial = type(params.PathSettings) == "table" and params.PathSettings.SupportPartialPath == true
        local id = api and api.pedir(startPos, goalPos, opcion("AgentRadius", 2), opcion("AgentHeight", 5),
            opcion("AgentCanJump", true), opcion("WaypointSpacing", 4), partial)
        if not id then return end
        local status, puntos, acciones
        repeat
            task.wait()
            status, puntos, acciones = api.estado(id)
        until status ~= nil
        for i, p in ipairs(puntos) do
            local accion = acciones[i] == 1 and Enum.PathWaypointAction.Jump or Enum.PathWaypointAction.Walk
            waypoints[i] = PathWaypoint.new(p, accion, "")
        end
        self.Status = __gl_enum_item("PathStatus", status)
    end

    -- El primer tramo cortado desde `start` (indice del waypoint del que sale),
    -- o -1. Rayos a media altura del agente; los personajes no cuentan.
    function path:CheckOcclusionAsync(start)
        start = math.max(1, tonumber(start) or 1)
        if #waypoints < 2 then return -1 end
        local rp = RaycastParams.new()
        rp.FilterType = Enum.RaycastFilterType.Exclude
        local fuera = {}
        for _, plr in ipairs(game:GetService("Players"):GetPlayers()) do
            if plr.Character then table.insert(fuera, plr.Character) end
        end
        rp.FilterDescendantsInstances = fuera
        local alto = Vector3.new(0, opcion("AgentHeight", 5) * 0.5, 0)
        for i = start, #waypoints - 1 do
            local a = waypoints[i].Position + alto
            local b = waypoints[i + 1].Position + alto
            local hit = workspace:Raycast(a, b - a, rp)
            if hit and hit.Instance and hit.Instance.CanCollide then return i end
        end
        return -1
    end

    -- Blocked/Unblocked: mientras alguien escuche, se mira cada segundo si algo
    -- corta el camino (Roblox lo vigila solo).
    local vigilando = false
    local function vigilar()
        if vigilando then return end
        vigilando = true
        task.spawn(function()
            local cortado = nil
            while vivo do
                task.wait(1)
                if #waypoints >= 2 then
                    local i = path:CheckOcclusionAsync(1)
                    if i ~= -1 and i ~= cortado then
                        cortado = i
                        path.Blocked:Fire(i)
                    elseif i == -1 and cortado then
                        path.Unblocked:Fire(cortado)
                        cortado = nil
                    end
                end
            end
        end)
    end
    for _, sig in ipairs({ path.Blocked, path.Unblocked }) do
        for _, metodo in ipairs({ "Connect", "Once", "Wait" }) do
            local original = sig[metodo]
            sig[metodo] = function(s, ...) vigilar() return original(s, ...) end
        end
        sig.connect, sig.once, sig.wait = sig.Connect, sig.Once, sig.Wait
    end

    return path
end

-- La forma vieja (FindPathAsync) devuelve el camino ya calculado.
function PathfindingService:FindPathAsync(startPos, goalPos)
    local p = self:CreatePath()
    p:ComputeAsync(startPos, goalPos)
    return p
end

_G["__SVC_PathfindingService"] = PathfindingService

-- ══════════════════════════════════════════════════════════════════════
--  PhysicsService — collision groups and filters
-- ////
--  PhysicsService — grupos y filtros de colisión
-- ══════════════════════════════════════════════════════════════════════
local PhysicsService = {}
PhysicsService.__index = PhysicsService
local _ps_groups = {}

-- Todo esto va contra la matriz DE VERDAD del motor (gl_colgroups.h en C++),
-- que se traduce a capas y mascaras de Godot. Antes era una tabla de Lua que no
-- llegaba a la fisica: dos grupos "no colisionables" seguian chocando.
function PhysicsService:RegisterCollisionGroup(name)
    __gl_colgroup("reg", name)
end

function PhysicsService:CreateCollisionGroup(name)
    self:RegisterCollisionGroup(name)
end

function PhysicsService:RemoveCollisionGroup(name)
    __gl_colgroup("del", name)
end

function PhysicsService:CollisionGroupSetCollidable(name1, name2, collidable)
    __gl_colgroup("set", name1, name2, collidable and true or false)
end

function PhysicsService:CollisionGroupsAreCollidable(name1, name2)
    return __gl_colgroup("get", name1, name2)
end

-- API moderna de Roblox: la propiedad BasePart.CollisionGroup. Las dos formas
-- (la propiedad y estos metodos, que estan obsoletos pero se siguen usando)
-- acaban en el mismo sitio.
function PhysicsService:SetPartCollisionGroup(part, groupName)
    pcall(function() part.CollisionGroup = groupName end)
end

function PhysicsService:GetPartCollisionGroup(part)
    local ok, v = pcall(function() return part.CollisionGroup end)
    return (ok and v) or "Default"
end

function PhysicsService:RegisterCollisionGroupTo(name) self:RegisterCollisionGroup(name) end

-- Sin registrarlo de paso (GetCollisionGroupId si lo registra).
function PhysicsService:IsCollisionGroupRegistered(name)
    return __gl_colgroup("existe", name) == true
end

function PhysicsService:GetCollisionGroupId(name)
    return __gl_colgroup("id", name)
end

function PhysicsService:GetCollisionGroupName(id)
    return __gl_colgroup("name", id)
end

_G["__SVC_PhysicsService"] = PhysicsService
)LUAU"
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  TeleportService — teletransporte entre lugares (stub)
-- ══════════════════════════════════════════════════════════════════════
local TeleportService = {}
TeleportService.__index = TeleportService
local _ts_callbacks = {}

-- 1.14.17: teleport REAL entre los mundos de un mismo coordinador. Aqui un
-- "place" es una instancia-mundo; se descubren solas por los latidos de user://
-- que el coordinador ya escribia, asi que un mundo puede mandarte a otro sin
-- hablar con nadie. placeId se acepta como PUERTO destino; si es nil o 0, va al
-- mundo con menos gente que no sea este.
local _ts_llegada = {}

TeleportService.TeleportInitFailed = { Connect = function(_, fn) end }
TeleportService.LocalPlayerArrivedFromTeleport = {
    Connect = function(_, fn)
        local e = { fn = fn, dueno = __gl_dueno and __gl_dueno() }
        table.insert(_ts_llegada, e)
        return { Connected = true, Disconnect = function(self)
            self.Connected = false
            for i, x in ipairs(_ts_llegada) do if x == e then table.remove(_ts_llegada, i) break end end
        end }
    end
}

function TeleportService:Teleport(placeId, player, teleportData, customLoadingScreen)
    player = player or (game:GetService("Players").LocalPlayer)
    if not player then warn("[TeleportService] Teleport sin jugador.") return end
    if not _GL_NET then warn("[TeleportService] Teleport necesita una sesion de red.") return end
    local datos = ""
    if teleportData ~= nil and _JSON then
        local ok, s = pcall(_JSON.encode, teleportData)
        datos = ok and s or ""
    end
    local destino = _GL_NET.teleport(player, tonumber(placeId) or 0, datos)
    if destino == 0 then
        warn("[TeleportService] Teleport: no hay otro mundo al que ir (¿coordinador activo?).")
    end
    return destino
end

function TeleportService:TeleportAsync(placeId, players, teleportOptions)
    local datos = teleportOptions and teleportOptions.TeleportData or nil
    for _, p in ipairs(players or {}) do self:Teleport(placeId, p, datos) end
    return { Instances = players or {} }
end

function TeleportService:TeleportToSpawnByName(placeId, spawnName, player, teleportData)
    -- El nombre del spawn viaja en los datos; el mundo destino decide que hacer.
    local d = teleportData or {}
    if type(d) == "table" then d.SpawnName = spawnName end
    return self:Teleport(placeId, player, d)
end

function TeleportService:TeleportToPlaceInstance(placeId, instanceId, player, teleportData)
    -- instanceId = el PUERTO del mundo concreto.
    return self:Teleport(tonumber(instanceId) or placeId, player, teleportData)
end

function TeleportService:TeleportPartyAsync(placeId, players, teleportOptions)
    return self:TeleportAsync(placeId, players, teleportOptions)
end

-- Mundos vivos (puertos). Extra de GodotLuau: en Roblox no existe.
function TeleportService:GetWorlds()
    return _GL_NET and _GL_NET.call("net_world_ports") or {}
end

function TeleportService:GetLocalPlayerTeleportData()
    if not _GL_NET then return nil end
    local s = _GL_NET.call("net_teleport_data")
    if not s or s == "" or not _JSON then return nil end
    local ok, d = pcall(_JSON.decode, s)
    return ok and d or nil
end

-- Al entrar a un mundo, si venimos de un teleport se avisa una sola vez.
--
-- Solo existe si hay red (en un jugador no hay teleports entre mundos). Hasta
-- F8 el stdlib corria en CADA script y esto eran cientos de bucles en fase;
-- ahora hay uno por contexto, y es del sistema del contexto (el stdlib se
-- carga en su hilo principal).
if _GL_NET then
    task.spawn(function()
        task.wait(0.5 + math.random() * 1.0)
        while _GL_NET do
            if _GL_NET.call("net_teleport_arrived") then
                local d = TeleportService:GetLocalPlayerTeleportData()
                for _, e in ipairs(_ts_llegada) do
                    if e.dueno ~= nil then __gl_spawn_dueno(e.dueno, e.fn, d)
                    else task.spawn(e.fn, d) end
                end
            end
            task.wait(1)
        end
    end)
end

function TeleportService:GetTeleportSetting(name)
    return nil
end

function TeleportService:SetTeleportSetting(name, value) end
function TeleportService:SetTeleportGui(gui) end

_G["__SVC_TeleportService"] = TeleportService

)LUAU"
// Segunda mitad del stdlib — MSVC no permite string literals > 65535 chars
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  BadgeService — sistema de medallas (stub con logging)
-- ══════════════════════════════════════════════════════════════════════
local BadgeService = {}
BadgeService.__index = BadgeService

function BadgeService:AwardBadge(userId, badgeId)
    print("[BadgeService] AwardBadge:", userId, badgeId, "— en juego real se enviaría al servidor.")
    return true
end

function BadgeService:UserHasBadgeAsync(userId, badgeId)
    return false
end

function BadgeService:GetBadgeInfoAsync(badgeId)
    return {
        Name        = "Badge " .. tostring(badgeId),
        Description = "",
        IsEnabled   = true,
        Points      = 0,
        IconImageId = 0,
    }
end

_G["__SVC_BadgeService"] = BadgeService

-- ══════════════════════════════════════════════════════════════════════
--  SocialService — invitar amigos. Sin backend no se puede invitar a nadie,
--  pero el servicio TIENE que existir: sin el, un game:GetService da nil y el
--  script muere en la primera linea.
-- ══════════════════════════════════════════════════════════════════════
local SocialService = {}
SocialService.__index = SocialService
SocialService.GameInvitePromptClosed = { Connect = function(_, fn) return { Disconnect = function() end, Connected = true } end }

function SocialService:CanSendGameInviteAsync(player, experienceId)
    return false
end

function SocialService:PromptGameInvite(player, inviteOptions)
    warn("[SocialService] PromptGameInvite no tiene backend en modo offline.")
end

_G["__SVC_SocialService"] = SocialService
)LUAU"
R"LUAU(

-- ══════════════════════════════════════════════════════════════════════
--  GuiService — user interface management
-- ////
--  GuiService — gestión de interfaces de usuario
-- ══════════════════════════════════════════════════════════════════════
local GuiService = {}
GuiService.__index = GuiService

GuiService.MenuOpened  = { Connect = function() end }
GuiService.MenuClosed  = { Connect = function() end }
GuiService.ErrorMessageChanged = { Connect = function() end }

function GuiService:GetScreenResolution()
    return Vector3.new(1920, 1080, 0)
end

function GuiService:SetMenuIsOpen(open) end
function GuiService:GetInspectMenuEnabled() return false end
function GuiService:GetNotificationTypeList() return {} end
function GuiService:CloseInspectMenu() end
function GuiService:InspectPlayerFromHumanoidDescription(humanoidDescription, name) end
function GuiService:SetGlobalGuiInset(inset) end
function GuiService:GetGlobalGuiInset() return 0, 0, 0, 0 end
-- GetGuiInset() lo define el bloque de "servicios que faltaban" mas abajo, con
-- el hueco de 36 px de la barra de Roblox: los juegos posicionan contra el.
function GuiService:IsTenFootInterface() return false end
function GuiService:IsKeyboardGamepadNavigationEnabled() return false end

_G["__SVC_GuiService"] = GuiService

-- ══════════════════════════════════════════════════════════════════════
--  InsertService — external asset loader (stub)
-- ////
--  InsertService — carga de assets externos (stub)
-- ══════════════════════════════════════════════════════════════════════
local InsertService = {}
InsertService.__index = InsertService

function InsertService:LoadAsset(assetId)
    warn("[InsertService] LoadAsset(" .. tostring(assetId) .. ") no soportado en GodotLuau.")
    return nil
end

function InsertService:LoadAssetVersion(assetVersionId)
    warn("[InsertService] LoadAssetVersion no soportado.")
    return nil
end

function InsertService:GetFreeDecals(searchText, pageNum) return {} end
function InsertService:GetFreeModels(searchText, pageNum) return {} end
function InsertService:GetProductInfo(assetId) return {} end

_G["__SVC_InsertService"] = InsertService

-- ══════════════════════════════════════════════════════════════════════
--  ScriptContext — script execution context
-- ////
--  ScriptContext — contexto de ejecución de scripts
-- ══════════════════════════════════════════════════════════════════════
local ScriptContext = {}
ScriptContext.__index = ScriptContext

ScriptContext.Error = {
    Connect = function(_, fn)
        -- Could be hooked to the global error handler in the future / Podría hookearse al error handler global en el futuro
    end
}

function ScriptContext:GetCurrent() return nil end
function ScriptContext:AddCoreScriptLocal(name, parent) end

_G["__SVC_ScriptContext"] = ScriptContext

)LUAU"
