@tool
extends EditorPlugin
# Drags files, resources and nodes onto the inspector's collection editors the way the FileSystem and Scene docks
# do (Control.force_drag() plus the mouse release the viewport turns into a drop), then checks what was added.
# Prints "[DROP-TEST] <check> PASS|FAIL ..." per check and "[DROP-TEST] DONE <failures>", then quits.

const WAVS := ["res://assets/kick.wav", "res://assets/snare.wav", "res://assets/hat.wav"]
const PNGS := ["res://assets/red.png", "res://assets/blue.png"]

var failures := 0


func _enter_tree() -> void:
	_run.call_deferred()


func _frames(count := 3) -> void:
	for i in count:
		await get_tree().process_frame


func _check(name: String, ok: bool, detail := "") -> void:
	print("[DROP-TEST] %s %s %s" % [name, "PASS" if ok else "FAIL", detail])
	if not ok:
		failures += 1


func _files(paths: Array) -> Dictionary:
	return {"type": "files", "files": PackedStringArray(paths), "from": null}


func _nodes(nodes: Array) -> Dictionary:
	var paths := []
	for n: Node in nodes:
		paths.push_back(n.get_path())
	return {"type": "nodes", "nodes": paths}


# A real drag and drop: start the drag, move over the target, release.
func _drop_on(control: Control, data: Dictionary, at := Vector2.INF) -> bool:
	if control == null:
		print("[DROP-TEST] (no drop target found)")
		return false
	var viewport := control.get_viewport()
	if at == Vector2.INF:
		at = control.get_global_rect().get_center()
	control.force_drag(data, null)
	if not OS.get_environment("DROP_TEST_DEBUG").is_empty():
		print("[DROP-DEBUG] target %s rect %s visible %s dragging %s viewport %s hovered-before %s" % [control.get_class(), control.get_global_rect(), control.is_visible_in_tree(), viewport.gui_is_dragging(), viewport.get_visible_rect(), viewport.gui_get_hovered_control()])
	var motion := InputEventMouseMotion.new()
	motion.position = at
	motion.global_position = at
	viewport.push_input(motion)
	await _frames(1)
	if not OS.get_environment("DROP_TEST_DEBUG").is_empty():
		print("[DROP-DEBUG] hovered %s" % [viewport.gui_get_hovered_control()])
	var release := InputEventMouseButton.new()
	release.button_index = MOUSE_BUTTON_LEFT
	release.pressed = false
	release.position = at
	release.global_position = at
	viewport.push_input(release)
	await _frames(3)
	return true


func _inspector_array() -> Control:
	# The inspector rebuilds after an edit; skip the sections it is about to free.
	for array: Control in EditorInterface.get_inspector().find_children("*", "EditorInspectorArray", true, false):
		if array.is_visible_in_tree() and not array.is_queued_for_deletion():
			return array
	return null


func _add_button(array: Control) -> Control:
	if array == null:
		return null
	for button: Button in array.find_children("*", "Button", true, false):
		if button.is_visible_in_tree() and button.text.begins_with("Add"):
			return button
	return null


# Drops on a property array: on its "Add" button when the section is unfolded, else on the section header.
func _drop_on_array(data: Dictionary) -> void:
	var array := _inspector_array()
	var button := _add_button(array)
	if button:
		await _drop_on(button, data)
	elif array:
		await _drop_on(array, data, array.get_global_rect().position + Vector2(array.size.x / 2, 8))
	else:
		await _drop_on(null, data)


func _property_editor(name: String) -> Control:
	for property: EditorProperty in EditorInterface.get_inspector().find_children("*", "EditorProperty", true, false):
		if property.get_edited_property() == StringName(name):
			# The array/dictionary editors' header button takes drops.
			for button: Button in property.find_children("*", "Button", false, false):
				return button
			return property
	return null


func _edit_resource(resource: Resource) -> void:
	EditorInterface.edit_resource(resource)
	await _frames(5)


