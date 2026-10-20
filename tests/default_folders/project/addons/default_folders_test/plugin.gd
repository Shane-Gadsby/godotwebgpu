@tool
extends EditorPlugin
# Drives the editor's create and save dialogs the way a click does (OK pressed, check box toggled), then checks the
# folder each one proposed and what its "Make this the default folder" check box wrote to project.godot.
# Prints "[DF-TEST] <check> PASS|FAIL ..." per check and "[DF-TEST] DONE <failures>", then quits.

var failures := 0


func _enter_tree() -> void:
	if "--import" in OS.get_cmdline_args():
		return
	_run.call_deferred()


func _frames(count := 3) -> void:
	for i in count:
		await get_tree().process_frame


func _check(name: String, ok: bool, detail := "") -> void:
	print("[DF-TEST] %s %s %s" % [name, "PASS" if ok else "FAIL", detail])
	if not ok:
		failures += 1


func _find_all(node: Node, cls: String, out: Array) -> Array:
	if node.get_class() == cls:
		out.push_back(node)
	for child in node.get_children(true):
		_find_all(child, cls, out)
	return out


func _find(cls: String) -> Node:
	var found := _find_all(EditorInterface.get_base_control(), cls, [])
	return found[0] if not found.is_empty() else null


func _remember_box(dialog: Node) -> CheckBox:
	for box: CheckBox in _find_all(dialog, "CheckBox", []):
		if box.text.begins_with("Make this the default folder"):
			return box
	return null


func _line_edit_ending(dialog: Node, suffix: String) -> LineEdit:
	for edit: LineEdit in _find_all(dialog, "LineEdit", []):
		if edit.text.ends_with(suffix):
			return edit
	return null


func _set_path(edit: LineEdit, path: String) -> void:
	edit.text = path
	edit.text_changed.emit(path)


func _project_file_has(text: String) -> bool:
	return FileAccess.get_file_as_string("res://project.godot").contains(text)


func _visible_file_dialog(title: String) -> EditorFileDialog:
	for dialog: EditorFileDialog in _find_all(EditorInterface.get_base_control(), "EditorFileDialog", []):
		if dialog.visible and dialog.title == title:
			return dialog
	return null


func _menu_id(root: Node, text: String) -> Array:
	for menu: PopupMenu in _find_all(root, "PopupMenu", []):
		for i in menu.item_count:
			if menu.get_item_text(i) == text:
				return [menu, menu.get_item_id(i)]
	return []


# Opens the editor's shared Save As dialog through a menu item, as clicking it would.
func _open_save_dialog(root: Node, item: String, title: String) -> EditorFileDialog:
	var entry := _menu_id(root, item)
	if entry.is_empty():
		print("[DF-TEST] (menu item %s not found)" % item)
		return null
	entry[0].id_pressed.emit(entry[1])
	await _frames()
	return _visible_file_dialog(title)


func _save_in(dialog: EditorFileDialog, dir: String, file: String, remember: bool) -> void:
	DirAccess.make_dir_recursive_absolute(dir)
	dialog.current_dir = dir
	dialog.current_file = file
	if remember:
		for box: CheckBox in _find_all(dialog, "CheckBox", []):
			if box.text.begins_with("Make this the default folder"):
				box.button_pressed = true
	dialog.get_ok_button().pressed.emit()
	await _frames()


func _run() -> void:
	await _frames(10)
	_test_migration()
	await _test_script_dialog()
	await _test_shader_dialog()
	await _test_resource_save_as()
	await _test_scene_save_as()
	await _test_scene_create_dialog()
	print("[DF-TEST] DONE %d" % failures)
	get_tree().quit(1 if failures else 0)


func _test_migration() -> void:
	_check("legacy script folder moved", ProjectSettings.get_setting("editor/default_folders/scripts") == "res://legacy_scripts", str(ProjectSettings.get_setting("editor/default_folders/scripts")))
	_check("legacy setting removed", not ProjectSettings.has_setting("editor/script/default_script_folder"))


