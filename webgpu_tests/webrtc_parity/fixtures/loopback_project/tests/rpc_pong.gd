# The RPC endpoint used by rpc.gd. One of these sits under each peer's
# multiplayer root, at the same relative path on both sides, which is what lets
# an RPC resolve: SceneMultiplayer addresses nodes relative to its root_path.
extends Node

signal got_ping(from: int, payload: String, mode: String)
signal got_pong(from: int, payload: String)

@rpc("any_peer", "call_remote", "reliable")
func ping(payload: String) -> void:
	got_ping.emit(multiplayer.get_remote_sender_id(), payload, "reliable")

@rpc("any_peer", "call_remote", "unreliable")
func ping_unreliable(payload: String) -> void:
	got_ping.emit(multiplayer.get_remote_sender_id(), payload, "unreliable")

@rpc("any_peer", "call_remote", "unreliable_ordered")
func ping_unreliable_ordered(payload: String) -> void:
	got_ping.emit(multiplayer.get_remote_sender_id(), payload, "unreliable_ordered")

@rpc("any_peer", "call_remote", "reliable")
func pong(payload: String) -> void:
	got_pong.emit(multiplayer.get_remote_sender_id(), payload)
