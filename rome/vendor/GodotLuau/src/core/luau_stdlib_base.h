// Stdlib de Luau, parte 1 de 4: Once y Wait de las senales, Enum, RaycastParams,
// OverlapParams, Ray, TweenInfo y TweenService.
//
// No es un header normal: se incluye DENTRO del literal LUAU_STDLIB_CODE de
// luau_stdlib.h, y los literales de texto seguidos se juntan en uno
// solo al compilar. Va aparte porque el stdlib pasaba de 2.000 lineas.
R"LUAU(

-- ══════════════════════════════════════════════════════════════════════
--  Once y Wait genericos para las señales que solo traen Connect. El C++
--  los pone en cada señal (gl_signal_lowercase_aliases) y, nada mas cargar
--  esto, los saca de _G y los guarda en el registro.
-- ══════════════════════════════════════════════════════════════════════
function __gl_sig_once(sig, fn)
    local hecho, c = false, nil
    c = sig:Connect(function(...)
        if hecho then return end
        hecho = true
        if c then c:Disconnect() end
        return fn(...)
    end)
    return c
end

-- Cede el hilo que espera hasta el proximo disparo y devuelve sus argumentos.
-- Lo despierta task.spawn(hilo, ...), que el planificador sabe reanudar.
function __gl_sig_wait(sig)
    local hilo = coroutine.running()
    local hecho, c = false, nil
    c = sig:Connect(function(...)
        if hecho then return end
        hecho = true
        if c then c:Disconnect() end
        task.spawn(hilo, ...)
    end)
    return coroutine.yield()
end

-- ══════════════════════════════════════════════════════════════════════
--  class(base) — Roblox-style OOP inheritance with setmetatable
-- ////
--  class(base) — herencia OOP estilo Roblox con setmetatable
-- ══════════════════════════════════════════════════════════════════════
function class(base)
    local cls = {}
    cls.__index = cls
    if base then
        setmetatable(cls, { __index = base })
    end
    cls.new = function(...)
        local self = setmetatable({}, cls)
        if self.init then self:init(...) end
        return self
    end
    cls.super = base
    return cls
end

