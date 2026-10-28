// Stdlib de Luau, parte 3 de 4: los tipos de datos: Vector2, UDim, UDim2, Font, CFrame,
// Instance.new, los Value, extras de Color3 y Vector3, Random, Region3,
// Rect, NumberSequence, ColorSequence y BrickColor.
//
// No es un header normal: se incluye DENTRO del literal LUAU_STDLIB_CODE de
// luau_stdlib.h, y los literales de texto seguidos se juntan en uno
// solo al compilar. Va aparte porque el stdlib pasaba de 2.000 lineas.
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  Vector2 — 2D vector (UI coordinates, input, etc.)
-- ////
--  Vector2 — vector 2D (coordenadas de UI, input, etc.)
-- ══════════════════════════════════════════════════════════════════════
Vector2 = {}
-- .Magnitude y .Unit son PROPIEDADES en Roblox (no metodos): con __index
-- tabla no existian y `(a - b).Magnitude` daba nil en todo el codigo tactil.
Vector2.__index = function(self, k)
    if k == "Magnitude" then return math.sqrt(self.X * self.X + self.Y * self.Y) end
    if k == "Unit" then
        local m = math.sqrt(self.X * self.X + self.Y * self.Y)
        if m < 1e-8 then return Vector2.new(0, 0) end
        return Vector2.new(self.X / m, self.Y / m)
    end
    return Vector2[k]
end
Vector2.__type = "Vector2"   -- para que typeof(v) devuelva "Vector2"

-- Componentes en float32, como Roblox (Vector2.new(0.1, 0).X = 0.1000000015).
function Vector2.new(x, y)
    x = __gl_f32(x or 0)
    y = __gl_f32(y or 0)
    return setmetatable({ X = x, Y = y, x = x, y = y }, Vector2)
end
function Vector2.__tostring(v) return "(" .. v.X .. ", " .. v.Y .. ")" end
function Vector2.__add(a, b)   return Vector2.new(a.X + b.X, a.Y + b.Y) end
function Vector2.__sub(a, b)   return Vector2.new(a.X - b.X, a.Y - b.Y) end
-- El numero puede ir a cualquier lado: `2 * v` llegaba con a = 2 y moria
-- indexando un numero.
function Vector2.__mul(a, b)
    if type(a) == "number" then return Vector2.new(a * b.X, a * b.Y) end
    if type(b) == "number" then return Vector2.new(a.X * b, a.Y * b) end
    return Vector2.new(a.X * b.X, a.Y * b.Y)
end
function Vector2.__div(a, b)
    if type(a) == "number" then return Vector2.new(a / b.X, a / b.Y) end
    if type(b) == "number" then return Vector2.new(a.X / b, a.Y / b) end
    return Vector2.new(a.X / b.X, a.Y / b.Y)
end
function Vector2.__idiv(a, b)
    local q = a / b
    return Vector2.new(math.floor(q.X), math.floor(q.Y))
end
function Vector2.__unm(a)    return Vector2.new(-a.X, -a.Y) end
function Vector2.__eq(a, b)  return a.X == b.X and a.Y == b.Y end
function Vector2:magnitude()  return math.sqrt(self.X * self.X + self.Y * self.Y) end
function Vector2:normalized() local m = self:magnitude(); if m < 0.0001 then return Vector2.new(0,0) end; return Vector2.new(self.X/m, self.Y/m) end
function Vector2:dot(v2)      return self.X * v2.X + self.Y * v2.Y end
function Vector2:lerp(v2, t)  return Vector2.new(self.X + (v2.X - self.X)*t, self.Y + (v2.Y - self.Y)*t) end
-- Nombres reales de Roblox (los de minuscula se quedan por compatibilidad)
Vector2.Dot  = Vector2.dot
Vector2.Lerp = Vector2.lerp
function Vector2:Cross(v2)    return self.X * v2.Y - self.Y * v2.X end
function Vector2:Abs()   return Vector2.new(math.abs(self.X), math.abs(self.Y)) end
function Vector2:Ceil()  return Vector2.new(math.ceil(self.X), math.ceil(self.Y)) end
function Vector2:Floor() return Vector2.new(math.floor(self.X), math.floor(self.Y)) end
function Vector2:Sign()  return Vector2.new(math.sign(self.X), math.sign(self.Y)) end
function Vector2:Min(...)
    local x, y = self.X, self.Y
    for _, v in ipairs({...}) do x = math.min(x, v.X); y = math.min(y, v.Y) end
    return Vector2.new(x, y)
end
function Vector2:Max(...)
    local x, y = self.X, self.Y
    for _, v in ipairs({...}) do x = math.max(x, v.X); y = math.max(y, v.Y) end
    return Vector2.new(x, y)
end
-- Angulo con signo de self a v2 (sentido antihorario positivo), como Roblox.
function Vector2:Angle(v2, isSigned)
    local a = math.atan2(self:Cross(v2), self:Dot(v2))
    if isSigned then return a end
    return math.abs(a)
end
function Vector2:FuzzyEq(v2, eps)
    eps = eps or 1e-5
    return math.abs(self.X - v2.X) <= eps and math.abs(self.Y - v2.Y) <= eps
end
Vector2.zero  = Vector2.new(0, 0)
Vector2.one   = Vector2.new(1, 1)
Vector2.xAxis = Vector2.new(1, 0)
Vector2.yAxis = Vector2.new(0, 1)

-- ══════════════════════════════════════════════════════════════════════
--  UDim — single dimension (scale + offset)
-- ////
--  UDim — dimensión única (scale + offset)
-- ══════════════════════════════════════════════════════════════════════
UDim = {}
UDim.__index = UDim
UDim.__type = "UDim"
-- Como Roblox: Scale en float32 y Offset en int32 truncado hacia cero
-- (UDim.new(0.1, 12.5) -> 0.1000000015, 12). Asi lo que se escribe en un
-- Frame y lo que se lee de vuelta son iguales con ==, tambien tras + y -.
-- /// Like Roblox: float32 Scale and int32 Offset truncated toward zero.
local _gl_f32 = __gl_f32
local function _gl_udim_offset(o)
    if o >= 0 then return math.floor(o) end
    return math.ceil(o) + 0   -- + 0: sin "-0"
end
function UDim.new(scale, offset)
    return setmetatable({ Scale = _gl_f32(scale or 0), Offset = _gl_udim_offset(offset or 0) }, UDim)
end
function UDim.__add(a, b) return UDim.new(a.Scale + b.Scale, a.Offset + b.Offset) end
function UDim.__sub(a, b) return UDim.new(a.Scale - b.Scale, a.Offset - b.Offset) end
function UDim.__unm(a)    return UDim.new(-a.Scale, -a.Offset) end
function UDim.__eq(a, b)  return a.Scale == b.Scale and a.Offset == b.Offset end
function UDim.__tostring(u) return string.format("%g, %g", u.Scale, u.Offset) end

-- ══════════════════════════════════════════════════════════════════════
--  UDim2 — Roblox-style 2D position/size
--  Usage:  UDim2.new(xScale, xOffset, yScale, yOffset)
--          UDim2.fromScale(xS, yS)
--          UDim2.fromOffset(xO, yO)
--  Access: size.X.Scale, size.X.Offset, size.Y.Scale, size.Y.Offset
-- ////
--  UDim2 — posición/tamaño bidimensional tipo Roblox
--  Uso: UDim2.new(xScale, xOffset, yScale, yOffset)
--       UDim2.fromScale(xS, yS)
--       UDim2.fromOffset(xO, yO)
--  Acceso: size.X.Scale, size.X.Offset, size.Y.Scale, size.Y.Offset
-- ══════════════════════════════════════════════════════════════════════
-- ══════════════════════════════════════════════════════════════════════
--  Font — la API NUEVA de fuentes de Roblox (la de Enum.Font es la vieja).
--  Uso: Font.new(family, weight, style)  ·  Font.fromEnum(Enum.Font.Gotham)
--       label.FontFace = Font.new("rbxasset://fonts/families/Arial.json")
--  Los juegos modernos usan FontFace en vez de Font; sin esto el script moria.
-- ══════════════════════════════════════════════════════════════════════
Font = {}
Font.__index = Font
Font.__type = "Font"

