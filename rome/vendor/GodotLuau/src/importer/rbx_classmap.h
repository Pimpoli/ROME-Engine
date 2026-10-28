#ifndef RBX_CLASSMAP_H
#define RBX_CLASSMAP_H

// Tablas de traduccion Roblox -> GodotLuau: clases, alias de propiedades,
// enumerados y paleta BrickColor.
//
// Las tablas se construyen una sola vez dentro de la funcion (static local, NO
// global estatica: ver el gotcha del error 1114 en gl_errors.h).

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

namespace rbx {

// ── Clase de Roblox -> clase registrada en GodotLuau ────────────────
// "" significa que no hay equivalente: el importador usara RBXInstance, que
// conserva nombre, jerarquia y propiedades para que nada se pierda.
inline String map_class(const String &c) {
    static HashMap<String, String> m;
    if (m.is_empty()) {
        // Geometria
        m["Part"] = "Part";              m["BasePart"] = "Part";
        m["WedgePart"] = "Part";         m["CornerWedgePart"] = "Part";
        m["TrussPart"] = "Part";         m["Seat"] = "Part";
        m["VehicleSeat"] = "Part";       m["SpawnLocation"] = "SpawnLocation";
        m["MeshPart"] = "RBXMeshPart";   m["UnionOperation"] = "RBXMeshPart";
        m["NegateOperation"] = "RBXMeshPart";
        m["Model"] = "RBXModel";         m["Tool"] = "Tool";
        m["Accessory"] = "RBXModel";     m["Accoutrement"] = "RBXModel";
        m["Terrain"] = "Terrain";
        // Mallas y superficies
        m["SpecialMesh"] = "RBXMesh";    m["CylinderMesh"] = "RBXMesh";
        m["BlockMesh"] = "RBXMesh";      m["FileMesh"] = "RBXMesh";
        m["Texture"] = "RBXTexture";     m["Decal"] = "RBXDecal";
        m["Bone"] = "RBXBone";
        // Contenedores
        m["Folder"] = "Folder";          m["Configuration"] = "Folder";
        m["Backpack"] = "Backpack";
        // Valores
        m["StringValue"] = "StringValue";  m["NumberValue"] = "NumberValue";
        m["IntValue"] = "IntValue";     m["BoolValue"] = "BoolValue";
        m["ObjectValue"] = "ObjectValue";  m["Vector3Value"] = "Vector3Value";
        m["CFrameValue"] = "CFrameValue";  m["Color3Value"] = "Color3Value";
        m["BrickColorValue"] = "BrickColorValue"; m["RayValue"] = "RayValue";
        // Scripts
        m["Script"] = "ServerScript";      m["LocalScript"] = "LocalScript";
        m["ModuleScript"] = "ModuleScript";
        // Red
        m["RemoteEvent"] = "RemoteEventNode";
        m["UnreliableRemoteEvent"] = "UnreliableRemoteEventNode";
        m["RemoteFunction"] = "RemoteFunctionNode";
        m["BindableEvent"] = "BindableEventNode";
        m["BindableFunction"] = "BindableEventNode";
        // Personaje y fisica
        m["Humanoid"] = "Humanoid";        m["Motor6D"] = "Motor6D";
        m["Attachment"] = "Attachment";
        // Weld/Snap/ManualWeld son la soldadura con C0/C1 (clase Weld): como
        // WeldConstraint perdian C0/C1.
        m["WeldConstraint"] = "WeldConstraint";  m["Weld"] = "Weld";
        m["Snap"] = "Weld";                      m["ManualWeld"] = "Weld";
        m["RigidConstraint"] = "WeldConstraint";
        m["HingeConstraint"] = "HingeConstraint";
        m["BallSocketConstraint"] = "BallSocketConstraint";
        m["RodConstraint"] = "RodConstraint";
        m["SpringConstraint"] = "SpringConstraint";
        m["RopeConstraint"] = "RopeConstraint";
        m["AlignPosition"] = "AlignPosition";
        m["AlignOrientation"] = "AlignOrientation";
        m["LinearVelocity"] = "LinearVelocity";
        m["AngularVelocity"] = "AngularVelocity";
        m["VectorForce"] = "VectorForce";  m["Torque"] = "Torque";
        m["LineForce"] = "LineForce";
        m["BodyVelocity"] = "BodyVelocity";      m["BodyPosition"] = "BodyPosition";
        m["BodyForce"] = "BodyForce";            m["BodyGyro"] = "BodyGyro";
        m["BodyAngularVelocity"] = "BodyAngularVelocity";
        // Animacion
        m["Animation"] = "AnimationObject";
        // UI
        m["ScreenGui"] = "ScreenGui";      m["SurfaceGui"] = "SurfaceGui";
        m["BillboardGui"] = "BillboardGui";
        m["Frame"] = "Frame";        m["CanvasGroup"] = "RobloxCanvasGroup";
        // ViewportFrame: el de verdad (gl_viewport_frame.h), que dibuja su 3D.
        // Su Camera y su WorldModel salen solos: ya hay clases con su nombre.
        m["ViewportFrame"] = "ViewportFrame";
        m["TextLabel"] = "TextLabel";   m["TextButton"] = "TextButton";
        m["TextBox"] = "TextBox";       m["ImageLabel"] = "ImageLabel";
        m["ImageButton"] = "ImageButton";
        m["ScrollingFrame"] = "ScrollingFrame";
        m["UIListLayout"] = "UIListLayout";   m["UIGridLayout"] = "UIGridLayout";
        m["UIPageLayout"] = "UIPageLayout";   m["UICorner"] = "UICorner";
        m["UIGradient"] = "UIGradient";       m["UIStroke"] = "UIStroke";
        m["UIPadding"] = "UIPadding";         m["UIScale"] = "UIScale";
        m["UIAspectRatioConstraint"] = "UIAspectRatioConstraint";
        m["UISizeConstraint"] = "UISizeConstraint";
        m["UITextSizeConstraint"] = "UITextSizeConstraint";
        m["UITableLayout"] = "UITableLayout";  m["UIFlexItem"] = "UIFlexItem";
        // Sonido
        m["Sound"] = "Sound";        m["SoundGroup"] = "SoundGroup";
        // Interaccion
        m["ClickDetector"] = "ClickDetector";
        m["ProximityPrompt"] = "ProximityPrompt";
        // Efectos
        m["ParticleEmitter"] = "ParticleEmitter";  m["Fire"] = "Fire";
        m["Smoke"] = "Smoke";              m["Sparkles"] = "Sparkles";
        m["Explosion"] = "Explosion";
        m["BloomEffect"] = "BloomEffect";  m["BlurEffect"] = "BlurEffect";
        m["ColorCorrectionEffect"] = "ColorCorrectionEffect";
        m["DepthOfFieldEffect"] = "DepthOfFieldEffect";
        m["SunRaysEffect"] = "SunRaysNode";
        m["Atmosphere"] = "AtmosphereNode";  m["Sky"] = "LightingSkyNode";
        // Servicios
        m["Workspace"] = "Workspace";  m["Lighting"] = "Lighting";
        m["Players"] = "Players";            m["Player"] = "PlayerObject";
        m["ReplicatedStorage"] = "ReplicatedStorage";
        m["ReplicatedFirst"] = "ReplicatedFirst";
        m["ServerStorage"] = "ServerStorage";
        m["ServerScriptService"] = "ServerScriptService";
        m["StarterGui"] = "StarterGui";      m["StarterPack"] = "StarterPack";
        m["StarterPlayer"] = "StarterPlayer";
        m["StarterPlayerScripts"] = "StarterPlayerScripts";
        m["StarterCharacterScripts"] = "StarterCharacterScripts";
        m["SoundService"] = "SoundService";  m["RunService"] = "RunService";
        m["TextChatService"] = "TextChatService";
        // Lo que un place guarda bajo TextChatService (F5): sus comandos y
        // configuraciones. Los canales RBX se crean al arrancar.
        m["TextChatCommand"] = "TextChatCommand";   m["TextChannel"] = "TextChannel";
        m["ChatWindowConfiguration"] = "ChatWindowConfiguration";
        m["ChatInputBarConfiguration"] = "ChatInputBarConfiguration";
        m["BubbleChatConfiguration"] = "BubbleChatConfiguration";
        m["ChannelTabsConfiguration"] = "ChannelTabsConfiguration";
        m["Teams"] = "Teams";                m["Team"] = "Team";
        m["MaterialService"] = "MaterialService";
        m["CollectionService"] = "CollectionService";
        m["TweenService"] = "TweenService";
        m["UserInputService"] = "UserInputService";
        m["PlayerGui"] = "PlayerGui";        m["PlayerScripts"] = "PlayerScripts";
    }
    const String *v = m.getptr(c);
    if (v) return *v;
    // Sin entrada en la tabla: si el motor YA registra una clase con ese mismo
    // nombre de Roblox (Beam, Trail, Highlight, ParticleEmitter, los
    // constraints, los efectos...), se usa esa. Antes todas esas caian al
    // comodin RBXInstance y perdian su comportamiento: el rayo del tutorial del
    // place de prueba se quedaba sin Attachment0/1 y fallaba 121 veces por
    // partida. La tabla manda siempre, asi que los nombres que chocan con Godot
    // (Decal, Texture...) siguen resolviendose a mano.
    if (ClassDB::class_exists(StringName(c))) return c;
    return String();
}

// Servicios: el importador los cuelga directamente del DataModel raiz.
inline bool is_service(const String &c) {
    return c.ends_with("Service") || c == "Workspace" || c == "Lighting" ||
           c == "Players" || c == "ReplicatedStorage" || c == "ReplicatedFirst" ||
           c == "ServerStorage" || c == "StarterGui" || c == "StarterPack" ||
           c == "StarterPlayer" || c == "Teams" || c == "Chat" || c == "Debris" ||
           c == "Selection" || c == "Packages";
}

// ── Servicios VISIBLES en el Explorer de Roblox Studio ──────────────
// Un .rbxl serializa ~50 servicios, pero Studio solo enseña estos 14 (los de
// un place nuevo, la misma estructura que crea RobloxTemplate). El resto
// (TimerService, StudioData, PlayerEmulatorService...) existe en el archivo
// pero Studio los OCULTA: nosotros los agrupamos bajo Game/HiddenServices en
// vez de colgarlos sueltos de Game, que era lo que llenaba la raiz de nodos
// "de mas". GetService los sigue encontrando (busca recursivo por nombre).
// Fuente: https://create.roblox.com/docs/projects/data-model (secciones de
// servicios del DataModel que aparecen en el Explorer).
inline bool is_explorer_service(const String &c) {
    static HashMap<String, bool> m;
    if (m.is_empty()) {
        const char *names[] = {
            "Workspace", "Players", "Lighting", "MaterialService",
            "ReplicatedFirst", "ReplicatedStorage", "ServerScriptService",
            "ServerStorage", "StarterGui", "StarterPack", "StarterPlayer",
            "Teams", "SoundService", "TextChatService", nullptr
        };
        for (int k = 0; names[k]; k++) m[String(names[k])] = true;
    }
    return m.has(c);
}

// ── Propiedades internas de Studio: no se aplican nunca ─────────────
inline bool is_internal_property(const String &p) {
    static HashMap<String, bool> m;
    if (m.is_empty()) {
        const char *names[] = {
            "Capabilities", "DefinesCapabilities", "UniqueId", "HistoryId",
            "SourceAssetId", "ScriptGuid", "PhysicsData", "PhysicalConfigData",
            "AeroMeshData", "SlimHash", "ModelMeshData", "ModelMeshCFrame",
            "ModelMeshSize", "InertiaMigrated",
            "NeedsPivotMigration", "UnscaledCofm", "UnscaledVolInertiaDiags",
            "UnscaledVolInertiaOffDiags", "UnscaledVolume", "RootPriority",
            "CollisionGroupId", "LocalizationMatchIdentifier",
            "LocalizationMatchedSourceText", "VertexCount", "HasJointOffset",
            "JointOffset", "LevelOfDetail", "ModelStreamingMode", "LinkedSource",
            "ChildData", "ChildData2", "MeshData", "MeshData2", "SolidMeshHolder",
            "ComponentIndex", "TriangleCount", "HSRData", "HSRMeshIdData",
            "HSRAssetId", "TemporaryCageMeshId", "CollisionGroupData",
            "TexturePackMetadata", "OpenTypeFeatures", "InitialSize",
            "InternalBodyScale", "InternalHeadScale", "FluidFidelityInternal",
            "AudioCanCollide", "EnableFluidForces", "State", "PhysicsGrid", nullptr
        };
        for (int k = 0; names[k]; k++) m[String(names[k])] = true;
    }
    if (m.has(p)) return true;
    // Sufijos de las superficies heredadas de Roblox clasico
    if (p.ends_with("ParamA") || p.ends_with("ParamB") ||
        p.ends_with("SurfaceInput")) return true;
    return false;
}

// ── Nombre serializado -> nombre de la API ──────────────────────────
// En disco Roblox usa nombres historicos que NO coinciden con la API publica.
// Devuelve "" si la propiedad debe ignorarse.
inline String map_property(const String &cls, const String &p) {
    if (is_internal_property(p)) return String();

    static HashMap<String, String> m;
    if (m.is_empty()) {
        m["size"] = "Size";                    // minuscula en disco
        m["shape"] = "Shape";
        m["formFactorRaw"] = "FormFactor";
        m["Color3uint8"] = "Color";
        m["Health_XML"] = "Health";
        m["WorldPivotData"] = "WorldPivot";
        // OJO: en una BasePart, "Velocity" es el nombre viejo de
        // AssemblyLinearVelocity — pero en un BodyVelocity/BodyAngularVelocity
        // "Velocity" es SU propiedad, la que dice a que velocidad empuja. Al
        // renombrarla a ciegas, todo BodyVelocity importado llegaba con velocidad
        // cero: plataformas moviles, proyectiles y vehiculos quietos. Se filtra
        // por clase en gl_rbx_prop_name (ver abajo).
        m["Velocity"] = "AssemblyLinearVelocity";
        m["RotVelocity"] = "AssemblyAngularVelocity";
        m["Part0Internal"] = "Part0";
        m["Part1Internal"] = "Part1";
        m["CFrame0"] = "C0";
        m["TextureID"] = "TextureId";
        m["archivable"] = "Archivable";
        // La MaterialVariant de una pieza va en disco con este nombre. Estaba en
        // la lista de internas y se tiraba: las piezas que la traen guardada (el
        // tema Minecraft de Build a Mountain en ServerStorage: adoquin, tierra,
        // piedra, tablones, tronco) salian lisas; solo tenia textura lo que un
        // script le pone en marcha.
        m["MaterialVariantSerialized"] = "MaterialVariant";
    }
    // En un BodyVelocity/BodyAngularVelocity/BodyGyro, "Velocity" y "RotVelocity"
    // son SUS propiedades, no los alias viejos de la BasePart: renombrarlas dejaba
    // todos los BodyMovers importados a cero (plataformas, proyectiles, vehiculos).
    if ((p == "Velocity" || p == "RotVelocity") && cls.begins_with("Body")) return p;

    const String *v = m.getptr(p);
    if (v) return *v;

    // Color3 sobre una Part es la propiedad Color; en la UI es otra cosa.
    if (p == "Color3" && (cls == "Texture" || cls == "Decal")) return "Color3";
    return p;
}

// Propiedades que en Godot existen pero con OTRO nombre (minuscula/snake_case).
// Sin esto no se aplican: p.ej. Roblox usa "Visible" y Godot "visible", asi que
// un place importado mostraba TODOS los frames aunque estuvieran ocultos.
// Solo se listan las seguras (mismo significado y mismo tipo).
inline String map_native_property(const String &p) {
    static HashMap<String, String> m;
    if (m.is_empty()) {
        m["Visible"] = "visible";     // CanvasItem / Node3D
        m["ZIndex"]  = "z_index";     // CanvasItem
    }
    const String *v = m.getptr(p);
    return v ? *v : String();
}

// ── Enum.Material -> indice 0..36 de GodotLuau ──────────────────────
// Roblox usa valores grandes y dispersos; GodotLuau los tiene consecutivos
// (ver el comentario en roblox_part.h).
inline int map_material(int v) {
    static HashMap<int, int> m;
    if (m.is_empty()) {
        m[256] = 0;   m[272] = 1;   m[288] = 2;   m[512] = 3;   m[528] = 4;
        m[784] = 5;   m[800] = 6;   m[816] = 7;   m[832] = 8;   m[848] = 9;
        m[1088] = 10; m[1040] = 11; m[1056] = 12; m[1072] = 13; m[1280] = 14;
        m[1536] = 15; m[1568] = 16; m[1296] = 17; m[1312] = 18; m[896] = 19;
        m[1328] = 20; m[880] = 21;  m[864] = 22;  m[1360] = 23; m[1344] = 24;
        m[912] = 25;  m[788] = 26;  m[804] = 27;  m[1552] = 28; m[1284] = 29;
        m[1392] = 30; m[1408] = 31; m[1424] = 32; m[1376] = 33; m[1584] = 34;
        m[2048] = 35; m[1792] = 36;
    }
    const int *p = m.getptr(v);
    return p ? *p : 0;   // Plastic por defecto
}

// Enum.PartType -> Shape de GodotLuau. Ball y Block estan INTERCAMBIADOS:
// sin esta traduccion todas las esferas saldrian cubos.
inline int map_part_type(int v) {
    switch (v) {
        case 0: return 1;   // Ball  -> Ball(1)
        case 1: return 0;   // Block -> Block(0)
        case 2: return 2;   // Cylinder
        case 3: return 3;   // Wedge
        case 4: return 4;   // CornerWedge
        default: return 0;
    }
}

// ── Paleta BrickColor ───────────────────────────────────────────────
inline Color brick_color(int n) {
    struct Entry { int id; uint8_t r, g, b; };
    static const Entry T[] = {
        {1,242,243,243},   {2,161,165,162},   {3,249,233,153},   {5,215,197,154},
        {6,194,218,184},   {9,232,186,200},   {11,128,187,219},  {12,203,132,66},
        {18,204,142,105},  {21,196,40,28},    {22,196,112,160},  {23,13,105,172},
        {24,245,205,48},   {25,98,71,50},     {26,27,42,53},     {27,109,110,108},
        {28,40,127,71},    {29,161,196,140},  {36,243,207,155},  {37,75,151,75},
        {38,160,95,53},    {39,193,202,222},  {40,236,236,236},  {41,205,84,75},
        {42,193,223,240},  {43,123,182,232},  {44,247,241,141},  {45,180,210,228},
        {47,217,133,108},  {48,132,182,141},  {49,248,241,132},  {50,236,232,222},
        {100,238,196,182}, {101,218,134,122}, {102,110,153,202}, {103,199,193,183},
        {104,107,50,124},  {105,226,155,64},  {106,218,133,65},  {107,0,143,156},
        {108,104,92,67},   {110,67,84,147},   {111,191,183,177}, {112,104,116,172},
        {113,228,173,200}, {115,199,210,60},  {116,85,165,175},  {118,183,215,213},
        {119,164,189,71},  {120,217,228,167}, {121,231,172,88},  {123,211,111,76},
        {124,146,57,120},  {125,234,184,146}, {126,165,165,203}, {127,220,188,129},
        {128,174,122,89},  {131,156,163,168}, {133,213,115,61},  {134,216,221,86},
        {135,116,134,157}, {136,135,124,144}, {137,224,152,100}, {138,149,138,115},
        {140,32,58,86},    {141,39,70,45},    {143,207,226,247}, {145,121,136,161},
        {146,149,142,163}, {147,147,135,103}, {148,87,88,87},    {149,22,29,50},
        {150,171,173,172}, {151,120,144,130}, {153,149,121,119}, {154,123,46,47},
        {157,255,246,123}, {158,225,164,194}, {168,117,108,98},  {176,151,105,91},
        {178,180,132,85},  {179,137,135,136}, {180,215,169,75},  {190,249,214,46},
        {191,232,171,45},  {192,105,64,40},   {193,207,96,36},   {194,163,162,165},
        {195,70,103,164},  {196,35,71,139},   {198,142,66,133},  {199,99,95,98},
        {200,130,138,93},  {208,229,228,223}, {209,176,142,68},  {210,112,149,120},
        {211,121,181,181}, {212,159,195,233}, {213,108,129,183}, {216,144,76,42},
        {217,124,92,70},   {218,150,112,159}, {219,107,98,155},  {220,167,169,206},
        {221,205,98,152},  {222,228,173,200}, {223,220,144,149}, {224,240,213,160},
        {225,235,184,127}, {226,253,234,141}, {232,125,187,221}, {268,52,43,117},
        {301,80,109,84},   {302,91,93,105},   {303,0,16,176},    {304,44,101,29},
        {305,82,124,174},  {306,51,88,130},   {307,16,42,220},   {308,61,21,133},
        {309,52,142,64},   {310,91,154,76},   {311,159,161,172}, {312,89,34,89},
        {313,31,128,29},   {314,159,173,192}, {315,9,137,207},   {316,123,0,123},
        {317,124,156,107}, {318,138,171,133}, {319,185,196,177}, {320,202,203,209},
        {321,167,94,155},  {322,123,47,123},  {323,148,190,129}, {324,168,189,153},
        {325,223,223,222}, {327,151,0,0},     {328,177,229,166}, {329,152,194,219},
        {330,255,152,220}, {331,255,89,89},   {332,117,0,0},     {333,239,184,56},
        {334,248,217,109}, {335,231,231,236}, {336,199,212,228}, {337,255,148,148},
        {338,190,104,98},  {339,86,36,36},    {340,241,231,199}, {341,254,243,187},
        {342,224,178,208}, {343,212,144,189}, {344,150,85,85},   {345,143,76,42},
        {346,211,190,150}, {347,226,220,188}, {348,237,234,234}, {349,233,218,218},
        {350,136,62,62},   {351,188,155,93},  {352,199,172,120}, {353,202,191,163},
        {354,187,179,178}, {355,108,88,75},   {356,160,132,79},  {357,149,137,136},
        {358,171,168,158}, {359,175,148,131}, {360,150,103,102}, {361,86,66,54},
        {362,126,104,63},  {363,105,102,92},  {364,90,76,66},    {365,106,57,9},
        {1001,248,248,248},{1002,205,205,205},{1003,17,17,17},   {1004,255,0,0},
        {1005,255,176,0},  {1006,180,128,255},{1007,163,75,75},  {1008,193,190,66},
        {1009,255,255,0},  {1010,0,0,255},    {1011,0,32,96},    {1012,33,84,185},
        {1013,4,175,236},  {1014,170,85,0},   {1015,170,0,170},  {1016,255,102,204},
        {1017,255,175,0},  {1018,18,238,212}, {1019,0,255,255},  {1020,0,255,0},
        {1021,58,125,21},  {1022,127,142,100},{1023,140,91,159}, {1024,175,221,255},
        {1025,255,201,201},{1026,177,167,255},{1027,159,243,233},{1028,204,255,204},
        {1029,255,255,204},{1030,255,204,153},{1031,98,37,209},  {1032,255,0,191},
    };
    static HashMap<int, Color> m;
    if (m.is_empty()) {
        for (size_t k = 0; k < sizeof(T) / sizeof(T[0]); k++)
            m[T[k].id] = Color(T[k].r / 255.0f, T[k].g / 255.0f, T[k].b / 255.0f);
    }
    const Color *p = m.getptr(n);
    return p ? *p : Color(0.639f, 0.635f, 0.647f);   // Medium stone grey
}

} // namespace rbx

#endif // RBX_CLASSMAP_H