-- ══════════════════════════════════════════════════════════════════════
--  Enum — exact Roblox enumeration constants
-- ////
--  Enum — constantes de enumeración exactas de Roblox
-- ══════════════════════════════════════════════════════════════════════
Enum = {
    EasingStyle = {
        Linear = 0, Sine = 1, Quad = 2, Cubic = 3,
        Quart = 4, Quint = 5, Bounce = 6, Elastic = 7,
        Back = 8, Exponential = 9,
    },
    EasingDirection = { In = 0, Out = 1, InOut = 2 },
    HumanoidStateType = {
        None = 0, Running = 1, Jumping = 2, Landed = 3,
        Swimming = 4, Climbing = 5, Seated = 6, Dead = 7,
        Freefall = 8, Flying = 9, GettingUp = 10, FallingDown = 11,
        Ragdoll = 12, StrafingNoWalk = 13, Frozen = 14,
        PlatformStanding = 15, Physics = 16, RunningNoPhysics = 17,
    },
    UserInputType = {
        None = 0, MouseButton1 = 1, MouseButton2 = 2, MouseButton3 = 3,
        MouseWheel = 4, MouseMovement = 5, Touch = 7, Gamepad1 = 8,
        Gamepad2 = 9, Keyboard = 10, Focus = 11, Accelerometer = 12,
        Gyro = 13, TextInput = 14,
        Gamepad3 = 15, Gamepad4 = 16, Gamepad5 = 17, Gamepad6 = 18, Gamepad7 = 19, Gamepad8 = 20,
    },
    UserInputState = { Begin = 0, Change = 1, End = 2, Cancel = 3, None = 4 },
    ProximityPromptStyle = { Default = 0, Custom = 1 },
    -- Valores REALES de Roblox (estaban con los dos primeros cambiados):
    ProximityPromptExclusivity = { OneGlobally = 0, OnePerButton = 1, AlwaysShow = 2 },
    ProximityPromptInputType = { Keyboard = 0, Gamepad = 1, Touch = 2 },
    MembershipType = { None = 0, BuildersClub = 1, TurboBuildersClub = 2, OutrageousBuildersClub = 3, Premium = 4 },
    KeyCode = {
        Unknown = 0, Backspace = 8, Tab = 9, Return = 13, Escape = 27,
        Space = 32, Quote = 39, Comma = 44, Minus = 45, Period = 46,
        Slash = 47, Zero = 48, One = 49, Two = 50, Three = 51, Four = 52,
        Five = 53, Six = 54, Seven = 55, Eight = 56, Nine = 57,
        Semicolon = 59, Equals = 61, LeftBracket = 91, Backslash = 92,
        RightBracket = 93, Backquote = 96,
        A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72,
        I = 73, J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80,
        Q = 81, R = 82, S = 83, T = 84, U = 85, V = 86, W = 87, X = 88,
        Y = 89, Z = 90,
        LeftShift = 304, RightShift = 303, LeftControl = 306, RightControl = 305,
        LeftAlt = 308, RightAlt = 307, CapsLock = 301, NumLock = 300,
        ScrollLock = 302, LeftMeta = 310, RightMeta = 309,
        Insert = 277, Home = 278, PageUp = 280, Delete = 127,
        End = 279, PageDown = 281,
        Up = 273, Down = 274, Left = 276, Right = 275,
        F1 = 282, F2 = 283, F3 = 284, F4 = 285, F5 = 286,
        F6 = 287, F7 = 288, F8 = 289, F9 = 290, F10 = 291,
        F11 = 292, F12 = 293,
        KeypadZero = 256, KeypadOne = 257, KeypadTwo = 258, KeypadThree = 259,
        KeypadFour = 260, KeypadFive = 261, KeypadSix = 262, KeypadSeven = 263,
        KeypadEight = 264, KeypadNine = 265, KeypadPeriod = 266,
        KeypadDivide = 267, KeypadMultiply = 268, KeypadMinus = 269,
        KeypadPlus = 270, KeypadEnter = 271,
        -- Mando. Faltaban TODOS, y como en Roblox se usan como CLAVE de tabla
        -- (`[Enum.KeyCode.ButtonX] = "..."`), una tabla asi reventaba entera con
        -- "table index is nil" y se llevaba por delante el script completo.
        -- Los numeros son los de Roblox; roblox_input.h traduce a los de Godot.
        ButtonX = 1000, ButtonY = 1001, ButtonA = 1002, ButtonB = 1003,
        ButtonR1 = 1004, ButtonL1 = 1005, ButtonR2 = 1006, ButtonL2 = 1007,
        ButtonR3 = 1008, ButtonL3 = 1009,
        ButtonStart = 1010, ButtonSelect = 1011,
        DPadLeft = 1012, DPadRight = 1013, DPadUp = 1014, DPadDown = 1015,
        Thumbstick1 = 1016, Thumbstick2 = 1017,
    },
    Material = {
        -- Índices internos (coinciden con RobloxPart::_configure_material).
        -- Antes usaban los valores de Roblox (Neon=288…) y ningún material se
        -- aplicaba porque el switch interno espera estos índices.
        -- NOTA: había una SEGUNDA tabla Material más abajo que sobreescribía a
        -- esta (en Lua la última clave gana) y borraba Water/Air/Ground/… → nil.
        -- Ahora es la única. TODOS los Enum.Material de Roblox resuelven aquí.
        Plastic = 0, SmoothPlastic = 1, Neon = 2, Wood = 3, WoodPlanks = 4,
        Marble = 5, Slate = 6, Concrete = 7, Granite = 8, Brick = 9,
        Metal = 10, CorrodedMetal = 11, Corrodedmetal = 11, DiamondPlate = 12,
        Foil = 13, Grass = 14, Ice = 15, Glass = 16, Sand = 17, Fabric = 18,
        Rock = 19, Snow = 20, Cobblestone = 21, Pebble = 22,
        -- Materiales con visual propio (añadidos en 1.14)
        Ground = 23, Mud = 24, Sandstone = 25, SandStone = 25, Basalt = 26,
        CrackedLava = 27, Glacier = 28, LeafyGrass = 29, Salt = 30,
        Limestone = 31, Pavement = 32, Asphalt = 33,
        ForceField = 34, Forcefield = 34, Water = 35, Air = 36,
        -- Compat: alias sin índice propio
        Fire = 27,
    },
    -- La numeracion de Roblox, que es la que leen el C++ (SurfaceGui, Decal,
    -- luces, particulas) y el importador. Con la de antes (Front = 3) un
    -- SurfaceGui.Face = Enum.NormalId.Front acababa en la cara izquierda.
    NormalId = { Right = 0, Top = 1, Back = 2, Left = 3, Bottom = 4, Front = 5 },
    FillDirection = { Horizontal = 0, Vertical = 1 },
    HorizontalAlignment = { Center = 0, Left = 1, Right = 2 },
    VerticalAlignment = { Center = 0, Top = 1, Bottom = 2 },
    SortOrder = { Name = 0, Custom = 1, LayoutOrder = 2 },
    ApplyStrokeMode = { Contextual = 0, Border = 1 },
    AutomaticSize = { None = 0, X = 1, Y = 2, XY = 3 },
    CameraMode = { Classic = 0, LockFirstPerson = 1 },
    CameraType = { Fixed = 0, Attach = 1, Watch = 2, Track = 3, Follow = 4,
                   Custom = 5, Scriptable = 6, Orbital = 7 },
    ChatVersion = { LegacyChatService = 0, TextChatService = 1 },
    TextChatMessageStatus = { Unknown = 1, Success = 2, Sending = 3, TextFilterFailed = 4, Floodchecked = 5,
        InvalidPrivacySettings = 6, InvalidTextChannelPermissions = 7, MessageTooLong = 8 },
    UserInputState = { Begin = 0, Change = 1, End = 2, Cancel = 3, None = 4 },
    -- Valores oficiales: Smooth=0, Glue=1, Weld=2, Studs=3, Inlet=4, Universal=5,
    -- Hinge=6, Motor=7, SteppingMotor=8, SmoothNoOutlines=10. Faltaban la mitad
    -- (y Weld/Studs/Hinge/Motor estaban corridos), asi que asignar
    -- Enum.SurfaceType.SmoothNoOutlines pasaba nil y reventaba el script entero.
    SurfaceType = {
        Smooth = 0, Glue = 1, Weld = 2, Studs = 3, Inlet = 4, Universal = 5,
        Hinge = 6, Motor = 7, SteppingMotor = 8, SmoothNoOutlines = 10,
        Inbox = 4,   -- alias antiguo de Inlet
    },
    -- Numeracion de Roblox, la que usan el importador y RBXMesh (antes Sphere = 8 y sin Brick).
    MeshType = { Head = 0, Torso = 1, Wedge = 2, Sphere = 3, Cylinder = 4, FileMesh = 5, Brick = 6, Prism = 7, Pyramid = 8, ParallelRamp = 9, RightAngleRamp = 10, CornerWedge = 11 },
    -- Enum.Font COMPLETO con los numeros de Roblox (sacados de Studio el
    -- 08/10 con Enum.Font:GetEnumItems). Los de antes iban corridos desde
    -- Antique (Gotham=16 en vez de 17...), y el Font que guarda el place es
    -- ese numero: un GothamBlack importado se leia como Roboto.
    Font = { Legacy = 0, Arial = 1, ArialBold = 2, SourceSans = 3, SourceSansBold = 4, SourceSansLight = 5, SourceSansItalic = 6, Bodoni = 7, Garamond = 8, Cartoon = 9, Code = 10, Highway = 11, SciFi = 12, Arcade = 13, Fantasy = 14, Antique = 15,
             SourceSansSemibold = 16, Gotham = 17, GothamMedium = 18, GothamBold = 19, GothamBlack = 20, AmaticSC = 21, Bangers = 22, Creepster = 23, DenkOne = 24, Fondamento = 25, FredokaOne = 26, GrenzeGotisch = 27, IndieFlower = 28, JosefinSans = 29, Jura = 30, Kalam = 31, LuckiestGuy = 32, Merriweather = 33, Michroma = 34, Nunito = 35, Oswald = 36, PatrickHand = 37, PermanentMarker = 38,
             Roboto = 39, RobotoCondensed = 40, RobotoMono = 41, Sarpanch = 42, SpecialElite = 43, TitilliumWeb = 44, Ubuntu = 45, BuilderSans = 46, BuilderSansMedium = 47, BuilderSansBold = 48, BuilderSansExtraBold = 49, Arimo = 50, ArimoBold = 51, Unknown = 100 },
    -- OJO: en Roblox el orden es Left=0, RIGHT=1, CENTER=2 (Right ANTES que
    -- Center). Con el orden "natural" cualquier script que pidiera Center
    -- alineaba a la derecha.
    -- Enum.CoreGuiType: sin el, cualquier juego que apague la mochila o la lista
    -- de jugadores de Roblox moria en su primera linea util
    -- (StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.Backpack, false)) y su
    -- inventario propio se quedaba a medio montar, con la plantilla a la vista.
    CoreGuiType = { PlayerList = 0, Health = 1, Backpack = 2, Chat = 3, All = 4,
                    EmotesMenu = 5, SelfView = 6, Captures = 7 },
    CoreGuiComponent = { All = 0, Backpack = 1, Chat = 2, Health = 3, PlayerList = 4 },
    TextXAlignment = { Left = 0, Right = 1, Center = 2 },
    TextTruncate = { None = 0, AtEnd = 1, SplitWord = 2 },
    TextDirection = { Auto = 0, LeftToRight = 1, RightToLeft = 2 },
    SizeConstraint = { RelativeXY = 0, RelativeXX = 1, RelativeYY = 2 },
    BorderMode = { Outline = 0, Middle = 1, Inset = 2 },
    StartCorner = { TopLeft = 0, TopRight = 1, BottomLeft = 2, BottomRight = 3 },
    ScrollingDirection = { X = 1, Y = 2, XY = 4 },
    ElasticBehavior = { WhenScrollable = 0, Always = 1, Never = 2 },
    AutomaticCanvasSize = { None = 0, X = 1, Y = 2, XY = 3 },
    VerticalScrollBarPosition = { Right = 0, Left = 1 },
    ScrollBarInset = { None = 0, ScrollBar = 1, Always = 2 },
    -- En Roblox el enum se llama ResamplerMode (ImageLabel.ResampleMode es la
    -- propiedad). Con el nombre equivocado `Enum.ResamplerMode.Pixelated` era
    -- nil y la linea moria (7 veces en Build A Mountain y Press-The-Button).
    -- ResampleMode se queda como alias por si algun script lo usaba.
    ResamplerMode = { Default = 0, Pixelated = 1 },
    -- Los que el puente necesita para dar EnumItems y no estaban (F8):
    CreatorType = { User = 0, Group = 1 },
    DevCameraOcclusionMode = { Zoom = 0, Invisicam = 1 },
    DevComputerCameraMovementMode = { UserChoice = 0, Classic = 1, Follow = 2, Orbital = 3, CameraToggle = 4 },
    DevComputerMovementMode = { UserChoice = 0, KeyboardMouse = 1, ClickToMove = 2, Scriptable = 3 },
    DevTouchCameraMovementMode = { UserChoice = 0, Classic = 1, Follow = 2, Orbital = 3 },
    DevTouchMovementMode = { UserChoice = 0, Thumbstick = 1, DPad = 2, Thumbpad = 3, ClickToMove = 4,
        Scriptable = 5, DynamicThumbstick = 6 },
    ReverbType = { NoReverb = 0, GenericReverb = 1, PaddedCell = 2, Room = 3, Bathroom = 4, LivingRoom = 5,
        StoneRoom = 6, Auditorium = 7, ConcertHall = 8, Cave = 9, Arena = 10, Hangar = 11, CarpettedHallway = 12,
        Hallway = 13, StoneCorridor = 14, Alley = 15, Forest = 16, City = 17, Mountains = 18, Quarry = 19,
        Plain = 20, ParkingLot = 21, SewerPipe = 22, UnderWater = 23 },
    TableMajorAxis = { RowMajor = 0, ColumnMajor = 1 },
    -- Los que usa el codigo de hoy de Build A Mountain y faltaban (con ellos
    -- `Enum.X.Y` era nil y la linea moria: UIStroke, mando, anuncios...).
    StrokeSizingMode = { FixedSize = 0, ScaledSize = 1 },
    BorderStrokePosition = { Outer = 0, Center = 1, Inner = 2 },
    HapticEffectType = { Custom = 0, UIHover = 1, UIClick = 2, UINotification = 3, GameplayExplosion = 4,
        GameplayCollision = 5 },
    RsvpStatus = { None = 0, Going = 1, NotGoing = 2 },
    AdFormat = { RewardedVideo = 1 },
    AdAvailabilityResult = { IsAvailable = 1, NoFill = 2, DeviceIneligible = 3, ExperienceIneligible = 4,
        PublisherIneligible = 5, PlayerIneligible = 6, InternalError = 7 },
    ShowAdResult = { ShowCompleted = 1, AdNotReady = 2, AdAlreadyShowing = 3, InternalError = 4,
        ShowInterrupted = 5, InsufficientMemory = 6 },
    FeedbackType = { Feedback = 0 },
    FriendStatus = { Unknown = 0, NotFriend = 1, Friend = 2, FriendRequestSent = 3, FriendRequestReceived = 4 },
    AspectType = { FitWithinMaxSize = 0, ScaleWithParentSize = 1 },
    DominantAxis = { Width = 0, Height = 1 },
    TileMode = { Stretch = 0, Wrap = 1, Mirror = 2 },
    LineJoinMode = { Round = 0, Bevel = 1, Miter = 2 },
    UIFlexMode = { None = 0, Grow = 1, Shrink = 2, Fill = 3, Custom = 4 },
    UIFlexAlignment = { None = 0, Fill = 1, SpaceAround = 2, SpaceBetween = 3, SpaceEvenly = 4 },
    ItemLineAlignment = { Automatic = 0, Start = 1, Center = 2, End = 3, Stretch = 4 },
    ScreenInsets = { None = 0, DeviceSafeInsets = 1, CoreUISafeInsets = 2, TopbarSafeInsets = 3 },
    SafeAreaCompatibility = { None = 0, FullscreenExtension = 1 },
    SelectionBehavior = { Escape = 0, Stop = 1 },
    PlaybackState = { Begin = 0, Delayed = 1, Playing = 2, Paused = 3, Completed = 4, Cancelled = 5 },
    FrameStyle = { Custom = 0, ChatBlue = 1, RobloxSquare = 2, RobloxRound = 3, ChatGreen = 4, ChatRed = 5, DropShadow = 6 },
    ButtonStyle = { Custom = 0, RobloxButtonDefault = 1, RobloxButton = 2, RobloxRoundButton = 3, RobloxRoundDefaultButton = 4, RobloxRoundDropdownButton = 5 },
    FontWeight = { Thin = 100, ExtraLight = 200, Light = 300, Regular = 400, Medium = 500, SemiBold = 600, Bold = 700, ExtraBold = 800, Heavy = 900 },
    FontStyle = { Normal = 0, Italic = 1 },
    -- SurfaceGui: como decide su resolucion. Faltaba entero, asi que cualquier
    -- cartel del mundo que lo configurara moria con "index nil".
    -- (ElasticBehavior / AutomaticSize / ApplyStrokeMode / LineJoinMode ya
    --  estaban mas arriba: en Lua la ultima definicion gana, asi que repetirlos
    --  aqui era pisarlos por nada.)
    SurfaceGuiSizingMode = { FixedSize = 0, PixelsPerStud = 1 },
    TextYAlignment = { Top = 0, Center = 1, Bottom = 2 },
    ScaleType = { Stretch = 0, Slice = 1, Tile = 2, Fit = 3, Crop = 4 },
    ZIndexBehavior = { Global = 0, Sibling = 1 },
    SoundPriority = { Ambient = 0, Music = 1, Sfx = 2, Voice = 3 },
    RollOffMode = { Inverse = 0, Linear = 1, LinearSquare = 2, InverseTapered = 3 },
    ParticleEmitterShape = { Box = 0, Sphere = 1, Cylinder = 2, Disc = 3 },
    ParticleEmitterShapeStyle = { Volume = 0, Surface = 1 },
    ParticleEmitterShapeInOut = { Outward = 0, Inward = 1, InAndOut = 2 },
    ParticleOrientation = { FacingCamera = 0, FacingCameraWorldUp = 1, VelocityParallel = 2, VelocityPerpendicular = 3 },
    ParticleFlipbookLayout = { None = 0, Grid2x2 = 1, Grid4x4 = 2, Grid8x8 = 3 },
    ParticleFlipbookMode = { Loop = 0, OneShot = 1, PingPong = 2, Random = 3 },
    TextureMode = { Stretch = 0, Wrap = 1, Static = 2 },
    HighlightDepthMode = { AlwaysOnTop = 0, Occluded = 1 },
    -- Enum.TeamColor NO existe en Roblox: era inventado y sus valores (0..5) no
    -- son numeros de BrickColor, asi que ahora caerian en gris en silencio.
    -- Se usa BrickColor.new("Bright red"), como en Roblox.
    ForceLimitMode = { Magnitude = 0, PerAxis = 1 },
    FormFactor = { Symmetric = 0, Brick = 1, Plate = 2, Custom = 3 },
    MessageType = { MessageOutput = 0, MessageInfo = 1, MessageWarning = 2, MessageError = 3 },
    ThumbnailType = { HeadShot = 0, AvatarBust = 1, AvatarThumbnail = 2 },
    InfoType = { Asset = 0, Product = 1, GamePass = 2, Subscription = 3, Bundle = 4 },
    ProductPurchaseDecision = { NotProcessedYet = 0, PurchaseGranted = 1 },
    CurrencyType = { Default = 0, Robux = 1, Tix = 2 },
    ProductPurchaseChannel = { InExperience = 1, ExperienceDetailsPage = 2, AdReward = 3, CommerceProduct = 4 },
    TeleportType = { ToPlace = 0, ToInstance = 1, ToReservedServer = 2 },
    HumanoidRigType = { R6 = 0, R15 = 1 },
    PathStatus = {
        Success = 0, ClosestNoPath = 1, ClosestOutOfRange = 2,
        FailStartNotEmpty = 3, FailFinishNotEmpty = 4, NoPath = 5,
    },
    PathWaypointAction = { Walk = 0, Jump = 1, Custom = 2 },
    HumanoidDisplayDistanceType = { Viewer = 0, Subject = 1, None = 2 },
    HumanoidHealthDisplayType = { DisplayWhenDamaged = 0, AlwaysOn = 1, AlwaysOff = 2 },
    HumanoidCollisionType = { OuterBox = 0, InnerBox = 1 },
    -- Valores de Roblox (Core es la MAS BAJA aunque valga 1000). Antes Action2,
    -- Action3 y Action4 valian lo mismo y no se podian distinguir.
    AnimationPriority = { Idle = 0, Movement = 1, Action = 2, Action2 = 3, Action3 = 4, Action4 = 5, Core = 1000 },
    PoseEasingStyle = { Linear = 0, Constant = 1, Elastic = 2, Cubic = 3, Bounce = 4, CubicV2 = 5 },
    PoseEasingDirection = { In = 0, Out = 1, InOut = 2 },
    RaycastFilterType = { Include = 0, Exclude = 1 },
    MouseButton = { Left = 1, Right = 2, Middle = 3 },
    MouseBehavior = { Default = 0, LockCenter = 1, LockCurrentPosition = 2 },
    RenderPriority = { First = 0, Input = 100, Camera = 200, Character = 300, Last = 2000 },
    AssetFetchStatus = { Success = 0, Failure = 1, None = 2, Loading = 3, TimedOut = 4 },
    UserCFrame = { Head = 0, LeftHand = 1, RightHand = 2 },
    ContextActionResult = { Pass = 0, Sink = 1 },
    -- F6: los que pedian los juegos del usuario (ContextActionService con
    -- prioridad, GuiService.ViewportDisplaySize, AnalyticsService, el
    -- presupuesto de DataStore).
    ContextActionPriority = { Low = 1000, Medium = 2000, Default = 2000, High = 3000 },
    DisplaySize = { Small = 0, Medium = 1, Large = 2 },
    AnalyticsCustomFieldKeys = { CustomField01 = 0, CustomField02 = 1, CustomField03 = 2 },
    AnalyticsEconomyFlowType = { Sink = 0, Source = 1 },
    AnalyticsProgressionType = { Custom = 0, Start = 1, Complete = 2, Fail = 3 },
    AnalyticsEconomyTransactionType = { IAP = 0, Shop = 1, Gameplay = 2, ContextualPurchase = 3, TimedReward = 4, Onboarding = 5 },
    DataStoreRequestType = { GetAsync = 0, SetIncrementAsync = 1, UpdateAsync = 2, GetSortedAsync = 3,
                             SetIncrementSortedAsync = 4, OnUpdate = 5, ListAsync = 6, GetVersionAsync = 7,
                             RemoveVersionAsync = 8 },
    BulkMoveMode = { FireAllEvents = 0, FireCFrameChanged = 1 },
    PreferredInput = { KeyboardAndMouse = 0, Gamepad = 1, Touch = 2 },
    -- Los encontro el analizador de compatibilidad en el place de prueba:
    -- se usaban y no existian, asi que `Enum.ThumbnailSize.Size420x420` era
    -- indexar nil y el script moria en esa linea.
    ThumbnailSize = { Size48x48 = 0, Size180x180 = 1, Size420x420 = 2,
                      Size60x60 = 3, Size100x100 = 4, Size150x150 = 5,
                      Size352x352 = 6, Size720x720 = 7 },
    ThumbnailType = { HeadShot = 0, AvatarBust = 1, AvatarThumbnail = 2 },
    SortDirection = { Ascending = 0, Descending = 1 },
    VelocityConstraintMode = { Vector = 0, Line = 1, Plane = 2 },
    ActuatorType = { None = 0, Motor = 1, Servo = 2 },
    ActuatorRelativeTo = { Attachment0 = 0, Attachment1 = 1, World = 2 },
    PositionAlignmentMode = { OneAttachment = 0, TwoAttachment = 1 },
    OrientationAlignmentMode = { OneAttachment = 0, TwoAttachment = 1 },
    -- Enum.Technology (Lighting.Technology). Faltaba entero: cualquier place que
    -- hiciera `Lighting.Technology = Enum.Technology.Future` moria indexando nil.
    Technology = { Legacy = 0, Voxel = 1, Compatibility = 2, ShadowMap = 3, Future = 4, Unified = 5 },
    Platform = { Windows = 0, OSX = 1, IOS = 2, Android = 3, Xbox = 4 },
    DeviceType = { Desktop = 0, Phone = 1, Tablet = 2, Console = 3 },
    AccessoryType = { Hat = 0, Hair = 1, Face = 2, Neck = 3, Shoulder = 4, Front = 5, Back = 6, Waist = 7 },
    RunContext = { Legacy = 0, Server = 1, Client = 2 },
    RenderFidelity = { Automatic = 0, Precise = 1, Performance = 2 },
    -- Los valores de Roblox (los del C++ de MeshPart). Aqui estaban Box y Hull
    -- cambiados y un Disabled que no existe.
    CollisionFidelity = { Default = 0, Hull = 1, Box = 2, PreciseConvexDecomposition = 3, Tunable = 4 },
    FluidFidelity = { Automatic = 0, UseCollisionGeometry = 1, UsePreciseGeometry = 2 },
    -- EditableImage, EditableMesh y Content (F9).
    ContentSourceType = { None = 0, Uri = 1, Object = 2, Opaque = 3 },
    ImageCombineType = { BlendSourceOver = 1, Overwrite = 2, Add = 3, Multiply = 4, AlphaBlend = 5,
                         NormalMapBlend = 6, Subtract = 7 },
    ImageAlphaType = { Default = 1, LockCanvasAlpha = 2, LockCanvasColor = 3 },
    AntiAliasing = { Disabled = 0, Enabled = 1 },
    -- (La tabla Material está definida una sola vez, más arriba, con TODOS los
    --  materiales. La duplicada que había aquí borraba Water/Air/Ground → nil.)
    -- Los valores de Roblox. El C++ guarda Block=0 y Ball=1 (al reves): el
    -- puente traduce al leer y escribir Shape.
    PartType = { Ball = 0, Block = 1, Cylinder = 2, Wedge = 3, CornerWedge = 4 },
    AdornShading = { Default = 0, Shaded = 1, XRay = 2, XRayShaded = 3, AlwaysOnTop = 4 },
    AdornCullingMode = { Automatic = 0, Never = 1 },
}

