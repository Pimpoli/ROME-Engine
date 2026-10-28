@tool
extends RefCounted

## Objects offered by "Insertar objeto", grouped as in Roblox Studio. Entries use
## the Roblox name; CLASSES maps the ones implemented by another GodotLuau class.
## Services, runtime-only objects and extension internals are not listed.

const CATEGORIES := [
	["Básico", "world", ["Part", "Model", "Folder", "SpawnLocation", "MeshPart", "WedgePart", "CornerWedgePart", "TrussPart", "Seat", "VehicleSeat", "Tool", "Configuration", "Camera", "WorldModel"]],
	["Scripts", "script", ["Script", "LocalScript", "ModuleScript"]],
	["Interfaz", "gui", ["ScreenGui", "BillboardGui", "SurfaceGui", "Frame", "ScrollingFrame", "CanvasGroup", "ViewportFrame", "TextLabel", "TextButton", "TextBox", "ImageLabel", "ImageButton", "VideoFrame"]],
	["Diseño de interfaz", "gui_modifier", ["UICorner", "UIStroke", "UIGradient", "UIPadding", "UIScale", "UIListLayout", "UIGridLayout", "UIPageLayout", "UITableLayout", "UIFlexItem", "UIAspectRatioConstraint", "UISizeConstraint", "UITextSizeConstraint"]],
	["Luces", "attach", ["PointLight", "SpotLight", "SurfaceLight"]],
	["Efectos", "attach", ["ParticleEmitter", "Fire", "Smoke", "Sparkles", "Explosion", "Beam", "Trail", "Highlight"]],
	["Adornos", "attach", ["SelectionBox", "SelectionSphere", "BoxHandleAdornment", "ConeHandleAdornment", "CylinderHandleAdornment", "LineHandleAdornment", "SphereHandleAdornment"]],
	["Iluminación", "lighting", ["Sky", "Atmosphere", "Clouds", "BloomEffect", "BlurEffect", "ColorCorrectionEffect", "DepthOfFieldEffect", "SunRaysEffect"]],
	["Superficies", "attach", ["Decal", "Texture", "SpecialMesh", "MaterialVariant"]],
	["Restricciones", "attach", ["Attachment", "WeldConstraint", "Weld", "Motor6D", "HingeConstraint", "BallSocketConstraint", "RodConstraint", "RopeConstraint", "SpringConstraint", "PrismaticConstraint", "CylindricalConstraint", "PlaneConstraint", "TorsionSpringConstraint", "RigidConstraint", "NoCollisionConstraint", "UniversalConstraint"]],
	["Fuerzas", "attach", ["AlignPosition", "AlignOrientation", "LinearVelocity", "AngularVelocity", "VectorForce", "Torque", "LineForce", "BodyVelocity", "BodyPosition", "BodyForce", "BodyGyro", "BodyAngularVelocity"]],
	["Valores", "generic", ["StringValue", "NumberValue", "IntValue", "BoolValue", "ObjectValue", "Vector3Value", "CFrameValue", "Color3Value", "BrickColorValue", "RayValue"]],
	["Eventos", "remote", ["RemoteEvent", "UnreliableRemoteEvent", "RemoteFunction", "BindableEvent"]],
	["Sonido", "sound", ["Sound", "SoundGroup", "AudioPlayer", "AudioEmitter", "AudioListener", "AudioFader", "AudioDeviceInput", "AudioDeviceOutput", "Wire", "ReverbSoundEffect", "EchoSoundEffect", "EqualizerSoundEffect", "DistortionSoundEffect", "CompressorSoundEffect", "PitchShiftSoundEffect", "ChorusSoundEffect", "FlangeSoundEffect", "TremoloSoundEffect"]],
	["Interacción", "attach", ["ClickDetector", "ProximityPrompt"]],
	["Personajes", "attach", ["Character", "Humanoid", "Animation", "Accessory", "Bone", "Character2D", "Humanoid2D"]],
	["Equipos", "teams", ["Team"]],
	["Chat", "chat", ["TextChannel", "TextChatCommand", "ChatWindowConfiguration", "ChatInputBarConfiguration", "BubbleChatConfiguration", "ChannelTabsConfiguration"]],
]

