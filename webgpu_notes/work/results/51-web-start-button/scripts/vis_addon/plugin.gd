@tool
extends EditorPlugin

class VisExport extends EditorExportPlugin:
	func _get_name() -> String:
		return "vis"
	func _export_begin(_features, _debug, _path, _flags) -> void:
		# EditorExportPreset._get_property_list() is what the export dialog's inspector shows:
		# it drops every option get_export_option_visibility() hides.
		var shown := []
		for p in get_export_preset().get_property_list():
			if String(p.name).begins_with("html/start_button/"):
				shown.append(String(p.name).trim_prefix("html/start_button/"))
		print("VISIBLE_START_OPTIONS ", ",".join(shown))

var exp := VisExport.new()

func _enter_tree() -> void:
	add_export_plugin(exp)

func _exit_tree() -> void:
	remove_export_plugin(exp)