-- ══════════════════════════════════════════════════════════════════════
--  Enum.X:GetEnumItems() — iterar cualquier enum como en Roblox
--  https://create.roblox.com/docs/reference/engine/datatypes/Enum
-- ══════════════════════════════════════════════════════════════════════
-- Los enums de aqui son tablas planas nombre→numero (los compara todo el
-- runtime, incluido el C++, asi que NO se pueden convertir en objetos). Lo que
-- se hace es devolver EnumItems {Name, Value, EnumType} SOLO desde
-- GetEnumItems, y que quien los consuma (TweenInfo.new) acepte ambas formas.
-- Sin esto, AnimUi moria en su primera linea util:
-- `for _, item in ipairs(Enum.EasingStyle:GetEnumItems())`.
do
    local enum_names = {}   -- tabla→nombre (sin ensuciar los enums con claves extra)
    local EnumItemMT = {
        __type = "EnumItem",   -- typeof(item) == "EnumItem", como en Roblox
        __tostring = function(it)
            return "Enum." .. (enum_names[it.EnumType] or "?") .. "." .. it.Name
        end,
    }
    EnumItemMT.__index = EnumItemMT
    local EnumMT = {}
    EnumMT.__index = function(_, key)
        if key == "GetEnumItems" then
            return function(self)
                -- Orden estable: por Value y, a igual Value (alias tipo
                -- SandStone/Sandstone), solo el primer nombre alfabetico.
                -- Los valores YA son EnumItems (ver mas abajo); los alias
                -- comparten objeto, asi que basta con no repetir.
                local names = {}
                for name, v in pairs(self) do
                    if type(v) == "table" and rawget(v, "Value") ~= nil then
                        table.insert(names, name)
                    elseif type(v) == "number" then
                        table.insert(names, name)   -- por si alguno quedo sin convertir
                    end
                end
                table.sort(names)
                local seen, items = {}, {}
                for _, name in ipairs(names) do
                    local v = rawget(self, name)
                    local num = (type(v) == "table") and rawget(v, "Value") or v
                    if not seen[num] then
                        seen[num] = true
                        if type(v) ~= "table" then
                            v = setmetatable({ Name = name, Value = num, EnumType = self }, EnumItemMT)
                        end
                        table.insert(items, v)
                    end
                end
                table.sort(items, function(a, b) return rawget(a, "Value") < rawget(b, "Value") end)
                return items
            end
        end
        return nil
    end
    -- ── Enum.X.Y pasa a ser un EnumItem de verdad ────────────────────
    -- Antes era un NUMERO pelado, asi que `parte.Material.Name` moria y
    -- `typeof(Enum.Font.Gotham)` decia "number". En Roblox es un objeto con
    -- Name, Value y EnumType.
    --
    -- Compatibilidad: Lua NO puede hacer que una tabla sea == a un numero
    -- (__eq solo salta entre dos tablas), asi que la conversion se hace en el
    -- limite C++: gl_num() acepta las dos formas en TODO el puente. Aqui se
    -- añade ademas __eq entre EnumItems y aritmetica basica, para que el
    -- codigo que trataba el enum como numero (item + 1, item < otro) siga
    -- funcionando igual.
    local function _valor(x)
        if type(x) == "table" then return rawget(x, "Value") end
        return x
    end
    EnumItemMT.__eq  = function(a, b) return _valor(a) == _valor(b) end
    EnumItemMT.__lt  = function(a, b) return _valor(a) <  _valor(b) end
    EnumItemMT.__le  = function(a, b) return _valor(a) <= _valor(b) end
    EnumItemMT.__add = function(a, b) return _valor(a) +  _valor(b) end
    EnumItemMT.__sub = function(a, b) return _valor(a) -  _valor(b) end
    EnumItemMT.__concat = function(a, b)
        local function s(x)
            if type(x) == "table" and rawget(x, "Name") then return tostring(x) end
            return tostring(x)
        end
        return s(a) .. s(b)
    end

    -- Busqueda inversa valor→EnumItem por enum, para que el C++ pueda entregar
    -- EnumItems DE VERDAD en los InputObject. Sin esto, input.KeyCode llegaba
    -- como numero pelado y `input.KeyCode == Enum.KeyCode.Escape` era SIEMPRE
    -- false (__eq de Lua solo salta entre dos tablas, nunca numero==tabla):
    -- ni el menu de Escape ni ninguna comparacion de teclas de ningun juego
    -- podia funcionar.
    local __por_valor_global = {}
    _G["__gl_enum_item"] = function(enumName, value)
        local c = __por_valor_global[enumName]
        if c then
            local it = c[value]
            if it then return it end
        end
        return value
    end

    for enumName, tbl in pairs(Enum) do
        if type(tbl) == "table" then
            enum_names[tbl] = enumName
            -- Los numeros de este enum se cambian por EnumItems. Los ALIAS
            -- (dos nombres con el mismo numero, como SandStone/Sandstone)
            -- comparten el mismo objeto: asi `a == b` da true, como en Roblox.
            local por_valor = {}
            local nombres = {}
            for name, v in pairs(tbl) do
                if type(v) == "number" then table.insert(nombres, name) end
            end
            table.sort(nombres)   -- orden estable: el alias canonico es el 1o
            for _, name in ipairs(nombres) do
                local v = rawget(tbl, name)
                local item = por_valor[v]
                if not item then
                    item = setmetatable({ Name = name, Value = v, EnumType = tbl }, EnumItemMT)
                    por_valor[v] = item
                end
                rawset(tbl, name, item)
            end
            __por_valor_global[enumName] = por_valor
            setmetatable(tbl, EnumMT)
        end
    end

    -- Nombres viejos de RaycastFilterType: el MISMO EnumItem que el nuevo. Sin
    -- ellos un script antiguo pasaba nil y Whitelist se trataba como Exclude.
    -- Van aparte porque en la tabla el alias alfabetico (Blacklist) se habria
    -- quedado como nombre canonico.
    rawset(Enum.RaycastFilterType, "Blacklist", Enum.RaycastFilterType.Exclude)
    rawset(Enum.RaycastFilterType, "Whitelist", Enum.RaycastFilterType.Include)
    -- Alias del nombre viejo: el MISMO enum (Enum.ResampleMode.Pixelated ==
    -- Enum.ResamplerMode.Pixelated, y __gl_enum_item vale con los dos nombres).
    rawset(Enum, "ResampleMode", Enum.ResamplerMode)
    __por_valor_global["ResampleMode"] = __por_valor_global["ResamplerMode"]