function Font.new(family, weight, style)
    return setmetatable({
        Family = family or "rbxasset://fonts/families/SourceSansPro.json",
        Weight = weight or Enum.FontWeight.Regular,
        Style  = style  or Enum.FontStyle.Normal,
        Bold   = false,
    }, Font)
end
-- Font.fromEnum como en Roblox (su tabla, sacada de Studio; gl_fuentes.h
-- tiene la misma para dibujar). Antes daba siempre SourceSans normal.
local _FUENTES_ENUM = {
    [0] = {"LegacyArial", 400, 0}, [1] = {"Arial", 400, 0}, [2] = {"Arial", 700, 0},
    [3] = {"SourceSansPro", 400, 0}, [4] = {"SourceSansPro", 700, 0}, [5] = {"SourceSansPro", 300, 0},
    [6] = {"SourceSansPro", 400, 1}, [7] = {"AccanthisADFStd", 400, 0}, [8] = {"Guru", 400, 0},
    [9] = {"ComicNeueAngular", 400, 0}, [10] = {"Inconsolata", 400, 0}, [11] = {"HighwayGothic", 400, 0},
    [12] = {"Zekton", 400, 0}, [13] = {"PressStart2P", 400, 0}, [14] = {"Balthazar", 400, 0},
    [15] = {"RomanAntique", 400, 0}, [16] = {"SourceSansPro", 600, 0}, [17] = {"GothamSSm", 400, 0},
    [18] = {"GothamSSm", 500, 0}, [19] = {"GothamSSm", 700, 0}, [20] = {"GothamSSm", 900, 0},
    [21] = {"AmaticSC", 400, 0}, [22] = {"Bangers", 400, 0}, [23] = {"Creepster", 400, 0},
    [24] = {"DenkOne", 400, 0}, [25] = {"Fondamento", 400, 0}, [26] = {"FredokaOne", 400, 0},
    [27] = {"GrenzeGotisch", 400, 0}, [28] = {"IndieFlower", 400, 0}, [29] = {"JosefinSans", 400, 0},
    [30] = {"Jura", 400, 0}, [31] = {"Kalam", 400, 0}, [32] = {"LuckiestGuy", 400, 0},
    [33] = {"Merriweather", 400, 0}, [34] = {"Michroma", 400, 0}, [35] = {"Nunito", 400, 0},
    [36] = {"Oswald", 400, 0}, [37] = {"PatrickHand", 400, 0}, [38] = {"PermanentMarker", 400, 0},
    [39] = {"Roboto", 400, 0}, [40] = {"RobotoCondensed", 400, 0}, [41] = {"RobotoMono", 400, 0},
    [42] = {"Sarpanch", 400, 0}, [43] = {"SpecialElite", 400, 0}, [44] = {"TitilliumWeb", 400, 0},
    [45] = {"Ubuntu", 400, 0}, [46] = {"BuilderSans", 400, 0}, [47] = {"BuilderSans", 500, 0},
    [48] = {"BuilderSans", 700, 0}, [49] = {"BuilderSans", 800, 0}, [50] = {"Arimo", 400, 0},
    [51] = {"Arimo", 700, 0},
}
local _PESOS = { [100] = "Thin", [200] = "ExtraLight", [300] = "Light", [400] = "Regular", [500] = "Medium",
                 [600] = "SemiBold", [700] = "Bold", [800] = "ExtraBold", [900] = "Heavy" }
function Font.fromEnum(e)
    local v = if type(e) == "table" then rawget(e, "Value") else e
    local d = _FUENTES_ENUM[v]
    if not d then error("Font.fromEnum: " .. tostring(e) .. " is not a valid Enum.Font", 2) end
    local estilo = if d[3] == 1 then Enum.FontStyle.Italic else Enum.FontStyle.Normal
    return Font.new("rbxasset://fonts/families/" .. d[1] .. ".json", Enum.FontWeight[_PESOS[d[2]]], estilo)
end
function Font.fromName(name, weight, style)
    return Font.new("rbxasset://fonts/families/" .. tostring(name) .. ".json", weight, style)
end
function Font.fromId(id, weight, style)
    return Font.new("rbxassetid://" .. tostring(id), weight, style)
end
function Font.__tostring(f) return "Font(" .. tostring(f.Family) .. ")" end
function Font.__eq(a, b) return a.Family == b.Family and a.Weight == b.Weight and a.Style == b.Style end

UDim2 = {}
UDim2.__index = UDim2
UDim2.__type = "UDim2"

function UDim2.new(xScale, xOffset, yScale, yOffset)
    local t = {
        X = UDim.new(xScale or 0, xOffset or 0),
        Y = UDim.new(yScale or 0, yOffset or 0),
    }
    t.Width  = t.X
    t.Height = t.Y
    return setmetatable(t, UDim2)
end

function UDim2.fromScale(xScale, yScale)
    return UDim2.new(xScale, 0, yScale, 0)
end

function UDim2.fromOffset(xOffset, yOffset)
    return UDim2.new(0, xOffset, 0, yOffset)
end

function UDim2.__add(a, b)
    return UDim2.new(a.X.Scale + b.X.Scale, a.X.Offset + b.X.Offset,
                     a.Y.Scale + b.Y.Scale, a.Y.Offset + b.Y.Offset)
end

function UDim2.__sub(a, b)
    return UDim2.new(a.X.Scale - b.X.Scale, a.X.Offset - b.X.Offset,
                     a.Y.Scale - b.Y.Scale, a.Y.Offset - b.Y.Offset)
end

function UDim2.__unm(a)
    return UDim2.new(-a.X.Scale, -a.X.Offset, -a.Y.Scale, -a.Y.Offset)
end

-- Sin __eq, dos UDim2 iguales daban false (se comparaban las tablas) y
-- cualquier `if frame.Size == objetivo then` no entraba nunca.
function UDim2.__eq(a, b)
    return a.X.Scale == b.X.Scale and a.X.Offset == b.X.Offset
       and a.Y.Scale == b.Y.Scale and a.Y.Offset == b.Y.Offset
end

function UDim2:lerp(b, t)
    return UDim2.new(
        self.X.Scale  + (b.X.Scale  - self.X.Scale)  * t,
        self.X.Offset + (b.X.Offset - self.X.Offset) * t,
        self.Y.Scale  + (b.Y.Scale  - self.Y.Scale)  * t,
        self.Y.Offset + (b.Y.Offset - self.Y.Offset) * t
    )
end
UDim2.Lerp = UDim2.lerp   -- el nombre de Roblox

function UDim2.__tostring(u)
    return string.format("{%g,%g},{%g,%g}", u.X.Scale, u.X.Offset, u.Y.Scale, u.Y.Offset)
end

-- ══════════════════════════════════════════════════════════════════════
--  CFrame — Roblox-style 3D transformation system
--  Stores position + 3x3 rotation matrix (same as Roblox)
--  CFrame.new(x,y,z)                     — position, no rotation
--  CFrame.new(pos, lookAt)               — look toward a Vector3
--  CFrame.new(x,y,z, r00,r01,r02,...)   — position + 9 rotation components
--  CFrame.fromEulerAnglesXYZ(rx,ry,rz)  — from Euler angles
--  CFrame.Angles(rx,ry,rz)              — alias for fromEulerAnglesXYZ
--  CFrame.lookAt(from, to)              — equivalent to CFrame.new(from, to)
--  cf * cf2                             — multiply (compose transforms)
--  cf + v3                              — translate
--  cf:Inverse()                         — inverse
--  cf:Lerp(cf2, t)                      — interpolation
--  cf:ToEulerAnglesXYZ()                — extract Euler angles
--  cf.Position / cf.X / cf.Y / cf.Z    — position
--  cf.LookVector / cf.RightVector / cf.UpVector — direction vectors
-- ////
--  CFrame — sistema de transformación 3D estilo Roblox
--  Almacena posición + matriz de rotación 3x3 (igual que Roblox)
--  CFrame.new(x,y,z)                     — posición, sin rotación
--  CFrame.new(pos, lookAt)               — apuntar hacia un Vector3
--  CFrame.new(x,y,z, r00,r01,r02,...)   — posición + 9 componentes de rotación
--  CFrame.fromEulerAnglesXYZ(rx,ry,rz)  — desde ángulos de Euler
--  CFrame.Angles(rx,ry,rz)              — alias de fromEulerAnglesXYZ
--  CFrame.lookAt(from, to)              — equivalente a CFrame.new(from, to)
--  cf * cf2                             — multiplicar (componer transformaciones)
--  cf + v3                              — trasladar
--  cf:Inverse()                         — inversa
--  cf:Lerp(cf2, t)                      — interpolación
--  cf:ToEulerAnglesXYZ()                — extraer ángulos de Euler
--  cf.Position / cf.X / cf.Y / cf.Z    — posición
--  cf.LookVector / cf.RightVector / cf.UpVector — vectores de dirección
-- ══════════════════════════════════════════════════════════════════════
)LUAU"
R"LUAU(
CFrame = {}
CFrame.__type = "CFrame"

