# Editor drag-and-drop onto collections

`./run.sh` (needs the editor in `bin/` and `xvfb-run`) checks that every editor list accepts what is dragged from the
FileSystem dock (one or many files or folders), from a resource picker (a resource) or from the Scene dock (one or
many nodes), the same way. The rules live in one place, `editor/inspector/editor_collection_drop.{h,cpp}`; each
list appends what was dropped in one undoable action:

| Where | What a drop adds |
|---|---|
| `Array` / `Array[T]` / `PackedStringArray` (`EditorPropertyArray`) | an element per item: resources for resource arrays, nodes for node arrays, relative NodePaths for NodePath arrays, paths for String arrays (a `uid://` for `@export_file`, honoring its filters), resources or NodePaths for untyped arrays |
| `Dictionary` / `Dictionary[K, V]` (`EditorPropertyDictionary`) | an entry per item: the item as value keyed by its name (`"kick"`, then `"kick 2"`) or the next integer; or, when the values hold plain data (`Dictionary[AudioStream, float]`), the item as key with a default value |
| property arrays (`EditorInspectorArray`: AudioStreamRandomizer/Playlist/Synchronized/Interactive streams, MultiplayerSpawner scenes, ItemList/OptionButton/TabBar items, ...) | an element per item, in the element property that takes it (a stream, a scene path, an icon), with a `name`/`text` property set to the item's name; drop on the header, the list or "Add Element" |
| Project Settings > Autoload | an autoload per script or scene, named like the file dialog names it |
| Project Settings > Localization (translations, remaps, remap options, POT sources) | what the list's file dialog would add |
| Audio panel, a bus's effects | `AudioEffect` resources, appended |
| Animation Library dialog | `AnimationLibrary` files add libraries; `Animation` files dropped on a library add to it |

How it drives the editor: an editor plugin (`project/addons/drop_test`) edits real objects, starts a real drag with
`Control.force_drag()` and pushes the mouse motion and release into the editor's viewport, which is exactly what a
drag from a dock does from that point on. It runs windowed under Xvfb, because `--headless` gives the editor a 2x2
viewport no drop can land in, and on a scratch copy of the project, because the Project Settings lists save
`project.godot`. `DROP_TEST_DEBUG=1 ./run.sh` prints each drop's target and what the mouse hovered.
The Animation Library dialog is not covered (it is only reachable through the AnimationPlayer editor's menu).