end

-- ══════════════════════════════════════════════════════════════════════
--  RaycastParams — parameters for workspace:Raycast()
-- ////
--  RaycastParams — parámetros para workspace:Raycast()
-- ══════════════════════════════════════════════════════════════════════
RaycastParams = {}
RaycastParams.__index = RaycastParams
RaycastParams.__type = "RaycastParams"
-- Sin MaxDistance: en Roblox el largo del rayo es el de la direccion. Con un
-- MaxDistance = 1000 por defecto, todo rayo de mas de 1000 studs se cortaba.
function RaycastParams.new()
    return setmetatable({
        FilterDescendantsInstances = {},
        FilterType = Enum.RaycastFilterType.Exclude,
        IgnoreWater = false,
        RespectCanCollide = false,
        CollisionGroup = "Default",
        BruteForceAllSlow = false,
    }, RaycastParams)
end
function RaycastParams:AddToFilter(...)
    for _, inst in ipairs({...}) do
        if type(inst) == "table" and typeof(inst) ~= "Instance" then
            for _, x in ipairs(inst) do table.insert(self.FilterDescendantsInstances, x) end
        else
            table.insert(self.FilterDescendantsInstances, inst)
        end
    end
end

-- ══════════════════════════════════════════════════════════════════════
--  OverlapParams — los de GetPartBoundsInBox / InRadius / GetPartsInPart.
--  No existia: `OverlapParams.new()` mataba la primera linea de cualquier
--  hitbox o zona moderna. MaxParts = 0 es sin limite.
-- ══════════════════════════════════════════════════════════════════════
OverlapParams = {}
OverlapParams.__index = OverlapParams
OverlapParams.__type = "OverlapParams"
function OverlapParams.new()
    return setmetatable({
        FilterDescendantsInstances = {},
        FilterType = Enum.RaycastFilterType.Exclude,
        MaxParts = 0,
        CollisionGroup = "Default",
        RespectCanCollide = false,
        BruteForceAllSlow = false,
    }, OverlapParams)