local _cf_raw   -- adelantada: los accesores de abajo la usan

-- Position / LookVector / RightVector / UpVector se calculan AL LEERLOS y son
-- Vector3 de verdad. Antes se guardaban dentro del CFrame como tablas sueltas
-- SIN la metatabla de Vector3, y eso rompia dos cosas a la vez:
--   1. (cf.Position - otro).Magnitude  ->  Vector3.__sub recibia una tabla,
--      devolvia nil y el script moria con "index nil with 'Magnitude'".
--   2. el puente C++ reescribe m00..m22 encima de un CFrame ya creado
--      (GetPivot, nodo.CFrame), pero esas tablas se quedaban con los valores
--      viejos: LookVector devolvia el de la identidad, no el real.
-- Calculandolos al vuelo desaparecen los dos problemas.
local _cf_get = {}
_cf_get.Position    = function(cf) return Vector3.new(cf.X, cf.Y, cf.Z) end
_cf_get.RightVector = function(cf) return Vector3.new(cf.m00, cf.m10, cf.m20) end
_cf_get.UpVector    = function(cf) return Vector3.new(cf.m01, cf.m11, cf.m21) end
_cf_get.LookVector  = function(cf) return Vector3.new(-cf.m02, -cf.m12, -cf.m22) end
_cf_get.XVector     = _cf_get.RightVector
_cf_get.YVector     = _cf_get.UpVector
_cf_get.ZVector     = function(cf) return Vector3.new(cf.m02, cf.m12, cf.m22) end
_cf_get.p           = _cf_get.Position
_cf_get.lookVector  = _cf_get.LookVector
_cf_get.Rotation    = function(cf)
    return _cf_raw(0,0,0, cf.m00,cf.m01,cf.m02, cf.m10,cf.m11,cf.m12, cf.m20,cf.m21,cf.m22)
end

CFrame.__index = function(cf, k)
    local g = _cf_get[k]
    if g then return g(cf) end
    return rawget(CFrame, k)
end

_cf_raw = function(x,y,z, m00,m01,m02, m10,m11,m12, m20,m21,m22)
    return setmetatable({
        X=x,Y=y,Z=z,
        m00=m00,m01=m01,m02=m02,
        m10=m10,m11=m11,m12=m12,
        m20=m20,m21=m21,m22=m22,
    }, CFrame)
end

-- Cuaternion (x,y,z,w) -> CFrame. Se normaliza, como Roblox.
local function _cf_quat(px,py,pz, x,y,z,w)
    local n = math.sqrt(x*x + y*y + z*z + w*w)
    if n < 1e-12 then return _cf_raw(px,py,pz, 1,0,0, 0,1,0, 0,0,1) end
    x, y, z, w = x/n, y/n, z/n, w/n
    return _cf_raw(px,py,pz,
        1 - 2*(y*y + z*z), 2*(x*y - z*w),     2*(x*z + y*w),
        2*(x*y + z*w),     1 - 2*(x*x + z*z), 2*(y*z - x*w),
        2*(x*z - y*w),     2*(y*z + x*w),     1 - 2*(x*x + y*y))
end

-- La rotacion de un CFrame como cuaternion (x,y,z,w), con w >= 0.
local function _cf_a_quat(cf)
    local m00,m01,m02 = cf.m00,cf.m01,cf.m02
    local m10,m11,m12 = cf.m10,cf.m11,cf.m12
    local m20,m21,m22 = cf.m20,cf.m21,cf.m22
    local tr = m00 + m11 + m22
    local x, y, z, w
    if tr > 0 then
        local s = math.sqrt(tr + 1) * 2
        w = 0.25 * s; x = (m21 - m12) / s; y = (m02 - m20) / s; z = (m10 - m01) / s
    elseif m00 > m11 and m00 > m22 then
        local s = math.sqrt(1 + m00 - m11 - m22) * 2
        w = (m21 - m12) / s; x = 0.25 * s; y = (m01 + m10) / s; z = (m02 + m20) / s
    elseif m11 > m22 then
        local s = math.sqrt(1 + m11 - m00 - m22) * 2
        w = (m02 - m20) / s; x = (m01 + m10) / s; y = 0.25 * s; z = (m12 + m21) / s
    else
        local s = math.sqrt(1 + m22 - m00 - m11) * 2
        w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = 0.25 * s
    end
    if w < 0 then x, y, z, w = -x, -y, -z, -w end
    return x, y, z, w
end

function CFrame.new(ax, ay, az, bx, by, bz, m01, m02, m10, m11, m12, m20, m21, m22)
    if ax == nil then
        return _cf_raw(0,0,0, 1,0,0, 0,1,0, 0,0,1)
    end
    -- CFrame.new(Vector3) — UN solo Vector3 de posicion. OJO: nuestro Vector3
    -- es USERDATA (no tabla); antes caia al caso de numeros y daba (0,0,0).
    if ay == nil and type(ax) ~= "number" then
        local px = ax.X or ax.x or 0
        local py = ax.Y or ax.y or 0
        local pz = ax.Z or ax.z or 0
        return _cf_raw(px,py,pz, 1,0,0, 0,1,0, 0,0,1)
    end
    -- CFrame.new(Vector3_pos, Vector3_lookAt [, Vector3_up]) — tabla O userdata
    --
    -- El producto vectorial iba AL REVES (right = up x forward en vez de
    -- forward x up), asi que la base salia ZURDA: determinante -1. Godot lo lee
    -- como una ESCALA NEGATIVA y Jolt se planta —
    --   "A scale of (-1.478, -2.550, -2.409) is not supported ...
    --    The scale will instead be treated as (-2.146, -2.146, -2.146)"
    -- — y monta el collider con otro tamaño que el muñeco. Ese era el
    -- personaje "hundiendose en el piso" al caminar: la capsula de fisica no
    -- coincidia con lo que se veia. Ademas, todo lo colocado con CFrame.lookAt
    -- salia con la izquierda y la derecha cambiadas.
    if type(ax) ~= "number" and ay ~= nil and type(ay) ~= "number" then
        local px,py,pz = ax.X or ax.x or 0, ax.Y or ax.y or 0, ax.Z or ax.z or 0
        local lx,ly,lz = ay.X or ay.x or 0, ay.Y or ay.y or 0, ay.Z or ay.z or 0
        local fx,fy,fz = lx-px, ly-py, lz-pz
        local ln = math.sqrt(fx*fx+fy*fy+fz*fz)
        if ln < 1e-6 then return _cf_raw(px,py,pz, 1,0,0, 0,1,0, 0,0,1) end
        fx,fy,fz = fx/ln, fy/ln, fz/ln
        -- tercer argumento opcional: el "arriba" de referencia (por defecto +Y)
        local upx,upy,upz = 0,1,0
        if az ~= nil and type(az) ~= "number" then
            upx,upy,upz = az.X or az.x or 0, az.Y or az.y or 0, az.Z or az.z or 0
        end
        -- right = forward x up
        local rx = fy*upz - fz*upy
        local ry = fz*upx - fx*upz
        local rz = fx*upy - fy*upx
        local rn = math.sqrt(rx*rx+ry*ry+rz*rz)
        if rn < 1e-6 then rx,ry,rz = 1,0,0 else rx,ry,rz = rx/rn, ry/rn, rz/rn end
        -- up real = right x forward  (asi X x Y = -forward = columna Z: base DIESTRA)
        local ux = ry*fz - rz*fy
        local uy = rz*fx - rx*fz
        local uz = rx*fy - ry*fx
        return _cf_raw(px,py,pz, rx,ux,-fx, ry,uy,-fy, rz,uz,-fz)
    end
    -- CFrame.new(x,y,z) — position only / posición sola
    if bx == nil then
        ax = ax or 0; ay = ay or 0; az = az or 0
        return _cf_raw(ax,ay,az, 1,0,0, 0,1,0, 0,0,1)
    end
    -- CFrame.new(x,y,z, qX,qY,qZ,qW) — posicion + cuaternion. Con 7 numeros
    -- caia en la forma de 12 y la rotacion salia con huecos nil.
    if m02 == nil then
        return _cf_quat(ax,ay,az, bx,by,bz, m01 or 1)
    end
    -- CFrame.new(x,y,z, r00,r01,r02, r10,r11,r12, r20,r21,r22)
    return _cf_raw(ax,ay,az, bx,by,bz, m01,m02,m10, m11,m12,m20)
