@tool
extends RefCounted

## Places new objects like Roblox Studio: at the center of the editor view,
## resting on the first visible Part there. Physics does not run in the editor,
## so the ray is tested against each Part's box. Without a surface the object
## lands on the y = 0 plane about 20 studs in front of the camera.

const FALLBACK_DISTANCE := 20.0
# A Character builds its rig at runtime; its root sits about 3 studs above the feet.
const CHARACTER_BOUNDS := AABB(Vector3(-1, -3, -1), Vector3(2, 5, 2))

static func place(object: Node3D, parent: Node, scene_root: Node) -> void:
	var target := target_origin(object, scene_root)
	var parent_3d := parent as Node3D
	object.position = parent_3d.global_transform.affine_inverse() * target if parent_3d != null and parent_3d.is_inside_tree() else target

## Global position for the object's origin so its bounds rest on the surface.
static func target_origin(object: Node3D, scene_root: Node) -> Vector3:
	var bounds := local_bounds(object)
	if bounds.size == Vector3.ZERO and object.is_class("RobloxPlayer"):
		bounds = CHARACTER_BOUNDS
	var surface := Vector3.ZERO
	var ray := view_ray()
	if not ray.is_empty():
		var boxes := part_boxes(scene_root)
		var hit := cast(ray.origin, ray.direction, boxes)
		if hit.is_empty():
			var ahead: Vector3 = ray.origin + ray.direction * FALLBACK_DISTANCE
			surface = Vector3(ahead.x, 0, ahead.z)
		elif hit.normal.y > 0.7:
			surface = hit.position
		else:
			# A wall or a ceiling: step out of it and rest on whatever is below.
			var outward := Vector3(hit.normal.x, 0, hit.normal.z).normalized()
			var step := outward * (maxf(bounds.size.x, bounds.size.z) * 0.5 + 0.05)
			surface = drop(hit.position + step - Vector3(0, 0.01, 0), boxes)
	var center := bounds.get_center()
	return Vector3(surface.x - center.x, surface.y - bounds.position.y, surface.z - center.z)

static func view_ray() -> Dictionary:
	var viewport := EditorInterface.get_editor_viewport_3d(0)
	var camera := viewport.get_camera_3d() if viewport != null else null
	if camera == null or not camera.is_inside_tree():
		return {}
	var center := Vector2(viewport.size) * 0.5
	if center.x < 1 or center.y < 1:
		return {"origin": camera.global_position, "direction": -camera.global_basis.z}
	return {"origin": camera.project_ray_origin(center), "direction": camera.project_ray_normal(center)}

## Boxes of the visible Parts in Workspace: [global transform, half size].
static func part_boxes(scene_root: Node) -> Array:
	var boxes := []
	var workspace := scene_root.get_node_or_null("Workspace") if scene_root != null else null
	if workspace == null:
		return boxes
	for part in workspace.find_children("*", "RobloxPart", true, false):
		if not part.is_visible_in_tree() or float(part.get("Transparency")) >= 0.999:
			continue
		var size: Vector3 = part.get("Size")
		boxes.append([part.global_transform, size.abs() * 0.5])
	return boxes

## Closest box hit in front of the origin: {position, normal} or empty.
static func cast(origin: Vector3, direction: Vector3, boxes: Array) -> Dictionary:
	var best := INF
	var result := {}
	for box: Array in boxes:
		var xform: Transform3D = box[0]
		var half: Vector3 = box[1]
		var inverse := xform.affine_inverse()
		var local_origin := inverse * origin
		var local_direction := inverse.basis * direction
		var near := -INF
		var far := INF
		var axis := -1
		var missed := false
		for i in 3:
			if absf(local_direction[i]) < 0.000001:
				if absf(local_origin[i]) > half[i]:
					missed = true
					break
				continue
			var t1 := (-half[i] - local_origin[i]) / local_direction[i]
			var t2 := (half[i] - local_origin[i]) / local_direction[i]
			if t1 > t2:
				var swap := t1
				t1 = t2
				t2 = swap
			if t1 > near:
				near = t1
				axis = i
			far = minf(far, t2)
		if missed or axis < 0 or near > far or near < 0.0 or near >= best:
			continue
		best = near
		var normal := Vector3.ZERO
		normal[axis] = -signf(local_direction[axis])
		result = {"position": origin + direction * near, "normal": (xform.basis.inverse().transposed() * normal).normalized()}
	return result

static func drop(point: Vector3, boxes: Array) -> Vector3:
	var hit := cast(point, Vector3.DOWN, boxes)
	return hit.position if not hit.is_empty() else Vector3(point.x, 0, point.z)

## Bounds of the Parts in the object, in its parent's axes, relative to its origin.
static func local_bounds(object: Node3D) -> AABB:
	var axes := Transform3D(object.transform.basis, Vector3.ZERO)
	var bounds := AABB()
	var found := false
	var parts: Array[Node] = [object]
	parts.append_array(object.find_children("*", "RobloxPart", true, false))
	for part in parts:
		if not part.is_class("RobloxPart"):
			continue
		var size: Vector3 = part.get("Size")
		var relative := Transform3D.IDENTITY
		var node := part
		while node != object and node != null:
			if node is Node3D:
				relative = node.transform * relative
			node = node.get_parent()
		var box := axes * relative * AABB(-size.abs() * 0.5, size.abs())
		bounds = bounds.merge(box) if found else box
		found = true
	return bounds