end
OverlapParams.AddToFilter = RaycastParams.AddToFilter

-- ══════════════════════════════════════════════════════════════════════
--  Ray — el rayo clasico (Ray.new + workspace:FindPartOnRay). No existia, asi
--  que todo el codigo anterior a workspace:Raycast moria en Ray.new.
-- ══════════════════════════════════════════════════════════════════════
Ray = {}
Ray.__type = "Ray"
Ray.__index = function(r, k)
    if k == "Unit" then
        local d = rawget(r, "Direction")
        local m = d.Magnitude
        return Ray.new(rawget(r, "Origin"), m > 1e-12 and d / m or d)
    end
    return Ray[k]
end
function Ray.new(origin, direction)
    return setmetatable({ Origin = origin or Vector3.new(), Direction = direction or Vector3.new() }, Ray)
end
function Ray:ClosestPoint(p)
    local d = self.Direction
    local m2 = d:Dot(d)
    if m2 < 1e-12 then return self.Origin end
    local t = math.max(0, (p - self.Origin):Dot(d) / m2)
    return self.Origin + d * t
end
function Ray:Distance(p) return (p - self:ClosestPoint(p)).Magnitude end
function Ray.__tostring(r) return tostring(r.Origin) .. ", " .. tostring(r.Direction) end

