extends CharacterBody2D
## Small, editable offline controller for the mixed template's 2D minigame.
@export var speed := 220.0
@export var jump_velocity := -480.0

func _physics_process(delta: float) -> void:
	velocity.y += 980.0 * delta
	var direction := float(Input.is_physical_key_pressed(KEY_D) or Input.is_physical_key_pressed(KEY_RIGHT)) - float(Input.is_physical_key_pressed(KEY_A) or Input.is_physical_key_pressed(KEY_LEFT))
	velocity.x = move_toward(velocity.x, direction * speed, 1800.0 * delta)
	if is_on_floor() and Input.is_physical_key_pressed(KEY_SPACE):
		velocity.y = jump_velocity
	move_and_slide()