func _test_property_arrays() -> void:
	# AudioStreamRandomizer: a property array with a move function, empty to begin with.
	var randomizer := AudioStreamRandomizer.new()
	await _edit_resource(randomizer)
	await _drop_on_array(_files(WAVS))
	var paths := []
	for i in randomizer.streams_count:
		paths.push_back(randomizer.get_stream(i).resource_path if randomizer.get_stream(i) else "")
	_check("randomizer_appends_three_streams_in_order", paths == WAVS, str(paths))
	var history := EditorInterface.get_editor_undo_redo().get_object_history_id(randomizer)
	EditorInterface.get_editor_undo_redo().get_history_undo_redo(history).undo()
	_check("randomizer_drop_undoes_in_one_step", randomizer.streams_count == 0, str(randomizer.streams_count))
	EditorInterface.get_editor_undo_redo().get_history_undo_redo(history).redo()
	_check("randomizer_drop_redoes", randomizer.streams_count == 3, str(randomizer.streams_count))
	await _frames(5)
	await _drop_on_array(_files([WAVS[0]]))
	_check("randomizer_appends_to_existing", randomizer.streams_count == 4 and randomizer.get_stream(3).resource_path == WAVS[0], str(randomizer.streams_count))
	await _drop_on_array(_files(PNGS))
	_check("randomizer_rejects_textures", randomizer.streams_count == 4, str(randomizer.streams_count))

	# AudioStreamPlaylist: count property, elements are a bare "stream_%d".
	var playlist := AudioStreamPlaylist.new()
	await _edit_resource(playlist)
	await _drop_on_array(_files(WAVS.slice(0, 2)))
	_check("playlist_appends_two", playlist.stream_count == 2 and playlist.get_list_stream(1) and playlist.get_list_stream(1).resource_path == WAVS[1], str(playlist.stream_count))

	# AudioStreamInteractive: count property with a swap method; clips also get their names.
	var interactive := AudioStreamInteractive.new()
	var clips_before := interactive.clip_count
	await _edit_resource(interactive)
	await _drop_on_array(_files(WAVS.slice(1, 3)))
	_check("interactive_appends_clips", interactive.clip_count == clips_before + 2, str(interactive.clip_count))
	if interactive.clip_count == clips_before + 2:
		_check("interactive_clip_named_after_file", interactive.get_clip_name(clips_before) == &"snare" and interactive.get_clip_stream(clips_before + 1).resource_path == WAVS[2], str(interactive.get_clip_name(clips_before)))