end

function CFrame.fromEulerAnglesXYZ(rx, ry, rz)
    local cx,sx = math.cos(rx), math.sin(rx)
    local cy,sy = math.cos(ry), math.sin(ry)
    local cz,sz = math.cos(rz), math.sin(rz)
    return _cf_raw(0,0,0,
        cy*cz,            -cy*sz,           sy,
        sx*sy*cz+cx*sz,   -sx*sy*sz+cx*cz,  -sx*cy,
        -cx*sy*cz+sx*sz,  cx*sy*sz+sx*cz,   cx*cy)
end
CFrame.Angles = CFrame.fromEulerAnglesXYZ
-- R = Ry * Rx * Rz (se aplica Z, luego X, luego Y). Los argumentos van en el
-- orden de Roblox (rx, ry, rz); antes se leian como (ry, rx, rz) y el giro
-- salia con X e Y cambiados.
CFrame.fromEulerAnglesYXZ = function(rx,ry,rz)
    local cx,sx = math.cos(rx), math.sin(rx)
    local cy,sy = math.cos(ry), math.sin(ry)
    local cz,sz = math.cos(rz), math.sin(rz)
    return _cf_raw(0,0,0,
        cy*cz+sy*sx*sz,   cz*sy*sx-cy*sz,   cx*sy,
        cx*sz,            cx*cz,             -sx,
        cy*sx*sz-cz*sy,   sy*sz+cy*cz*sx,   cy*cx)
end
CFrame.fromOrientation = CFrame.fromEulerAnglesYXZ

function CFrame.fromAxisAngle(axis, angle)
    local ax, ay, az = axis.X or axis.x or 0, axis.Y or axis.y or 0, axis.Z or axis.z or 0
    local n = math.sqrt(ax*ax + ay*ay + az*az)
    if n < 1e-12 then return _cf_raw(0,0,0, 1,0,0, 0,1,0, 0,0,1) end
    local s = math.sin(angle / 2) / n
    return _cf_quat(0,0,0, ax*s, ay*s, az*s, math.cos(angle / 2))
end

-- CFrame.fromMatrix(pos, vX, vY [, vZ]): las columnas de la rotacion. Sin vZ
-- se calcula como vX x vY (base diestra).
function CFrame.fromMatrix(pos, vX, vY, vZ)
    local x0, x1, x2 = vX.X, vX.Y, vX.Z
    local y0, y1, y2 = vY.X, vY.Y, vY.Z
    local z0, z1, z2
    if vZ then
        z0, z1, z2 = vZ.X, vZ.Y, vZ.Z
    else
        z0, z1, z2 = x1*y2 - x2*y1, x2*y0 - x0*y2, x0*y1 - x1*y0
        local n = math.sqrt(z0*z0 + z1*z1 + z2*z2)
        if n > 1e-12 then z0, z1, z2 = z0/n, z1/n, z2/n end
    end
    return _cf_raw(pos.X, pos.Y, pos.Z, x0,y0,z0, x1,y1,z1, x2,y2,z2)
end

function CFrame.lookAt(from, to, up)
    return CFrame.new(from, to, up)   -- el "arriba" se ignoraba
end

function CFrame.__mul(a, b)
    if type(b) == "table" and b.m00 ~= nil then
        -- CFrame * CFrame
        local x = a.X + a.m00*b.X + a.m01*b.Y + a.m02*b.Z
        local y = a.Y + a.m10*b.X + a.m11*b.Y + a.m12*b.Z
        local z = a.Z + a.m20*b.X + a.m21*b.Y + a.m22*b.Z
        return _cf_raw(x,y,z,
            a.m00*b.m00+a.m01*b.m10+a.m02*b.m20,
            a.m00*b.m01+a.m01*b.m11+a.m02*b.m21,
            a.m00*b.m02+a.m01*b.m12+a.m02*b.m22,
            a.m10*b.m00+a.m11*b.m10+a.m12*b.m20,
            a.m10*b.m01+a.m11*b.m11+a.m12*b.m21,
            a.m10*b.m02+a.m11*b.m12+a.m12*b.m22,
            a.m20*b.m00+a.m21*b.m10+a.m22*b.m20,
            a.m20*b.m01+a.m21*b.m11+a.m22*b.m21,
            a.m20*b.m02+a.m21*b.m12+a.m22*b.m22)
    end
    -- CFrame * Vector3
    local vx = b.X or b.x or 0
    local vy = b.Y or b.y or 0
    local vz = b.Z or b.z or 0
    local rx = a.X + a.m00*vx + a.m01*vy + a.m02*vz
    local ry = a.Y + a.m10*vx + a.m11*vy + a.m12*vz
    local rz = a.Z + a.m20*vx + a.m21*vy + a.m22*vz
    return Vector3.new(rx, ry, rz)
end

function CFrame.__add(a, b)
    local bx = b.X or b.x or 0
    local by = b.Y or b.y or 0
    local bz = b.Z or b.z or 0
    return _cf_raw(a.X+bx, a.Y+by, a.Z+bz,
        a.m00,a.m01,a.m02, a.m10,a.m11,a.m12, a.m20,a.m21,a.m22)
end

function CFrame.__sub(a, b)
    local bx = b.X or b.x or 0
    local by = b.Y or b.y or 0
    local bz = b.Z or b.z or 0
    return _cf_raw(a.X-bx, a.Y-by, a.Z-bz,
        a.m00,a.m01,a.m02, a.m10,a.m11,a.m12, a.m20,a.m21,a.m22)
end

-- Las 12 componentes: comparando solo la diagonal, dos giros distintos con la
-- misma diagonal (90 y -90 grados en Y) salian iguales.
function CFrame.__eq(a, b)
    return a.X==b.X and a.Y==b.Y and a.Z==b.Z
       and a.m00==b.m00 and a.m01==b.m01 and a.m02==b.m02
       and a.m10==b.m10 and a.m11==b.m11 and a.m12==b.m12
       and a.m20==b.m20 and a.m21==b.m21 and a.m22==b.m22
end

function CFrame:Inverse()
    -- Transponer la parte de rotación (es ortogonal)
    local ix = -(self.m00*self.X + self.m10*self.Y + self.m20*self.Z)
    local iy = -(self.m01*self.X + self.m11*self.Y + self.m21*self.Z)
    local iz = -(self.m02*self.X + self.m12*self.Y + self.m22*self.Z)
    return _cf_raw(ix,iy,iz,
        self.m00,self.m10,self.m20,
        self.m01,self.m11,self.m21,
        self.m02,self.m12,self.m22)
end

-- La rotacion va por slerp de cuaterniones, como Roblox. Mezclar la matriz
-- componente a componente la encogia a mitad de camino (a 180 grados, con
-- t = 0.5, la base salia casi nula y la pieza se aplastaba).
function CFrame:Lerp(cf2, t)
    local x = self.X + (cf2.X - self.X) * t
    local y = self.Y + (cf2.Y - self.Y) * t
    local z = self.Z + (cf2.Z - self.Z) * t
    local ax, ay, az, aw = _cf_a_quat(self)
    local bx, by, bz, bw = _cf_a_quat(cf2)
    local d = ax*bx + ay*by + az*bz + aw*bw
    if d < 0 then bx, by, bz, bw, d = -bx, -by, -bz, -bw, -d end
    local s0, s1
    if d > 0.9995 then
        s0, s1 = 1 - t, t
    else
        local th = math.acos(d)
        local sn = math.sin(th)
        s0 = math.sin((1 - t) * th) / sn
        s1 = math.sin(t * th) / sn
    end
    return _cf_quat(x, y, z, s0*ax + s1*bx, s0*ay + s1*by, s0*az + s1*bz, s0*aw + s1*bw)
end