-- ══════════════════════════════════════════════════════════════════════
--  shared — tabla compartida entre scripts (equivalente a Roblox shared)
-- ══════════════════════════════════════════════════════════════════════
if not shared then shared = {} end

-- ══════════════════════════════════════════════════════════════════════
--  TweenInfo — tipo de dato para configurar animaciones
-- ══════════════════════════════════════════════════════════════════════
TweenInfo = {}
TweenInfo.__index = TweenInfo

-- Acepta el enum como numero (Enum.EasingStyle.Quad) o como EnumItem (lo que
-- devuelve GetEnumItems): el motor de easing compara contra numeros, asi que
-- aqui se normaliza a numero una sola vez.
local function _enum_value(v, default)
    if type(v) == "table" and type(v.Value) == "number" then return v.Value end
    if type(v) == "number" then return v end
    -- El default tambien puede ser un EnumItem (Enum.EasingStyle.Quad ES una
    -- tabla): devolverlo sin desenvolver dejaba EasingStyle como tabla y _ease
    -- comparaba tabla==numero -> todo false -> tween LINEAL en vez de Quad/Out.
    if type(default) == "table" and type(default.Value) == "number" then return default.Value end
    return default
end

-- EasingStyle y EasingDirection son EnumItems, como en Roblox
-- (`info.EasingStyle == Enum.EasingStyle.Quad`). El motor usa sus numeros,
-- guardados aparte en _es y _ed.
function TweenInfo.new(tweenTime, easingStyle, easingDirection, repeatCount, reverses, delayTime)
    local es = _enum_value(easingStyle, Enum.EasingStyle.Quad)
    local ed = _enum_value(easingDirection, Enum.EasingDirection.Out)
    return setmetatable({
        Time            = tweenTime or 1,
        EasingStyle     = __gl_enum_item("EasingStyle", es),
        EasingDirection = __gl_enum_item("EasingDirection", ed),
        RepeatCount     = repeatCount or 0,
        Reverses        = reverses or false,
        DelayTime       = delayTime or 0,
        _es = es,
        _ed = ed,
    }, TweenInfo)
