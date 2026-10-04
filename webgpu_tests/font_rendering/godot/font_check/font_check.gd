extends Node2D
## Font rendering check — the scene half.
##
## Draws text in colors chosen so that a working glyph modulate and a broken one
## produce visibly different pixels, then prints [FontCheck] READY once the frame
## is stable. test_font_visual.mjs screenshots the canvas and does the asserting;
## nothing is read back here, because RenderingDeviceDriverWebGPU's
## texture_get_data() is a one-shot cache and a single texture_2d_get() can never
## return data on WebGPU (see webgpu_notes/HANDOFF.md 4.6).
##
## What it is guarding, concretely: both text servers used to decide whether a
## glyph carries its own color -- and so must not be tinted by the text color --
## by checking whether the atlas was RGBA8. On WebGPU monochrome atlases ARE
## RGBA8 (no texture swizzle to broadcast luminance), so that test matched every
## ordinary glyph, the modulate was dropped, and all text rendered white. White
## text looks identical either way, which is exactly how the regression survived
## its original "text renders correctly" verification -- so this scene renders
## text that is deliberately NOT white.
##
## Keep the two bands non-overlapping and keep the colors far apart; the runner
## addresses them by viewport rectangle.

const READY_AFTER_FRAMES := 30

# Must match REGIONS in test_font_visual.mjs. Viewport coordinates, 1280x720.
const BAND_FILL := Rect2(40, 80, 1200, 200)
const BAND_OUTLINE := Rect2(40, 360, 1200, 240)

const BACKDROP := Color(0.5, 0.5, 0.5)
const FILL_COLOR := Color(1, 0, 0) # pure red: nothing else in the scene is red
const OUTLINE_COLOR := Color(0, 0, 0) # pure black against a white fill

var _frames := 0
var _ready_printed := false


func _ready() -> void:
	print("[FontCheck] Starting font rendering check...")

	var layer := CanvasLayer.new()
	layer.layer = 100
	add_child(layer)

	var backdrop := ColorRect.new()
	backdrop.color = BACKDROP
	backdrop.set_anchors_preset(Control.PRESET_FULL_RECT)
	backdrop.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(backdrop)

	#Band 1: a non-white fill. Under the bug this renders white instead of red.
	layer.add_child(_make_label(BAND_FILL, FILL_COLOR, 0, Color(0, 0, 0, 0)))

	#Band 2: white fill with a thick dark outline -- the shape the original bug
	#report was actually seen through. Under the bug the outline renders white
	#too and the dark pixels disappear entirely.
	layer.add_child(_make_label(BAND_OUTLINE, Color(1, 1, 1), 24, OUTLINE_COLOR))

	print("[FontCheck] Bands built, settling for %d frames..." % READY_AFTER_FRAMES)


## A Label is deliberately used with the default theme font: it must be a plain
## FreeType-rasterized dynamic font, NOT MSDF. The draw-time check this test
## guards is skipped outright for MSDF fonts (`!fd->msdf`), so an MSDF label here
## would render correctly even with the bug present and the test would pass for
## the wrong reason.
func _make_label(p_rect: Rect2, p_color: Color, p_outline: int, p_outline_color: Color) -> Label:
	var label := Label.new()
	#"M" is the heaviest glyph in the default font, so each band has a large
	#number of fully-covered interior pixels rather than only antialiased edges.
	label.text = "MMMMMMMM"
	label.position = p_rect.position
	label.size = p_rect.size
	label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	label.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_size_override("font_size", 140)
	label.add_theme_color_override("font_color", p_color)
	if p_outline > 0:
		label.add_theme_constant_override("outline_size", p_outline)
		label.add_theme_color_override("font_outline_color", p_outline_color)
	return label


func _process(_delta: float) -> void:
	if _ready_printed:
		return
	_frames += 1
	if _frames < READY_AFTER_FRAMES:
		return
	_ready_printed = true
	#The glyph atlas upload is deferred to frame_pre_draw on WebGPU (Task 35), so
	#READY must only be printed once enough frames have actually been presented
	#for the atlas to exist on the GPU -- not merely once the nodes are built.
	print("[FontCheck] READY")