func _test_node_properties() -> void:
	EditorInterface.open_scene_from_path("res://main.tscn")
	await _frames(10)
	var root := EditorInterface.get_edited_scene_root()
	var holder := root.get_node("Holder")
	var a := root.get_node("A")
	var b := root.get_node("B")
	EditorInterface.edit_node(holder)
	await _frames(5)

	await _drop_on(_property_editor("textures"), _files(PNGS))
	_check("typed_array_takes_textures", holder.textures.size() == 2 and holder.textures[1].resource_path == PNGS[1], str(holder.textures))
	await _drop_on(_property_editor("textures"), _files(WAVS))
	_check("typed_array_rejects_wrong_type", holder.textures.size() == 2, str(holder.textures.size()))
	await _drop_on(_property_editor("anything"), _files([WAVS[0], PNGS[0]]))
	_check("untyped_array_takes_any_resource", holder.anything.size() == 2 and holder.anything[0] is AudioStream and holder.anything[1] is Texture2D, str(holder.anything))
	await _drop_on(_property_editor("anything"), _nodes([a]))
	_check("untyped_array_takes_node_as_path", holder.anything.size() == 3 and holder.anything[2] is NodePath and holder.get_node(holder.anything[2]) == a, str(holder.anything))
	await _drop_on(_property_editor("wav_paths"), _files(WAVS))
	var wav_paths_ok: bool = holder.wav_paths.size() == 3
	for i in holder.wav_paths.size():
		wav_paths_ok = wav_paths_ok and ResourceUID.ensure_path(holder.wav_paths[i]) == WAVS[i]
	_check("file_path_array_takes_paths", wav_paths_ok, str(holder.wav_paths))
	await _drop_on(_property_editor("wav_paths"), _files(PNGS))
	_check("file_path_array_honors_filter", holder.wav_paths.size() == 3, str(holder.wav_paths.size()))
	await _drop_on(_property_editor("packed_paths"), _files(PNGS))
	_check("packed_string_array_takes_paths", holder.packed_paths == PackedStringArray(PNGS), str(holder.packed_paths))
	await _drop_on(_property_editor("nodes"), _nodes([a, b]))
	_check("node_array_takes_nodes", holder.nodes == [a, b], str(holder.nodes))
	await _drop_on(_property_editor("node_paths"), _nodes([b]))
	_check("node_path_array_takes_relative_paths", holder.node_paths.size() == 1 and holder.get_node(holder.node_paths[0]) == b, str(holder.node_paths))

	await _drop_on(_property_editor("dict_untyped"), _files([WAVS[0], PNGS[0]]))
	_check("untyped_dictionary_keys_by_name", holder.dict_untyped.has("kick") and holder.dict_untyped["red"] is Texture2D, str(holder.dict_untyped))
	await _drop_on(_property_editor("dict_named"), _files([WAVS[0], WAVS[0]]))
	_check("string_keyed_dictionary_dedupes_names", holder.dict_named.has("kick") and holder.dict_named.has("kick 2"), str(holder.dict_named))
	await _drop_on(_property_editor("dict_named"), _files(PNGS))
	_check("string_keyed_dictionary_rejects_wrong_type", holder.dict_named.size() == 2, str(holder.dict_named.size()))
	await _drop_on(_property_editor("dict_numbered"), _files(PNGS))
	_check("int_keyed_dictionary_numbers_entries", holder.dict_numbered.has(0) and holder.dict_numbered.has(1), str(holder.dict_numbered))
	await _drop_on(_property_editor("dict_keys"), _files(WAVS.slice(0, 2)))
	_check("resource_keyed_dictionary_takes_keys", holder.dict_keys.size() == 2 and holder.dict_keys.values() == [0.0, 0.0], str(holder.dict_keys))

	# ItemList: dropped textures become items with the texture as icon and the file name as text.
	var list := root.get_node("List") as ItemList
	EditorInterface.edit_node(list)
	await _frames(5)
	await _drop_on_array(_files(PNGS))
	_check("item_list_items_from_icons", list.item_count == 2 and list.get_item_icon(0).resource_path == PNGS[0] and list.get_item_text(1) == "blue", str(list.item_count))

	# AnimationNodeTransition's inputs are names only: files are refused, not turned into inputs named after paths.
	var transition := AnimationNodeTransition.new()
	await _edit_resource(transition)
	var inputs_before := transition.input_count
	await _drop_on_array(_files(WAVS))
	_check("name_only_array_refuses_files", transition.input_count == inputs_before, str(transition.input_count))

	# MultiplayerSpawner: a property array of scene paths.
	var spawner := root.get_node("Spawner") as MultiplayerSpawner
	EditorInterface.edit_node(spawner)
	await _frames(5)
	await _drop_on_array(_files(["res://target.tscn"]))
	_check("spawner_takes_scene", spawner.get_spawnable_scene_count() == 1 and ResourceUID.ensure_path(spawner.get_spawnable_scene(0)) == "res://target.tscn", str(spawner.get_spawnable_scene_count()))
	await _drop_on_array(_files(PNGS))
	_check("spawner_rejects_non_scenes", spawner.get_spawnable_scene_count() == 1, str(spawner.get_spawnable_scene_count()))


func _project_settings_panel(type: String) -> Control:
	var panels := EditorInterface.get_base_control().get_tree().root.find_children("*", type, true, false)
	if panels.is_empty():
		return null
	var window := (panels[0] as Control).get_window()
	if not window.visible:
		window.popup_centered(Vector2i(1200, 800))
		await _frames(5)
	return panels[0]