function CFrame:ToEulerAnglesXYZ()
    local sy = self.m02
    sy = math.max(-1, math.min(1, sy))
    local ry = math.asin(sy)
    local rx, rz
    if math.abs(sy) < 0.9999 then
        rx = math.atan2(-self.m12, self.m22)
        rz = math.atan2(-self.m01, self.m00)
    else
        rx = math.atan2(self.m21, self.m11)
        rz = 0
    end
    return rx, ry, rz
end

-- Inversa de fromEulerAnglesYXZ (R = Ry * Rx * Rz). ToOrientation es esta y no
-- la XYZ: con la XYZ, CFrame.fromOrientation(cf:ToOrientation()) no devolvia
-- el mismo giro.
function CFrame:ToEulerAnglesYXZ()
    local sx = -self.m12
    sx = math.max(-1, math.min(1, sx))
    local rx = math.asin(sx)
    local ry, rz
    if math.abs(sx) < 0.9999 then
        ry = math.atan2(self.m02, self.m22)
        rz = math.atan2(self.m10, self.m11)
    else
        ry = math.atan2(-self.m20, self.m00)
        rz = 0
    end
    return rx, ry, rz
end
CFrame.ToOrientation = CFrame.ToEulerAnglesYXZ

function CFrame:ToAxisAngle()
    local x, y, z, w = _cf_a_quat(self)
    local s = math.sqrt(x*x + y*y + z*z)
    if s < 1e-12 then return Vector3.new(1, 0, 0), 0 end
    return Vector3.new(x/s, y/s, z/s), 2 * math.atan2(s, w)
end

-- Rehace la base ortonormal (los giros acumulados la van deformando).
function CFrame:Orthonormalize()
    local x, y, z, w = _cf_a_quat(self)
    return _cf_quat(self.X, self.Y, self.Z, x, y, z, w)
end

function CFrame:FuzzyEq(cf2, eps)
    eps = eps or 1e-5
    local a = { self:GetComponents() }
    local b = { cf2:GetComponents() }
    for i = 1, 12 do
        if math.abs(a[i] - b[i]) > eps then return false end
    end
    return true
end

function CFrame:GetComponents()
    return self.X,self.Y,self.Z,
           self.m00,self.m01,self.m02,
           self.m10,self.m11,self.m12,
           self.m20,self.m21,self.m22
end

function CFrame:ToWorldSpace(cf)
    return self * cf
end

function CFrame:ToObjectSpace(cf)
    return self:Inverse() * cf
end

function CFrame:PointToWorldSpace(v)
    return self * v
end

function CFrame:PointToObjectSpace(v)
    return self:Inverse() * v
end

function CFrame:VectorToWorldSpace(v)
    local vx = v.X or v.x or 0
    local vy = v.Y or v.y or 0
    local vz = v.Z or v.z or 0
    local rx = self.m00*vx + self.m01*vy + self.m02*vz
    local ry = self.m10*vx + self.m11*vy + self.m12*vz
    local rz = self.m20*vx + self.m21*vy + self.m22*vz
    return Vector3.new(rx, ry, rz)
end

function CFrame:VectorToObjectSpace(v)
    return self:Inverse():VectorToWorldSpace(v)
end

function CFrame.__tostring(cf)
    return string.format("%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f",
        cf.X,cf.Y,cf.Z, cf.m00,cf.m01,cf.m02, cf.m10,cf.m11,cf.m12, cf.m20,cf.m21,cf.m22)
end

CFrame.identity = _cf_raw(0,0,0, 1,0,0, 0,1,0, 0,0,1)

)LUAU"
// Third part of the stdlib / Tercera parte del stdlib
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  Instance.new — create instances at runtime
--  Saves the C++ version (which has the Part→RobloxPart mapping, etc.)
--  and uses it as first option; _InstanceNew as generic fallback.
-- ////
--  Instance.new — crear instancias en runtime
--  Guarda la versión C++ (que tiene el mapeo Part→RobloxPart etc.)
--  y la usa como primera opción; _InstanceNew como fallback genérico.
-- ══════════════════════════════════════════════════════════════════════
local _cpp_new = Instance and Instance.new or nil

-- Mapeo de nombres Roblox → nombres de clase Godot registrados
local _cls_map = {
    Part="Part", BasePart="Part", MeshPart="Part",
    WedgePart="Part", CornerWedgePart="Part", TrussPart="Part",
    UnionOperation="Part",
    -- SpecialMesh modifica la pieza en la que se mete (antes creaba OTRA Part).
    SpecialMesh="RBXMesh",
    -- Model = RBXModel, la clase con GetPivot/PivotTo/GetBoundingBox. Con un
    -- Node3D pelado, un Model creado por codigo no tenia ninguno de esos.
    Folder="Folder", Model="RBXModel", Configuration="Folder",
    Humanoid="Humanoid", Humanoid2D="Humanoid2D",
    PointLight="PointLight", SpotLight="SpotLight", SurfaceLight="SurfaceLight",
    DirectionalLight="DirectionalLight3D",
    RemoteEvent="RemoteEventNode", UnreliableRemoteEvent="UnreliableRemoteEventNode",
    RemoteFunction="RemoteFunctionNode",
    BindableEvent="BindableEventNode", BindableFunction="BindableEventNode",
    Script="ServerScript", LocalScript="LocalScript", ModuleScript="ModuleScript",
    -- Objetos de valor: nodo REAL RobloxValue con .Value/.Changed
    StringValue="StringValue", IntValue="IntValue", NumberValue="NumberValue",
    BoolValue="BoolValue", ObjectValue="ObjectValue", Vector3Value="Vector3Value",
    Color3Value="Color3Value", CFrameValue="CFrameValue",
    -- UI tipo Roblox
    ScreenGui="ScreenGui", Frame="Frame", TextLabel="TextLabel",
    TextButton="TextButton", TextBox="TextBox",
    -- ImageButton mapeaba a ImageLabel de cuando no existia la clase real:
    -- el boton creado por script salia sin Activated/MouseButton1Click.
    ImageLabel="ImageLabel", ImageButton="ImageButton",
    ScrollingFrame="ScrollingFrame", LocalizationTable="Folder",
    -- BodyMovers
    BodyVelocity="BodyVelocity", BodyPosition="BodyPosition",
    BodyForce="BodyForce", BodyAngularVelocity="BodyAngularVelocity",
    BodyGyro="BodyGyro",
    -- Constraints
    WeldConstraint="WeldConstraint", HingeConstraint="HingeConstraint",
    ManualWeld="Weld", Snap="Weld",
    BallSocketConstraint="BallSocketConstraint", RodConstraint="RodConstraint",
    SpringConstraint="SpringConstraint", Attachment="Attachment",
    -- Clases nuevas cuyo nombre choca con Godot / reutilizan nodos existentes
    -- Decal y Texture: las mismas clases que el importador (las que dibujan).
    Decal="RBXDecal", Texture="RBXTexture", CanvasGroup="RobloxCanvasGroup",
    Sky="LightingSkyNode", Atmosphere="AtmosphereNode", SunRays="SunRaysNode",
    -- Sound
    Sound="Sound", SoundGroup="SoundGroup",
    -- Tween
    Tween="RobloxTween",
    -- Interactive
    ClickDetector="ClickDetector", ProximityPrompt="ProximityPrompt",
    SpawnLocation="SpawnLocation",
    -- Billboard / Surface GUI
    BillboardGui="BillboardGui", SurfaceGui="SurfaceGui",
    -- Motor6D / Tool / Backpack
    Motor6D="Motor6D", Tool="Tool", Backpack="Backpack",
    -- Animation
    Animation="AnimationObject", AnimationTrack="AnimationTrack",
    -- Animator: nodo pelado que se llama "Animator"; el puente reenvia sus
    -- metodos (LoadAnimation, GetPlayingAnimationTracks...) al Humanoid padre.
    -- Sin esto Instance.new("Animator") devolvia nil y cualquier NPC con
    -- animaciones moria en la linea siguiente.
    Animator="Folder",
    -- (Camera y WorldModel ya no van aqui: son clases de verdad del C++, las
    -- del ViewportFrame, y Instance.new las crea antes de mirar esta tabla.)
    -- Value objects: nodo REAL RobloxValue con .Value/.Changed
    NumberValue="NumberValue", StringValue="StringValue", IntValue="IntValue",
    BoolValue="BoolValue", ObjectValue="ObjectValue", Vector3Value="Vector3Value",
    Color3Value="Color3Value", CFrameValue="CFrameValue",
}

