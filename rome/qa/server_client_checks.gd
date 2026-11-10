extends SceneTree

func _initialize() -> void:
	call_deferred("_run")

func _run() -> void:
	var game: Node = load("res://Main.tscn").instantiate()
	root.add_child(game)
	current_scene = game
	var network: Node
	for attempt in 200:
		await create_timer(0.1).timeout
		network = game.get_node_or_null("NetworkService")
		if network != null and network.call("is_connected_net"):
			print("SERVER_CLIENT_QA connected=", network.call("get_server_address"))
			quit(0)
			return
	push_error("SERVER_CLIENT_QA could not connect to the real dedicated server")
	quit(1)
