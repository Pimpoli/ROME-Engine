extends Node
var level: Node
func _input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_F2 and is_instance_valid(level):
		level._set_active(not level.activated)
		get_viewport().set_input_as_handled()