end
)LUAU"
R"LUAU(
-- ══════════════════════════════════════════════════════════════════════
--  TweenService — Roblox-style property interpolation
-- ////
--  TweenService — interpolación de propiedades estilo Roblox
-- ══════════════════════════════════════════════════════════════════════
local function _ease(styleEnum, dirEnum, t)
    if t <= 0 then return 0 elseif t >= 1 then return 1 end
    -- EasingStyle: Linear=0,Sine=1,Quad=2,Cubic=3,Quart=4,Quint=5,Bounce=6,Elastic=7,Back=8,Expo=9
    local s = styleEnum or 2  -- Quad por defecto

    local function easeIn(x)
        if s == 0 then return x                                                    -- Linear
        elseif s == 2 then return x * x                                            -- Quad
        elseif s == 3 then return x * x * x                                        -- Cubic
        elseif s == 4 then return x * x * x * x                                    -- Quart
        elseif s == 5 then return x * x * x * x * x                               -- Quint
        elseif s == 1 then return 1 - math.cos(x * math.pi * 0.5)                 -- Sine
        elseif s == 9 then return x == 0 and 0 or math.pow(2, 10 * x - 10)        -- Exponential
        elseif s == 8 then -- Back
            local c1 = 1.70158
            return (c1 + 1) * x * x * x - c1 * x * x
        elseif s == 6 then -- Bounce In = 1 - bounceOut(1-x)
            local bx = 1 - x
            local n1, d1 = 7.5625, 2.75
            local r
            if bx < 1 / d1 then r = n1 * bx * bx
            elseif bx < 2 / d1 then bx -= 1.5 / d1; r = n1 * bx * bx + 0.75
            elseif bx < 2.5 / d1 then bx -= 2.25 / d1; r = n1 * bx * bx + 0.9375
            else bx -= 2.625 / d1; r = n1 * bx * bx + 0.984375 end
            return 1 - r
        elseif s == 7 then -- Elastic
            local c4 = (2 * math.pi) / 3
            return x == 0 and 0 or -math.pow(2, 10 * x - 10) * math.sin((x * 10 - 10.75) * c4)
        end
        return x
    end

    local function easeOut(x)
        if s == 0 then return x
        elseif s == 2 then local v = 1 - x; return 1 - v * v           -- Quad
        elseif s == 3 then local v = 1 - x; return 1 - v * v * v       -- Cubic
        elseif s == 4 then local v = 1 - x; return 1 - v * v * v * v   -- Quart
        elseif s == 5 then local v = 1 - x; return 1 - v * v * v * v * v -- Quint
        elseif s == 1 then return math.sin(x * math.pi * 0.5)           -- Sine
        elseif s == 9 then return x == 1 and 1 or 1 - math.pow(2, -10 * x) -- Expo
        elseif s == 8 then -- Back
            local c1 = 1.70158; local c3 = c1 + 1; local v = x - 1
            return 1 + c3 * v * v * v + c1 * v * v
        elseif s == 6 then -- Bounce Out
            local n1, d1 = 7.5625, 2.75
            if x < 1 / d1 then return n1 * x * x
            elseif x < 2 / d1 then x -= 1.5 / d1; return n1 * x * x + 0.75
            elseif x < 2.5 / d1 then x -= 2.25 / d1; return n1 * x * x + 0.9375
            else x -= 2.625 / d1; return n1 * x * x + 0.984375 end
        elseif s == 7 then -- Elastic
            local c4 = (2 * math.pi) / 3
            return x == 1 and 1 or math.pow(2, -10 * x) * math.sin((x * 10 - 0.75) * c4) + 1
        end
        return x
    end

    local d = dirEnum or 1  -- Out por defecto
    if d == 0 then return easeIn(t)
    elseif d == 1 then return easeOut(t)
    else
        if t < 0.5 then return easeIn(t * 2) * 0.5
        else return 1 - easeIn((1 - t) * 2) * 0.5 end
    end
end

local function _lerp_val(a, b, t)
    local ta, tb = type(a), type(b)
    if ta == "number" and tb == "number" then
        return a + (b - a) * t
    end
    -- Vector3 / Vector2. AQUI ESTABA EL BUG QUE MATABA TODOS LOS TWEENS DE
    -- OBJETOS: se usaba rawget(a, "x"). Dos fallos en una linea —
    --   1) rawget no vale para USERDATA en Luau (un Vector3 lo es), y
    --   2) los campos son X/Y/Z en MAYUSCULA, que salen por __index.
    -- El resultado era un error dentro del pcall de arriba, que se lo tragaba
    -- sin decir nada: un tween de Position o de Size no movia NADA y no
    -- aparecia ni un mensaje en la consola. Los de numeros (Transparency,
    -- BackgroundTransparency…) si funcionaban, y por eso parecia que
    -- TweenService "iba pero mal".
    if ta == "userdata" and tb == "userdata" then
        -- Color3 primero: sus campos son R/G/B, no X/Y/Z. Sin esta rama caia
        -- abajo con X=nil y salia un Vector2(0,0) que la escritura de
        -- BackgroundColor3/TextColor3 descartaba EN SILENCIO (check_color3
        -- exige Color3): un tween de color no movia nada y no avisaba.
        if a.R ~= nil and b.R ~= nil then
            return Color3.new(a.R + (b.R - a.R) * t,
                              a.G + (b.G - a.G) * t,
                              a.B + (b.B - a.B) * t)
        end
        local ax, ay = a.X or 0, a.Y or 0
        local bx, by = b.X or 0, b.Y or 0
        local az, bz = a.Z, b.Z
        if az == nil or bz == nil then
            return Vector2.new(ax + (bx - ax) * t, ay + (by - ay) * t)
        end
        return Vector3.new(ax + (bx - ax) * t, ay + (by - ay) * t, az + (bz - az) * t)
    end
    -- UDim2 / Color3 table interpolation
    if ta == "table" and tb == "table" then
        if a.X ~= nil and type(a.X) == "table" and a.X.Scale ~= nil then
            return UDim2.new(
                (a.X.Scale  or 0) + ((b.X.Scale  or 0) - (a.X.Scale  or 0)) * t,
                (a.X.Offset or 0) + ((b.X.Offset or 0) - (a.X.Offset or 0)) * t,
                (a.Y.Scale  or 0) + ((b.Y.Scale  or 0) - (a.Y.Scale  or 0)) * t,
                (a.Y.Offset or 0) + ((b.Y.Offset or 0) - (a.Y.Offset or 0)) * t
            )
        end
        if a.R ~= nil then
            return Color3.new(
                (a.R or 0) + ((b.R or 0) - (a.R or 0)) * t,
                (a.G or 0) + ((b.G or 0) - (a.G or 0)) * t,
                (a.B or 0) + ((b.B or 0) - (a.B or 0)) * t
            )
        end
        if a.r ~= nil then
            return Color3.new(
                (a.r or 0) + ((b.r or 0) - (a.r or 0)) * t,
                (a.g or 0) + ((b.g or 0) - (a.g or 0)) * t,
                (a.b or 0) + ((b.b or 0) - (a.b or 0)) * t
            )
        end
    end
    return t >= 1 and b or a
end

-- Senal de Lua COMPLETA (Connect/Once/Wait + los alias en minuscula que Roblox
-- mantiene por compatibilidad + Disconnect real). Antes las senales escritas en
-- Lua solo tenian Connect/Once, y cualquier script que hiciera
-- `tween.Completed:Wait()` moria — es justo lo que hace la pantalla de carga de
-- un place normal (234 errores en el place de prueba).
function _gl_signal()
    local cbs = {}
    local sig = {}
    local function add(fn, once)
        if type(fn) ~= "function" then return { Connected = false, Disconnect = function() end } end
        -- dueno: el script que conecta (F8: la VM es de todo el contexto y la
        -- señal la comparten todos sus scripts).
        local entry = { fn = fn, once = once, alive = true, dueno = __gl_dueno and __gl_dueno() }
        table.insert(cbs, entry)
        local conn = { Connected = true }
        function conn:Disconnect()
            self.Connected = false
            entry.alive = false
        end
        conn.disconnect = conn.Disconnect
        return conn
    end
    function sig:Connect(fn) return add(fn, false) end
    function sig:Once(fn)    return add(fn, true) end
    function sig:Wait()
        local done, out = false, nil
        add(function(...) done = true; out = table.pack(...) end, true)
        while not done do task.wait() end
        return table.unpack(out, 1, out.n)
    end
    function sig:Fire(...)
        for i = #cbs, 1, -1 do
            local e = cbs[i]
            if not e.alive then
                table.remove(cbs, i)
            else
                if e.once then e.alive = false; table.remove(cbs, i) end
                -- Corre como tarea de quien conecto; si ese script ya murio,
                -- su conexion cae (en Roblox, destruir un script desconecta lo suyo).
                if e.dueno ~= nil then
                    if __gl_spawn_dueno(e.dueno, e.fn, ...) == nil and e.alive then
                        e.alive = false
                        table.remove(cbs, i)
                    end
                else
                    task.spawn(e.fn, ...)
                end
            end
        end
    end
    sig.connect = sig.Connect
    sig.once    = sig.Once
    sig.wait    = sig.Wait
    return sig