Instance = Instance or {}
Instance.new = function(className, parent)
    local inst
    -- 1. Intentar con la función C++ original (mapeos hardcodeados)
    if _cpp_new then
        local ok, result = pcall(_cpp_new, className)
        if ok and result ~= nil then inst = result end
    end
    -- 2. Fallback: _InstanceNew con mapeo de nombres
    if inst == nil and _InstanceNew then
        local godotName = _cls_map[className] or className
        inst = _InstanceNew(godotName)
    end
    if inst == nil then
        warn("[Instance.new] Clase no encontrada: '" .. tostring(className) .. "'")
        return nil
    end
    -- Nombrar la instancia con el className de Roblox
    pcall(function() inst.Name = className end)
    -- Aplicar parent si se especificó
    if parent ~= nil then
        pcall(function() inst.Parent = parent end)
    end
    return inst
end

-- Instance.fromExisting: envuelve un nodo Godot existente (uso interno)
Instance.fromExisting = function(node)
    return node  -- los nodos ya vienen envueltos como GodotObject
end

)LUAU"
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  Value Objects — NumberValue, StringValue, BoolValue, etc.
--  Ahora son NODOS REALES (clase C++ RobloxValue): Instance.new los crea por
--  el mapeo C++/_cls_map, así Parent/FindFirstChild/leaderstats funcionan y
--  .Value / .Changed andan como en Roblox. (Antes eran tablas Lua que, al
--  hacer obj.Parent = nodoReal, NO se añadían al árbol → leaderstats vacío.)
-- ══════════════════════════════════════════════════════════════════════

-- ══════════════════════════════════════════════════════════════════════
--  Color3 extras — missing methods
-- ////
--  Color3 extras — métodos faltantes
-- ══════════════════════════════════════════════════════════════════════
if Color3 then
    Color3.fromHSV = Color3.fromHSV or function(h, s, v)
        h = h % 1
        if s == 0 then return Color3.new(v, v, v) end
        local i = math.floor(h * 6)
        local f = h * 6 - i
        local p, q, t2 = v*(1-s), v*(1-s*f), v*(1-s*(1-f))
        local r, g, b
        i = i % 6
        if i == 0 then r,g,b=v,t2,p elseif i == 1 then r,g,b=q,v,p
        elseif i == 2 then r,g,b=p,v,t2 elseif i == 3 then r,g,b=p,q,v
        elseif i == 4 then r,g,b=t2,p,v else r,g,b=v,p,q end
        return Color3.new(r, g, b)
    end
    Color3.toHSV = Color3.toHSV or function(c)
        local r,g,b = c.R or c.r or 0, c.G or c.g or 0, c.B or c.b or 0
        local max = math.max(r,g,b); local min = math.min(r,g,b); local delta = max - min
        local h = 0
        if delta > 0 then
            if max == r then h = (g-b)/delta % 6
            elseif max == g then h = (b-r)/delta + 2
            else h = (r-g)/delta + 4 end
            h = h / 6
        end
        return h, (max==0 and 0 or delta/max), max
    end
    Color3.lerp = Color3.lerp or function(c1, c2, t)
        local r1,g1,b1 = c1.R or c1.r or 0, c1.G or c1.g or 0, c1.B or c1.b or 0
        local r2,g2,b2 = c2.R or c2.r or 0, c2.G or c2.g or 0, c2.B or c2.b or 0
        return Color3.new(r1+(r2-r1)*t, g1+(g2-g1)*t, b1+(b2-b1)*t)
    end
    Color3.fromHex = Color3.fromHex or function(hex)
        hex = tostring(hex):gsub("^#", "")
        local r = tonumber(hex:sub(1,2), 16) or 0
        local g = tonumber(hex:sub(3,4), 16) or 0
        local b = tonumber(hex:sub(5,6), 16) or 0
        return Color3.new(r/255, g/255, b/255)
    end
    Color3.toHex = Color3.toHex or function(c)
        local r = math.max(0,math.min(255, math.floor((c.R or c.r or 0)*255+0.5)))
        local g = math.max(0,math.min(255, math.floor((c.G or c.g or 0)*255+0.5)))
        local b = math.max(0,math.min(255, math.floor((c.B or c.b or 0)*255+0.5)))
        return string.format("%02X%02X%02X", r, g, b)
    end
end

)LUAU"
// Fourth part of the stdlib / Cuarta parte del stdlib
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  Vector3 extras — Magnitude, Unit, Cross, Dot, Lerp as methods
-- ////
--  Vector3 extras — Magnitude, Unit, Cross, Dot, Lerp como métodos
-- ══════════════════════════════════════════════════════════════════════
if Vector3 then
    Vector3.zero  = Vector3.zero  or Vector3.new(0,0,0)
    Vector3.one   = Vector3.one   or Vector3.new(1,1,1)
    Vector3.xAxis = Vector3.xAxis or Vector3.new(1,0,0)
    Vector3.yAxis = Vector3.yAxis or Vector3.new(0,1,0)
    Vector3.zAxis = Vector3.zAxis or Vector3.new(0,0,1)
end

-- ══════════════════════════════════════════════════════════════════════
--  Random — equivalent to Roblox Random
-- ////
--  Random — equivalente a Roblox Random
-- ══════════════════════════════════════════════════════════════════════
Random = {}
Random.__index = Random
function Random.new(seed)
    local state = { _seed = seed or os.clock() }
    local mt = {}
    mt.__index = {
        NextNumber = function(self, min, max)
            self._seed = (self._seed * 1664525 + 1013904223) % (2^32)
            local r = (self._seed % 10000) / 10000
            if min and max then return min + r * (max - min) end
            return r
        end,
        NextInteger = function(self, min, max)
            return math.floor(self:NextNumber(min, max + 1))
        end,
        NextUnitVector = function(self)
            local x = self:NextNumber(-1,1)
            local y = self:NextNumber(-1,1)
            local z = self:NextNumber(-1,1)
            local len = math.sqrt(x*x+y*y+z*z)
            if len < 0.001 then return Vector3.new(0,1,0) end
            return Vector3.new(x/len, y/len, z/len)
        end,
    }
    return setmetatable(state, mt)
end

-- ══════════════════════════════════════════════════════════════════════
--  string extras — métodos adicionales compatibles con Roblox
-- ══════════════════════════════════════════════════════════════════════
if not string.split then
    function string.split(s, sep)
        local result, pattern = {}, "([^" .. (sep or ",") .. "]+)"
        for part in string.gmatch(s, pattern) do table.insert(result, part) end
        return result
    end
end

if not string.trim then
    function string.trim(s) return s:match("^%s*(.-)%s*$") end
end

-- ══════════════════════════════════════════════════════════════════════
--  table extras
-- ══════════════════════════════════════════════════════════════════════
table.find  = table.find  or function(t, v) for i, x in ipairs(t) do if x == v then return i end end end
table.clear = table.clear or function(t) for k in next, t do t[k] = nil end end
table.clone = table.clone or function(t) local c = {} for k, v in pairs(t) do c[k] = v end return c end
table.freeze = table.freeze or function(t)
    local mt = getmetatable(t) or {}
    mt.__newindex = function() error("attempt to modify a frozen table", 2) end
    mt.__frozen = true
    setmetatable(t, mt)
    return t
end
table.isfrozen = table.isfrozen or function(t)
    local mt = getmetatable(t)
    return mt ~= nil and mt.__frozen == true
end

-- ══════════════════════════════════════════════════════════════════════
--  math extras
-- ══════════════════════════════════════════════════════════════════════
math.clamp = math.clamp or function(v, mn, mx) return v < mn and mn or v > mx and mx or v end
math.lerp  = math.lerp  or function(a, b, t) return a + (b - a) * t end
math.sign  = math.sign  or function(x) return x > 0 and 1 or x < 0 and -1 or 0 end
math.round = math.round or function(x) return math.floor(x + 0.5) end
math.map   = math.map   or function(v, inMin, inMax, outMin, outMax)
    return outMin + (v - inMin) / (inMax - inMin) * (outMax - outMin)