const CLASSES := {
	"Model": "RBXModel", "MeshPart": "RBXMeshPart", "WedgePart": "Part", "CornerWedgePart": "Part", "TrussPart": "Part",
	"Seat": "Part", "VehicleSeat": "Part", "Configuration": "Folder", "Script": "ServerScript", "CanvasGroup": "RobloxCanvasGroup",
	"Sky": "LightingSkyNode", "Atmosphere": "AtmosphereNode", "SunRaysEffect": "SunRaysNode", "Decal": "RBXDecal",
	"Texture": "RBXTexture", "SpecialMesh": "RBXMesh", "RemoteEvent": "RemoteEventNode", "UnreliableRemoteEvent": "UnreliableRemoteEventNode",
	"RemoteFunction": "RemoteFunctionNode", "BindableEvent": "BindableEventNode", "Animation": "AnimationObject",
	"Accessory": "RBXModel", "Bone": "RBXBone",
}
# Same implementation as another class: GodotLuau reads ClassName from __rbx_class.
const RBX_CLASS := ["WedgePart", "CornerWedgePart", "TrussPart", "Seat", "VehicleSeat", "Configuration", "Accessory"]
const SHAPES := {"WedgePart": 3, "CornerWedgePart": 4, "TrussPart": 5}
const KINDS := {
	"Folder": "generic", "Configuration": "generic", "Camera": "generic", "WorldModel": "generic", "ScreenGui": "screen_gui",
	"BillboardGui": "attach", "SurfaceGui": "attach", "Clouds": "terrain", "MaterialVariant": "material", "SoundGroup": "sound_service",
	"Character": "world", "Character2D": "world", "Animation": "generic", "Humanoid2D": "generic",
}
const ONLY_2D := ["Character2D", "Humanoid2D"]
const ICONS := {
	"Model": "StudioGroup", "Accessory": "StudioAccessory", "Character": "StudioCharacter", "Animation": "StudioAnimation",
	"ScreenGui": "StudioGui", "Frame": "StudioFrame", "TextLabel": "StudioLabel", "TextBox": "StudioInput",
	"UICorner": "StudioAppearance", "UIStroke": "StudioAppearance", "UIGradient": "StudioAppearance", "UIListLayout": "StudioLayout",
	"UIGridLayout": "StudioLayout", "UIPageLayout": "StudioLayout", "UITableLayout": "StudioLayout", "UISizeConstraint": "StudioConstraint",
	"UIAspectRatioConstraint": "StudioConstraint", "UITextSizeConstraint": "StudioConstraint", "Attachment": "StudioConnection",
	"WeldConstraint": "StudioWeld", "Weld": "StudioWeld", "Motor6D": "StudioWeld", "ParticleEmitter": "StudioEffect",
	"Decal": "StudioTexture", "Texture": "StudioTexture", "Camera": "Camera3D", "StringValue": "String", "NumberValue": "float",
	"IntValue": "int", "BoolValue": "bool", "ObjectValue": "Object", "Vector3Value": "Vector3", "CFrameValue": "Transform3D",
	"Color3Value": "Color", "BrickColorValue": "Color", "RayValue": "Vector3", "RemoteEvent": "Signal", "UnreliableRemoteEvent": "Signal",
	"BindableEvent": "Signal", "RemoteFunction": "MemberMethod", "Sound": "AudioStreamPlayer3D", "SoundGroup": "AudioBusLayout",
	"Sky": "Sky", "Atmosphere": "Environment",
}
const NAMES := {"PlayerObject": "Player", "RobloxDecal": "Decal", "RobloxTexture": "Texture"}

static var _entries: Dictionary = {}
static var _names: Dictionary = {}

static func entries() -> Dictionary:
	if _entries.is_empty():
		for category: Array in CATEGORIES:
			for title: String in category[2]:
				var kind: String = KINDS.get(title, category[1])
				if title.ends_with("SoundEffect"):
					kind = "sound_effect"
				_entries[title] = {"name": title, "type": CLASSES.get(title, title), "category": category[0], "kind": kind,
					"shape": SHAPES.get(title, -1), "rbx_class": title if title in RBX_CLASS else ""}
				if not title in RBX_CLASS and not _names.has(_entries[title].type):
					_names[_entries[title].type] = title
	return _entries

## Accepts a Roblox name ("RemoteEvent") or a GodotLuau class ("RemoteEventNode").
static func entry_for(type_name: String) -> Dictionary:
	var all := entries()
	if all.has(type_name):
		return all[type_name]
	if _names.has(type_name):
		return all[_names[type_name]]
	if ClassDB.can_instantiate(type_name) and ClassDB.is_parent_class(type_name, "Node"):
		return {"name": type_name, "type": type_name, "category": "", "kind": "generic", "shape": -1, "rbx_class": ""}
	return {}

## The ClassName a Luau script sees for this node.
static func class_name_of(node: Node) -> String:
	if node.has_meta("__rbx_class"):
		return str(node.get_meta("__rbx_class"))
	entries()
	var type := node.get_class()
	return _names.get(type, NAMES.get(type, type.trim_prefix("Roblox")))

static func icon_for(title: String) -> Texture2D:
	var theme := EditorInterface.get_editor_theme()
	var type: String = entry_for(title).get("type", title)
	if ResourceLoader.exists("res://GodotLuau/icons/" + type + ".svg"):
		return load("res://GodotLuau/icons/" + type + ".svg")
	if ICONS.has(title) and theme.has_icon(ICONS[title], "EditorIcons"):
		return theme.get_icon(ICONS[title], "EditorIcons")
	var current := type
	while not current.is_empty():
		if ResourceLoader.exists("res://GodotLuau/icons/" + current + ".svg"):
			return load("res://GodotLuau/icons/" + current + ".svg")
		if theme.has_icon(current, "EditorIcons"):
			return theme.get_icon(current, "EditorIcons")
		current = ClassDB.get_parent_class(current)
	return theme.get_icon("Node", "EditorIcons")