end

local TweenService = {}
TweenService.__index = TweenService

-- ══════════════════════════════════════════════════════════════════════
--  UN SOLO Heartbeat PARA TODOS LOS TWEENS
--
--  Antes cada tween hacia su propio `RunService.Heartbeat:Connect`. Cada
--  conexion viva cuesta un hilo Luau NUEVO por frame (asi dispara RunService),
--  asi que con 65 tweens a la vez eran 65 hilos por frame — medido en el place
--  de prueba: frames de 190 ms dentro de RunService, y el numero de conexiones
--  CRECIENDO (10 -> 47 -> 65) segun avanzaba la partida.
--
--  Crecia porque un tween sobre una instancia DESTRUIDA no termina nunca: sigue
--  escribiendo propiedades sobre un objeto muerto, frame tras frame, para
--  siempre. Aqui se purgan.
--
--  Ahora hay UNA conexion para todos, que se crea con el primer tween y se
--  suelta cuando no queda ninguno.
-- ══════════════════════════════════════════════════════════════════════
local _tw_activos = {}
local _tw_conn    = nil
local _tw_n       = 0

-- Una instancia destruida devuelve Parent=nil y Name="" (ver el puente).
local function _tw_instancia_viva(inst)
    if inst == nil then return false end
    local ok, padre = pcall(function() return inst.Parent end)
    if not ok then return false end
    if padre ~= nil then return true end
    local ok2, nom = pcall(function() return inst.Name end)
    if not ok2 then return false end
    return nom ~= ""
end

local function _tw_paso(dt)
    local ahora = time()
    local i = 1
    while i <= _tw_n do
        local e = _tw_activos[i]
        local vivo = true
        if e == nil or e.tw._cancelled or not e.tw._playing then
            vivo = false
        elseif not _tw_instancia_viva(e.tw.Instance) then
            -- La instancia se destruyo: en Roblox el tween deja de importar.
            e.tw._playing = false
            vivo = false
        else
            local elapsed = ahora - e.startT
            if elapsed >= 0 then
                if elapsed > e.dur then elapsed = e.dur end
                local t = e.dur > 0 and (elapsed / e.dur) or 1
                local eased = _ease(e.style, e.dir, e.forward and t or (1 - t))
                -- UN pcall para todas las propiedades (antes se creaba una
                -- closure nueva POR PROPIEDAD Y FRAME).
                pcall(e.aplicar, eased)
                if elapsed >= e.dur then
                    if e.rev and e.forward then
                        e.forward = false
                        e.startT  = ahora
                    else
                        e.rep += 1
                        -- RepeatCount = -1 es INFINITO en Roblox.
                        if e.repeats >= 0 and e.rep > e.repeats then
                            e.tw._playing = false
                            e.tw.PlaybackState = Enum.PlaybackState.Completed
                            e.tw._entrada = nil
                            e.completed:Fire(Enum.PlaybackState.Completed)
                            vivo = false
                        else
                            e.forward = true
                            e.startT  = ahora
                        end
                    end
                end
            end
        end
        if vivo then
            i += 1
        else
            if e then e.tw._entrada = nil end
            _tw_activos[i] = _tw_activos[_tw_n]
            _tw_activos[_tw_n] = nil
            _tw_n -= 1
        end
    end
    if _tw_n == 0 and _tw_conn then
        _tw_conn:Disconnect()
        _tw_conn = nil
    end
end

local function _tw_alta(e)
    _tw_n += 1
    _tw_activos[_tw_n] = e
    if not _tw_conn then
        local rs = game:GetService("RunService")
        -- El paso de TODOS los tweens del contexto es del sistema (F8): si
        -- fuera del primer script que hizo Play, al morir ese script se
        -- congelarian los tweens de todos.
        if rs then
            (__gl_sistema or task.spawn)(function()
                _tw_conn = rs.Heartbeat:Connect(_tw_paso)
            end)
        end
    end
end

local function _tw_baja(tw)
    if tw._entrada == nil then return end
    tw._entrada.tw._playing = false
    tw._entrada = nil
end

function TweenService:Create(instance, tweenInfo, goals)
    if not instance or not tweenInfo then return nil end

    local completed = _gl_signal()
    local tween = {
        PlaybackState = Enum.PlaybackState.Begin,
        Completed  = completed,
        Instance   = instance,
        TweenInfo  = tweenInfo,
        _playing   = false,
        _cancelled = false,
        _entrada   = nil,
    }

    function tween:Play()
        if self._playing then return end
        _tw_baja(self)
        self._cancelled = false
        self._playing   = true
        self.PlaybackState = Enum.PlaybackState.Playing

        local gls     = goals or {}
        local info    = tweenInfo or {}
        local dur     = info.Time or 1
        local style   = info._es or _enum_value(info.EasingStyle, Enum.EasingStyle.Quad)
        local dir     = info._ed or _enum_value(info.EasingDirection, Enum.EasingDirection.Out)
        local delay   = info.DelayTime or 0
        local repeats = info.RepeatCount or 0
        local rev     = info.Reverses or false

        local startVals = {}
        for prop, _ in pairs(gls) do
            local ok, v = pcall(function() return instance[prop] end)
            if ok then startVals[prop] = v end
        end

        -- La closure de aplicado se crea UNA vez por tween, no por frame.
        local aplicar = function(eased)
            for prop, goal in pairs(gls) do
                local sv = startVals[prop]
                if sv ~= nil then
                    instance[prop] = _lerp_val(sv, goal, eased)
                end
            end
        end

        local e = {
            tw = self, completed = completed, aplicar = aplicar,
            dur = dur, style = style, dir = dir,
            repeats = repeats, rev = rev,
            rep = 0, forward = true, startT = time() + delay,
        }
        self._entrada = e
        _tw_alta(e)
    end

    function tween:Pause()
        if not self._playing then return end
        self._playing = false
        self.PlaybackState = Enum.PlaybackState.Paused
        _tw_baja(self)
    end

    function tween:Cancel()
        self._cancelled = true
        self._playing   = false
        self.PlaybackState = Enum.PlaybackState.Cancelled
        _tw_baja(self)
        completed:Fire(Enum.PlaybackState.Cancelled)
    end

    -- Un Tween es un Instance en Roblox, asi que se puede destruir.
    function tween:Destroy()
        self._cancelled = true
        self._playing   = false
        _tw_baja(self)
    end
    tween.destroy = tween.Destroy

    return tween
end

_G["__SVC_TweenService"] = TweenService
)LUAU"