end
math.noise = math.noise or (function()
    local p={151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,8,99,37,240,
        21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,57,177,33,88,237,
        149,56,87,174,20,125,136,171,168,68,175,74,165,71,134,139,48,27,166,77,146,158,231,83,111,
        229,122,60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,
        209,76,132,187,208,89,18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,
        217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,189,
        28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,129,22,39,
        253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,228,251,34,242,193,238,210,144,
        12,191,179,162,241,81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,184,84,204,
        176,115,121,50,45,127,4,150,254,138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180}
    local function perm(i) return p[(i % 256) + 1] end
    local function fade(t) return t*t*t*(t*(t*6-15)+10) end
    local function lerp2(t,a,b) return a+t*(b-a) end
    local function grad(h,x,y,z)
        h=h%16; local u=h<8 and x or y; local v=h<4 and y or (h==12 or h==14) and x or z
        return ((h%2==0) and u or -u)+((math.floor(h/2)%2==0) and v or -v)
    end
    return function(x,y,z)
        y=y or 0; z=z or 0
        local xi=math.floor(x)%256; local yi=math.floor(y)%256; local zi=math.floor(z)%256
        x=x-math.floor(x); y=y-math.floor(y); z=z-math.floor(z)
        local u=fade(x); local v=fade(y); local w=fade(z)
        local A=perm(xi)+yi; local AA=perm(A)+zi; local AB=perm(A+1)+zi
        local B=perm(xi+1)+yi; local BA=perm(B)+zi; local BB=perm(B+1)+zi
        return lerp2(w,
            lerp2(v,lerp2(u,grad(perm(AA),x,y,z),grad(perm(BA),x-1,y,z)),
                    lerp2(u,grad(perm(AB),x,y-1,z),grad(perm(BB),x-1,y-1,z))),
            lerp2(v,lerp2(u,grad(perm(AA+1),x,y,z-1),grad(perm(BA+1),x-1,y,z-1)),
                    lerp2(u,grad(perm(AB+1),x,y-1,z-1),grad(perm(BB+1),x-1,y-1,z-1))))
    end
end)()

-- ══════════════════════════════════════════════════════════════════════
--  Region3 — 3D region defined by two Vector3 (min/max)
-- ////
--  Region3 — región 3D definida por dos Vector3 (min/max)
-- ══════════════════════════════════════════════════════════════════════
Region3 = {}
Region3.__index = Region3
function Region3.new(minV, maxV)
    minV = minV or Vector3.new(0,0,0)
    maxV = maxV or Vector3.new(0,0,0)
    local t = { Min = minV, Max = maxV }
    t.Size  = Vector3.new(maxV.X-minV.X, maxV.Y-minV.Y, maxV.Z-minV.Z)
    t.CFrame = CFrame.new((minV.X+maxV.X)*0.5, (minV.Y+maxV.Y)*0.5, (minV.Z+maxV.Z)*0.5)
    return setmetatable(t, Region3)
end
function Region3:ExpandToGrid(resolution)
    local function snap(v) return math.floor(v/resolution+0.5)*resolution end
    return Region3.new(
        Vector3.new(snap(self.Min.X),snap(self.Min.Y),snap(self.Min.Z)),
        Vector3.new(snap(self.Max.X),snap(self.Max.Y),snap(self.Max.Z)))
end
function Region3:__tostring()
    return string.format("Region3(%s, %s)", tostring(self.Min), tostring(self.Max))
end

-- ══════════════════════════════════════════════════════════════════════
--  Rect — 2D rectangle with min/max Vector2
-- ////
--  Rect — rectángulo 2D con min/max Vector2
-- ══════════════════════════════════════════════════════════════════════
Rect = {}
Rect.__index = Rect
function Rect.new(a, b, c, d)
    local min, max
    if type(a) == "table" then
        min = a; max = b or Vector2.new(0,0)
    else
        min = Vector2.new(a or 0, b or 0); max = Vector2.new(c or 0, d or 0)
    end
    return setmetatable({ Min=min, Max=max, Width=max.X-min.X, Height=max.Y-min.Y }, Rect)
end
function Rect:__tostring()
    return string.format("Rect(%s, %s)", tostring(self.Min), tostring(self.Max))
end

-- ══════════════════════════════════════════════════════════════════════
--  NumberSequenceKeypoint / NumberSequence
-- ══════════════════════════════════════════════════════════════════════
NumberSequenceKeypoint = {}
NumberSequenceKeypoint.__index = NumberSequenceKeypoint
function NumberSequenceKeypoint.new(t, v, envelope)
    return setmetatable({ Time=t, Value=v, Envelope=envelope or 0 }, NumberSequenceKeypoint)
end

NumberSequence = {}
NumberSequence.__index = NumberSequence
-- NumberSequence.new(n), NumberSequence.new(n0, n1) (de n0 a n1, como Roblox:
-- antes el segundo se ignoraba y el fundido 0 -> 1 salia constante) o una
-- lista de keypoints.
function NumberSequence.new(v, v1)
    if type(v) == "number" then
        local fin = if type(v1) == "number" then v1 else v
        return setmetatable({ Keypoints={NumberSequenceKeypoint.new(0,v), NumberSequenceKeypoint.new(1,fin)} }, NumberSequence)
    end
    return setmetatable({ Keypoints=v }, NumberSequence)
end

-- ══════════════════════════════════════════════════════════════════════
--  ColorSequenceKeypoint / ColorSequence
-- ══════════════════════════════════════════════════════════════════════
ColorSequenceKeypoint = {}
ColorSequenceKeypoint.__index = ColorSequenceKeypoint
-- En Roblox el campo se llama Value (no Color). Los modulos de gradientes
-- que llevan los juegos leen keypoint.Value.R, y con el nombre viejo daba
-- "attempt to index nil with 'R'". Se deja Color como alias.
function ColorSequenceKeypoint.new(t, color, envelope)
    return setmetatable({ Time=t, Value=color, Color=color, Envelope=envelope or 0 },
                        ColorSequenceKeypoint)
end

ColorSequence = {}
ColorSequence.__index = ColorSequence
function ColorSequence.new(v, c1)
    if type(v) == "table" and v[1] and v[1].Time ~= nil then
        return setmetatable({ Keypoints=v }, ColorSequence)
    end
    -- Forma de 2 colores: ColorSequence.new(c0, c1) -> degradado c0->c1 (como
    -- Roblox). Antes el segundo argumento se ignoraba y salían los dos iguales.
    local c = v or Color3.new(1,1,1)
    local last = c1 or c
    return setmetatable({ Keypoints={ColorSequenceKeypoint.new(0,c), ColorSequenceKeypoint.new(1,last)} }, ColorSequence)
end

