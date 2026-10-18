@tool
extends Node

@export var textures: Array[Texture2D]
@export var anything: Array
@export_file("*.wav") var wav_paths: Array[String]
@export var packed_paths: PackedStringArray
@export var nodes: Array[Node]
@export var node_paths: Array[NodePath]
@export var dict_untyped: Dictionary
@export var dict_named: Dictionary[String, AudioStream]
@export var dict_numbered: Dictionary[int, Texture2D]
@export var dict_keys: Dictionary[AudioStream, float]
