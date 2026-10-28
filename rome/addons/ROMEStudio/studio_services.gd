@tool
extends RefCounted

# The public Explorer order is stable; RunService remains available internally.
const SERVICES := {
	"Workspace": "RobloxWorkspace", "Players": "Players", "Lighting": "Lighting",
	"MaterialService": "MaterialService", "ReplicatedFirst": "ReplicatedFirst",
	"ReplicatedStorage": "ReplicatedStorage", "ServerScriptService": "ServerScriptService",
	"ServerStorage": "ServerStorage", "StarterGui": "StarterGui", "StarterPack": "StarterPack",
	"StarterPlayer": "StarterPlayer", "Teams": "Teams", "SoundService": "SoundService",
	"TextChatService": "TextChatService", "RunService": "RunService"
}

var root: Node
var queued := false
var repairing := false
var _known: Dictionary = {}
var _watched: Array[WeakRef] = []

func attach(scene: Node) -> void:
	var next_root := scene if scene != null and scene.is_class("RobloxGame3D") else null
	if root != next_root:
		_disconnect_watchers()
		_known.clear()
		root = next_root
	if root == null:
		return
	_watch(root)
	_queue_repair()

func _disconnect_watchers() -> void:
	for reference in _watched:
		var node := reference.get_ref() as Node
		if node == null:
			continue
		if node.child_order_changed.is_connected(_queue_repair):
			node.child_order_changed.disconnect(_queue_repair)
		if node.renamed.is_connected(_queue_repair):
			node.renamed.disconnect(_queue_repair)
	_watched.clear()

func _watch(node: Node) -> void:
	if not node.child_order_changed.is_connected(_queue_repair):
		node.child_order_changed.connect(_queue_repair)
		node.renamed.connect(_queue_repair)
		_watched.append(weakref(node))

func _queue_repair() -> void:
	if repairing or queued or not is_instance_valid(root) or not root.is_inside_tree():
		return
	queued = true
	call_deferred("_repair")

func _repair() -> void:
	queued = false
	if not is_instance_valid(root) or root != EditorInterface.get_edited_scene_root():
		return
	repairing = true
	var changed := false
	var order := 0
	for name: String in SERVICES:
		var type: String = SERVICES[name]
		var result := _ensure_container(root, name, type, order)
		var service := result.get("node") as Node
		changed = changed or result.get("changed", false)
		if service == null:
			continue
		order += 1
		if name == "StarterPlayer":
			var scripts_order := 0
			for scripts_name in ["StarterCharacterScripts", "StarterPlayerScripts"]:
				var scripts_result := _ensure_container(service, scripts_name, scripts_name, scripts_order)
				changed = changed or scripts_result.get("changed", false)
				scripts_order += 1
	if changed:
		EditorInterface.mark_scene_as_unsaved()
	repairing = false

func _ensure_container(parent: Node, name: String, type: String, order: int) -> Dictionary:
	var changed := false
	var key := str(parent.get_instance_id()) + ":" + name
	var service: Node
	if _known.has(key):
		var candidate := (_known[key] as WeakRef).get_ref() as Node
		# A moved service is restored; a detached/deleted one is replaced empty.
		if is_instance_valid(candidate) and not candidate.is_queued_for_deletion() and root.is_ancestor_of(candidate):
			service = candidate
	# Undo can restore an original after an empty replacement was created.
	for child in parent.get_children():
		if child.is_class(type) and not child.is_queued_for_deletion() and not child.has_meta("rome_recreated_service"):
			if service == null or service.has_meta("rome_recreated_service"):
				service = child
				break
	if service == null:
		var named := parent.get_node_or_null(NodePath(name))
		if named != null and named.is_class(type) and not named.is_queued_for_deletion():
			service = named
	if service == null and ClassDB.can_instantiate(type):
		service = ClassDB.instantiate(type)
		service.set_meta("rome_recreated_service", true)
		var placeholder: Node
		if name == "Workspace":
			# Retain sky/camera defaults, but do not silently recreate a deleted floor.
			placeholder = Node.new()
			placeholder.name = "ROMEEmptyWorkspace"
			service.add_child(placeholder)
		parent.add_child(service)
		service.owner = root
		for generated in service.get_children():
			generated.set_meta("rome_recreated_default", true)
		if placeholder != null:
			placeholder.queue_free()
		changed = true
	if service == null:
		return {"node": null, "changed": changed}
	# If an original returns via Undo, preserve objects added to its replacement.
	for child in parent.get_children():
		if child == service or not child.is_class(type) or not child.has_meta("rome_recreated_service"):
			continue
		for content in child.get_children():
			if content.has_meta("rome_recreated_default"):
				# StarterPlayer's default folders may contain newly authored scripts.
				if content.name in ["StarterCharacterScripts", "StarterPlayerScripts"]:
					var destination := service.get_node_or_null(NodePath(str(content.name)))
					if destination != null:
						for script_node in content.get_children():
							script_node.reparent(destination)
					elif content.get_child_count() > 0:
						content.reparent(service)
				continue
			content.reparent(service)
		parent.remove_child(child)
		child.queue_free()
		changed = true
	if service.get_parent() != parent:
		service.reparent(parent)
		service.owner = root
		changed = true
	var conflict := parent.get_node_or_null(NodePath(name))
	if conflict != null and conflict != service:
		# Preserve an ordinary user node that occupied a reserved service name.
		conflict.name = name + "Object"
		changed = true
	if service.name != name:
		service.name = name
		changed = true
	if service.get_index() != order:
		parent.move_child(service, order)
		changed = true
	_known[key] = weakref(service)
	_watch(service)
	return {"node": service, "changed": changed}