-- ══════════════════════════════════════════════════════════════════════
--  BrickColor — named colors Roblox-style
-- ////
--  BrickColor — colores con nombre estilo Roblox
-- ══════════════════════════════════════════════════════════════════════
BrickColor = {}
BrickColor.__index = BrickColor
-- Paleta con los nombres y numeros REALES de Roblox. Antes las claves eran
-- "BrightRed" (sin espacio) y Number siempre 0, asi que BrickColor.new("Bright red")
-- -- el nombre que usa Roblox de verdad -- caia al gris por defecto y dos colores
-- distintos comparaban iguales. Es un SUBCONJUNTO (los ~1500 de Roblox no estan):
-- los de equipo y los de uso comun. Un nombre desconocido cae a Medium stone grey
-- como en Roblox. {Nombre, Numero, R, G, B}
local _bc_list = {
    {"White",1,242,243,243}, {"Grey",2,161,165,162}, {"Light yellow",3,249,233,153},
    {"Brick yellow",5,215,197,154}, {"Light green (Mint)",6,194,218,184},
    {"Light reddish violet",9,232,186,200}, {"Pastel Blue",11,128,187,219},
    {"Nougat",18,204,142,105}, {"Bright red",21,196,40,28}, {"Bright blue",23,13,105,172},
    {"Bright yellow",24,245,205,48}, {"Earth orange",25,124,92,70}, {"Black",26,27,42,53},
    {"Dark grey",27,109,110,108}, {"Dark green",28,40,127,71}, {"Medium green",29,161,196,140},
    {"Bright green",37,75,151,75}, {"Dark orange",38,160,95,53}, {"Light bluish violet",39,193,202,222},
    {"Light blue",45,180,210,228}, {"Light red",100,238,196,182}, {"Medium red",101,218,134,122},
    {"Medium blue",102,110,153,202}, {"Light grey",103,199,193,183}, {"Bright violet",104,107,50,124},
    {"Bright orange",106,218,133,65}, {"Bright bluish green",107,0,143,156},
    {"Earth yellow",108,104,92,67}, {"Bright bluish violet",110,67,84,147},
    {"Medium bluish violet",112,104,116,172}, {"Med. yellowish green",115,199,210,60},
    {"Med. bluish green",116,85,165,175}, {"Light bluish green",118,183,215,213},
    {"Br. yellowish green",119,164,189,71}, {"Lig. yellowish green",120,217,228,167},
    {"Light orange",125,234,184,146}, {"Gold",127,220,188,129}, {"Silver",131,156,163,168},
    {"Neon orange",133,213,115,61}, {"Neon green",134,216,221,86}, {"Sand blue",135,116,134,157},
    {"Medium orange",137,224,152,100}, {"Sand yellow",138,149,138,115}, {"Earth blue",140,32,58,86},
    {"Earth green",141,39,70,45}, {"Sand green",151,120,144,130}, {"Sand red",153,149,121,119},
    {"Dark red",154,123,46,47}, {"Reddish brown",192,105,64,40}, {"Medium stone grey",194,163,162,165},
    {"Royal blue",195,70,103,164}, {"Dark stone grey",199,99,95,98}, {"Light stone grey",208,229,228,223},
    {"Brown",217,124,92,70}, {"Bright purple",221,196,112,160}, {"Light purple",222,229,173,200},
    {"Light pink",223,224,178,208}, {"Cool yellow",226,253,234,141}, {"Slime green",301,80,109,84},
    {"Smoky grey",302,91,93,105}, {"Dark blue",303,0,16,176}, {"Parsley green",304,44,101,29},
    {"Sea green",309,52,142,64}, {"Shamrock",310,91,154,76}, {"Forest green",313,31,128,29},
    {"Electric blue",315,9,137,207}, {"Moss",317,124,156,107}, {"Crimson",327,151,0,0},
    {"Mint",328,177,229,166}, {"Baby blue",329,152,194,219}, {"Carnation pink",330,255,152,220},
    {"Persimmon",331,255,89,89}, {"Maroon",332,117,0,0}, {"Gold",333,239,184,56},
    {"Daisy orange",334,248,217,109}, {"Pearl",335,231,231,236}, {"Fog",336,199,212,228},
    {"Salmon",337,255,148,148}, {"Terra Cotta",338,190,104,98}, {"Cocoa",339,86,36,36},
    {"Wheat",340,241,231,199}, {"Buttermilk",341,254,243,187}, {"Mauve",342,224,178,208},
    {"Sunrise",343,212,144,189}, {"Tawny",344,150,85,85}, {"Rust",345,143,76,42},
    {"Cashmere",346,211,190,150}, {"Khaki",347,226,220,188}, {"Lily white",348,237,234,234},
    {"Seashell",349,233,218,218}, {"Burgundy",350,136,62,62}, {"Cork",351,188,155,93},
    {"Burlap",352,199,172,120}, {"Beige",353,202,191,163}, {"Oyster",354,187,179,178},
    {"Pine Cone",355,108,88,75}, {"Fawn brown",356,160,132,79}, {"Hurricane grey",357,149,137,136},
    {"Cloudy grey",358,171,168,158}, {"Linen",359,175,148,131}, {"Copper",360,150,103,102},
    {"Dirt brown",361,86,66,54}, {"Bronze",362,126,104,63}, {"Flint",363,109,110,108},
    {"Dark taupe",364,90,76,66}, {"Burnt Sienna",365,106,57,9},
    {"Institutional white",1001,248,248,248}, {"Mid gray",1002,205,205,205},
    {"Really black",1003,17,17,17}, {"Really red",1004,255,0,0}, {"Deep orange",1005,255,176,0},
    {"Alder",1006,180,128,255}, {"Dusty Rose",1007,163,75,75}, {"Olive",1008,193,190,66},
    {"New Yeller",1009,255,255,0}, {"Really blue",1010,0,0,255}, {"Navy blue",1011,0,32,96},
    {"Deep blue",1012,33,84,185}, {"Cyan",1013,4,175,236}, {"CGA brown",1014,170,85,0},
    {"Magenta",1015,170,0,170}, {"Pink",1016,255,102,204}, {"Teal",1018,18,238,212},
    {"Toothpaste",1019,0,255,255}, {"Lime green",1020,0,255,0}, {"Camo",1021,58,125,21},
    {"Grime",1022,127,142,100}, {"Lavender",1023,140,91,159}, {"Pastel light blue",1024,175,221,255},
    {"Pastel orange",1025,255,201,201}, {"Pastel violet",1026,177,167,255},
    {"Pastel blue-green",1027,159,243,233}, {"Pastel green",1028,204,255,204},
    {"Pastel yellow",1029,255,255,204}, {"Pastel brown",1030,255,204,153},
    {"Royal purple",1031,98,37,209}, {"Hot pink",1032,255,0,191},
}
local _bc_by_name, _bc_by_num = {}, {}
for _, e in ipairs(_bc_list) do
    local entry = { name = e[1], num = e[2], color = Color3.fromRGB(e[3], e[4], e[5]) }
    if not _bc_by_name[e[1]] then _bc_by_name[e[1]] = entry end
    if not _bc_by_num[e[2]]  then _bc_by_num[e[2]]  = entry end
    -- Alias en CamelCase ("Bright red" -> "BrightRed"): es como se llamaban antes
    -- las claves, asi que el codigo ya escrito contra la paleta vieja sigue
    -- funcionando. Se pasa a mayuscula la inicial de cada palabra, no basta con
    -- quitar el espacio (eso daria "Brightred").
    local squashed = e[1]:gsub("[%.%(%)%-]", ""):gsub("%s+(%a)", string.upper):gsub("%s", "")
    if not _bc_by_name[squashed] then _bc_by_name[squashed] = entry end
end
local function _bc_make(entry)
    local c = entry.color
    return setmetatable({ Name = entry.name, Number = entry.num, Color = c,
                          r = c.R, g = c.G, b = c.B }, BrickColor)
end
function BrickColor.new(v, g, b)
    if type(v) == "number" and type(g) == "number" and type(b) == "number" then
        return BrickColor.new(Color3.new(v, g, b))
    end
    if type(v) == "string" then
        return _bc_make(_bc_by_name[v] or _bc_by_name["Medium stone grey"])
    elseif type(v) == "number" then
        return _bc_make(_bc_by_num[v] or _bc_by_name["Medium stone grey"])
    elseif typeof(v) == "Color3" then
        -- Color3 -> el BrickColor mas cercano de la paleta (como Roblox).
        local best, bd = nil, math.huge
        for _, e in ipairs(_bc_list) do
            local dr, dg, db = v.R - (e[3]/255), v.G - (e[4]/255), v.B - (e[5]/255)
            local d = dr*dr + dg*dg + db*db
            if d < bd then bd, best = d, e end
        end
        return _bc_make(_bc_by_name[best[1]])
    end
    return _bc_make(_bc_by_name["Medium stone grey"])
end
function BrickColor.random() return _bc_make(_bc_by_name[_bc_list[math.random(1, #_bc_list)][1]]) end
function BrickColor.palette(index) return _bc_make(_bc_by_name[_bc_list[((index or 1) - 1) % #_bc_list + 1][1]]) end
function BrickColor:__tostring() return self.Name end
-- Sin esto dos BrickColor del mismo color comparaban distinto (son tablas), que
-- es justo la comparacion que hace falta para casar un SpawnLocation con un Team.
function BrickColor.__eq(a, b) return a.Number == b.Number and a.Name == b.Name end
-- Constructores con nombre de Roblox: BrickColor.Red(), BrickColor.Blue()...
BrickColor.White  = function() return BrickColor.new("White")  end
BrickColor.Gray   = function() return BrickColor.new("Medium stone grey") end
BrickColor.Black  = function() return BrickColor.new("Black")  end
BrickColor.Red    = function() return BrickColor.new("Bright red")    end
BrickColor.Yellow = function() return BrickColor.new("Bright yellow") end
BrickColor.Green  = function() return BrickColor.new("Dark green")    end
BrickColor.Blue   = function() return BrickColor.new("Bright blue")   end
BrickColor.DarkGray = function() return BrickColor.new("Dark stone grey") end

)LUAU"