# Selects the tab holding `control` in every TabContainer above it.
func _show_tab(control: Control) -> void:
	var child: Node = control
	var parent := control.get_parent()
	while parent:
		if parent is TabContainer:
			(parent as TabContainer).current_tab = child.get_index()
		child = parent
		parent = parent.get_parent()
	await _frames(5)


func _tree_in(panel: Control, index := 0) -> Control:
	var trees := panel.find_children("*", "Tree", true, false).filter(func(t: Tree) -> bool: return t.is_visible_in_tree())
	return trees[index] if index < trees.size() else null


func _test_project_settings_lists() -> void:
	var autoload := await _project_settings_panel("EditorAutoloadSettings")
	if autoload:
		await _show_tab(autoload)
		await _drop_on(_tree_in(autoload), _files(["res://autoload_a.gd", "res://autoload_b.gd"]))
		_check("autoload_list_adds_scripts", ProjectSettings.has_setting("autoload/AutoloadA") and ProjectSettings.has_setting("autoload/AutoloadB"), "")
		await _drop_on(_tree_in(autoload), _files(PNGS))
		_check("autoload_list_refuses_textures", not ProjectSettings.has_setting("autoload/Red"), "")
	else:
		_check("autoload_panel_found", false)

	var localization := await _project_settings_panel("LocalizationEditor")
	if localization:
		await _show_tab(localization)
		var inner_tabs := localization.find_children("*", "TabContainer", true, false)
		if not inner_tabs.is_empty():
			(inner_tabs[0] as TabContainer).current_tab = 0 # Translations.
			await _frames(5)
		await _drop_on(_tree_in(localization), _files(["res://assets/fr.po"]))
		var translations: PackedStringArray = ProjectSettings.get_setting("internationalization/locale/translations", PackedStringArray())
		_check("translation_list_adds_po", translations.has("res://assets/fr.po"), str(translations))
		if not inner_tabs.is_empty():
			(inner_tabs[0] as TabContainer).current_tab = 1 # Remaps.
			await _frames(5)
		await _drop_on(_tree_in(localization), _files(PNGS))
		var remaps: Dictionary = ProjectSettings.get_setting("internationalization/locale/translation_remaps", {})
		_check("remap_list_adds_resources", remaps.has(PNGS[0]) and remaps.has(PNGS[1]), str(remaps.keys()))
	else:
		_check("localization_panel_found", false)
	# The Project Settings dialog is modal: close it before testing the main window.
	var panel: Control = localization if localization else autoload
	if panel:
		panel.get_window().hide()
		await _frames(5)


func _test_audio_bus_effects() -> void:
	var buses := EditorInterface.get_base_control().find_children("*", "EditorAudioBus", true, false)
	if buses.is_empty():
		_check("audio_bus_found", false)
		return
	await _show_tab(buses[0])
	var effects: Control = null
	for tree: Tree in (buses[0] as Control).find_children("*", "Tree", true, false):
		effects = tree
	var before := AudioServer.get_bus_effect_count(0)
	await _drop_on(effects, _files(["res://assets/reverb.tres"]))
	_check("bus_effects_take_effect_file", AudioServer.get_bus_effect_count(0) == before + 1 and AudioServer.get_bus_effect(0, before) is AudioEffectReverb, str(AudioServer.get_bus_effect_count(0)))
	await _drop_on(effects, _files(WAVS))
	_check("bus_effects_refuse_streams", AudioServer.get_bus_effect_count(0) == before + 1, str(AudioServer.get_bus_effect_count(0)))


func _run() -> void:
	var fs := EditorInterface.get_resource_filesystem()
	await _frames(10)
	while fs.is_scanning():
		await _frames(1)
	await _frames(10)
	await _test_property_arrays()
	await _test_node_properties()
	await _test_project_settings_lists()
	await _test_audio_bus_effects()
	print("[DROP-TEST] DONE %d" % failures)
	get_tree().quit(1 if failures else 0)