func _test_script_dialog() -> void:
	var dialog := get_script_create_dialog()
	dialog.config("Node", "res://somewhere/player.gd")
	dialog.popup_centered()
	await _frames()
	var path := _line_edit_ending(dialog, "player.gd")
	_check("script: default folder proposed", path != null and path.text == "res://legacy_scripts/player.gd", path.text if path else "no path field")
	var box := _remember_box(dialog)
	_check("script: check box shown, off", box != null and box.is_visible_in_tree() and not box.button_pressed)

	dialog.config("Node", "res://somewhere/player.gd", true, true, false)
	_check("script: use_default_folder=false keeps the path", path.text == "res://somewhere/player.gd", path.text)

	dialog.config("Node", "res://somewhere/enemy.gd")
	_set_path(path, "res://new_scripts/enemy.gd")
	box.button_pressed = true
	dialog.get_ok_button().pressed.emit()
	await _frames()
	_check("script: created", FileAccess.file_exists("res://new_scripts/enemy.gd"))
	_check("script: default folder set", ProjectSettings.get_setting("editor/default_folders/scripts") == "res://new_scripts", str(ProjectSettings.get_setting("editor/default_folders/scripts")))
	_check("script: project.godot saved", _project_file_has('default_folders/scripts="res://new_scripts"') and not _project_file_has("script/default_script_folder"))

	dialog.config("Node", "res://somewhere/boss.gd")
	dialog.popup_centered()
	await _frames()
	_check("script: new default proposed, box reset", path.text == "res://new_scripts/boss.gd" and not box.button_pressed, path.text)

	# Unticked: the default stays where it is.
	_set_path(path, "res://other_scripts/boss.gd")
	dialog.get_ok_button().pressed.emit()
	await _frames()
	_check("script: unticked leaves the default", FileAccess.file_exists("res://other_scripts/boss.gd") and ProjectSettings.get_setting("editor/default_folders/scripts") == "res://new_scripts")


func _test_shader_dialog() -> void:
	var dialog := _find("ShaderCreateDialog")
	if dialog == null:
		_check("shader: dialog found", false)
		return
	DirAccess.make_dir_recursive_absolute("res://shaders")
	ProjectSettings.set_setting("editor/default_folders/shaders", "res://shaders")
	dialog.config("res://elsewhere/water", false, false)
	dialog.popup_centered()
	await _frames()
	var path := _line_edit_ending(dialog, ".gdshader")
	_check("shader: default folder proposed", path != null and path.text == "res://shaders/water.gdshader", path.text if path else "no path field")
	var box := _remember_box(dialog)
	_check("shader: check box shown, off", box != null and box.is_visible_in_tree() and not box.button_pressed)

	_set_path(path, "res://fx/water.gdshader")
	box.button_pressed = true
	dialog.get_ok_button().pressed.emit()
	await _frames()
	_check("shader: created", FileAccess.file_exists("res://fx/water.gdshader"))
	_check("shader: default folder set", ProjectSettings.get_setting("editor/default_folders/shaders") == "res://fx", str(ProjectSettings.get_setting("editor/default_folders/shaders")))


func _test_resource_save_as() -> void:
	var inspector_dock := _find("InspectorDock")

	EditorInterface.edit_resource(StandardMaterial3D.new())
	await _frames()
	var dialog := await _open_save_dialog(inspector_dock, "Save Resource As...", "Save Resource As...")
	if dialog == null:
		_check("material: save dialog opened", false)
		return
	_check("material: default folder proposed", dialog.current_dir == "res://materials", dialog.current_dir)
	_check("material: check box offered", dialog.get_option_count() == 1 and dialog.get_option_name(0) == "Make this the default folder for materials", dialog.get_option_name(0) if dialog.get_option_count() else "none")
	await _save_in(dialog, "res://mats2", "red.tres", true)
	_check("material: saved", FileAccess.file_exists("res://mats2/red.tres"))
	_check("material: default folder set", ProjectSettings.get_setting("editor/default_folders/materials") == "res://mats2", str(ProjectSettings.get_setting("editor/default_folders/materials")))

	EditorInterface.edit_resource(Environment.new())
	await _frames()
	dialog = await _open_save_dialog(inspector_dock, "Save Resource As...", "Save Resource As...")
	_check("environment: settings folder proposed", dialog != null and dialog.current_dir == "res://settings", dialog.current_dir if dialog else "no dialog")
	_check("environment: check box is for settings, off", dialog != null and dialog.get_option_count() == 1 and dialog.get_option_name(0).ends_with("settings") and not dialog.get_selected_options().values()[0])
	# Saved without the box: the settings default does not move.
	await _save_in(dialog, "res://env", "env.tres", false)
	_check("environment: unticked leaves the default", FileAccess.file_exists("res://env/env.tres") and ProjectSettings.get_setting("editor/default_folders/settings") == "res://settings")

	EditorInterface.edit_resource(Curve.new())
	await _frames()
	dialog = await _open_save_dialog(inspector_dock, "Save Resource As...", "Save Resource As...")
	_check("curve: check box is for resources", dialog != null and dialog.get_option_count() == 1 and dialog.get_option_name(0).ends_with("resources"))
	if dialog:
		dialog.hide()


