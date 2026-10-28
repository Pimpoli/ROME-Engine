// Stdlib de Luau, parte 5: Content y AssetService (EditableImage,
// EditableMesh y CreateMeshPartAsync). Lo de C++ esta en gl_puente_editables.h.
//
// No es un header normal: se incluye DENTRO del literal LUAU_STDLIB_CODE de
// luau_stdlib.h, y los literales de texto seguidos se juntan en uno solo al
// compilar.
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  Content — referencia a un asset (Uri) o a un objeto (EditableImage,
--  EditableMesh). Inmutable; typeof(c) == "Content".
--  https://create.roblox.com/docs/reference/engine/datatypes/Content
--  El C++ lee los campos crudos _t (0 None, 1 Uri, 2 Object), _u y _o.
-- ══════════════════════════════════════════════════════════════════════
do
    local ContentMT = { __type = "Content" }
    local function nuevo(t, u, o)
        return setmetatable({ _t = t, _u = u, _o = o }, ContentMT)
    end
    ContentMT.__index = function(c, k)
        if k == "SourceType" then
            local t = rawget(c, "_t")
            if t == 1 then return Enum.ContentSourceType.Uri end
            if t == 2 then return Enum.ContentSourceType.Object end
            return Enum.ContentSourceType.None
        elseif k == "Uri" then
            return rawget(c, "_u")
        elseif k == "Object" then
            return rawget(c, "_o")
        elseif k == "Opaque" then
            return nil
        end
        error(tostring(k) .. " is not a valid member of Content", 2)
    end
    ContentMT.__newindex = function(_, k)
        error(tostring(k) .. " cannot be assigned to", 2)
    end
    ContentMT.__eq = function(a, b)
        return rawget(a, "_t") == rawget(b, "_t") and rawget(a, "_u") == rawget(b, "_u")
            and rawequal(rawget(a, "_o"), rawget(b, "_o"))
    end
    ContentMT.__tostring = function(c)
        local t = rawget(c, "_t")
        if t == 1 then return rawget(c, "_u") end
        if t == 2 then return tostring(rawget(c, "_o")) end
        return ""
    end

    local C = {}
    C.none = nuevo(0, nil, nil)
    function C.fromUri(uri)
        if type(uri) ~= "string" then
            error("invalid argument #1 to 'fromUri' (string expected, got " .. typeof(uri) .. ")", 2)
        end
        if uri == "" then return C.none end
        return nuevo(1, uri, nil)
    end
    function C.fromAssetId(id)
        if type(id) ~= "number" then
            error("invalid argument #1 to 'fromAssetId' (number expected, got " .. typeof(id) .. ")", 2)
        end
        if id ~= id or id == math.huge or id == -math.huge then
            error("invalid argument #1 to 'fromAssetId' (asset id must be finite)", 2)
        end
        if id == 0 then return C.none end
        local texto
        if id == math.floor(id) and math.abs(id) < 2 ^ 53 then
            texto = string.format("%d", id)
        else
            texto = tostring(id)
        end
        return nuevo(1, "rbxassetid://" .. texto, nil)
    end
    function C.fromObject(obj)
        local t = typeof(obj)
        if t ~= "Object" and t ~= "Instance" then
            error("invalid argument #1 to 'fromObject' (Object expected, got " .. t .. ")", 2)
        end
        return nuevo(2, nil, obj)
    end
    Content = setmetatable(C, {
        __index = function(_, k)
            error(tostring(k) .. " is not a valid member of Content", 2)
        end,
    })
end

-- ══════════════════════════════════════════════════════════════════════
--  AssetService — lo de EditableImage / EditableMesh / CreateMeshPartAsync.
--  Lo demas de AssetService no existe aqui y da el error de miembro de
--  Roblox, como cualquier miembro que no esta.
-- ══════════════════════════════════════════════════════════════════════
do
    local AS = { Name = "AssetService", ClassName = "AssetService" }
    -- Las funciones de C++ (gl_puente_editables.h) reciben el servicio como self.
    local crearImagen, crearImagenAsync = __gl_as_CreateEditableImage, __gl_as_CreateEditableImageAsync
    local crearMalla, crearMallaAsync = __gl_as_CreateEditableMesh, __gl_as_CreateEditableMeshAsync
    local crearPieza = __gl_as_CreateMeshPartAsync
    function AS:CreateEditableImage(...) return crearImagen(self, ...) end
    function AS:CreateEditableImageAsync(...) return crearImagenAsync(self, ...) end
    function AS:CreateEditableMesh(...) return crearMalla(self, ...) end
    function AS:CreateEditableMeshAsync(...) return crearMallaAsync(self, ...) end
    function AS:CreateMeshPartAsync(...) return crearPieza(self, ...) end
    function AS:IsA(c) return c == "AssetService" or c == "Instance" end
    function AS:GetPropertyChangedSignal() return _gl_signal() end
    AS.Changed = _gl_signal()
    setmetatable(AS, {
        __type = "Instance",
        __tostring = function() return "AssetService" end,
        __index = function(_, k)
            if k == "Parent" then return game end
            error(tostring(k) .. " is not a valid member of AssetService \"AssetService\"", 2)
        end,
    })
    _G["__SVC_AssetService"] = AS
end
)LUAU"