func _test_scene_save_as() -> void:
	var root := Node3D.new()
	root.name = "Level"
	EditorInterface.add_root_node(root)
	await _frames()
	var dialog := await _open_save_dialog(EditorInterface.get_base_control(), "Save Scene As...", "Save Scene As...")
	if dialog == null:
		_check("scene: save dialog opened", false)
		return
	_check("scene: check box is for scenes", dialog.get_option_count() == 1 and dialog.get_option_name(0).ends_with("scenes"))
	await _save_in(dialog, "res://levels", "level.tscn", true)
	_check("scene: saved", FileAccess.file_exists("res://levels/level.tscn"))
	_check("scene: default folder set", ProjectSettings.get_setting("editor/default_folders/scenes") == "res://levels", str(ProjectSettings.get_setting("editor/default_folders/scenes")))

	# The next new scene is proposed in the folder just made the default.
	var new_scene := _menu_id(EditorInterface.get_base_control(), "New Scene")
	if new_scene.is_empty():
		_check("scene: New Scene menu item found", false)
	else:
		new_scene[0].id_pressed.emit(new_scene[1])
		await _frames()
		var second := Node2D.new()
		second.name = "Second"
		EditorInterface.add_root_node(second)
		await _frames()
		dialog = await _open_save_dialog(EditorInterface.get_base_control(), "Save Scene As...", "Save Scene As...")
		_check("scene: default folder proposed", dialog != null and dialog.current_path == "res://levels/second.tscn", dialog.current_path if dialog else "no dialog")
		if dialog:
			dialog.hide()

	# The shared dialog drops the check box for its other uses.
	var entry := _menu_id(EditorInterface.get_base_control(), "Open Scene...")
	if not entry.is_empty():
		entry[0].id_pressed.emit(entry[1])
		await _frames()
		var open_dialog := _visible_file_dialog("Open Scene")
		_check("open scene: no check box", open_dialog != null and open_dialog.get_option_count() == 0)
		if open_dialog:
			open_dialog.hide()


func _test_scene_create_dialog() -> void:
	var dialog := _find("SceneCreateDialog")
	if dialog == null:
		_check("new scene: dialog found", false)
		return
	# Not bound to scripts: open it the way the FileSystem dock's "New Scene..." does (right click on the dock's
	# empty space, which targets res://), then fill it in.
	var dock := _find("FileSystemDock")
	var tree: Tree = _find_all(dock, "Tree", [])[0]
	tree.empty_clicked.emit(Vector2(4, 4), MOUSE_BUTTON_RIGHT)
	await _frames()
	var entry := _menu_id(dock, "New Scene...")
	if entry.is_empty():
		_check("new scene: menu item found", false)
		return
	entry[0].hide()
	entry[0].id_pressed.emit(entry[1])
	await _frames()
	var box := _remember_box(dialog)
	_check("new scene: check box shown, off", dialog.visible and box != null and box.is_visible_in_tree() and not box.button_pressed)
	var name_edit: LineEdit = null
	for edit: LineEdit in _find_all(dialog, "LineEdit", []):
		if edit.accessibility_name == "Scene Name:":
			name_edit = edit
			break
	_set_path(name_edit, "menu")
	box.button_pressed = true
	await _frames() # The dialog validates (and resolves the scene path) deferred.
	dialog.get_ok_button().pressed.emit()
	await _frames()
	_check("new scene: created", FileAccess.file_exists("res://menu.tscn"))
	_check("new scene: default folder set", ProjectSettings.get_setting("editor/default_folders/scenes") == "res://", str(ProjectSettings.get_setting("editor/default_folders/scenes")))
