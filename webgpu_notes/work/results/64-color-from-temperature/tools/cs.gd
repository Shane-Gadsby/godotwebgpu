extends SceneTree
# Color.from_temperature() against camera-sim's CinemaSettings.light_color_for_temperature() (copied verbatim,
# without its final linear_to_srgb()).
static func cs(kelvin: float) -> Color:
	var t := clampf(kelvin, 1000.0, 15000.0)
	var t2 := t * t
	var u := (0.860117757 + 1.54118254e-4 * t + 1.28641212e-7 * t2) / (1.0 + 8.42420235e-4 * t + 7.08145163e-7 * t2)
	var v := (0.317398726 + 4.22806245e-5 * t + 4.20481691e-8 * t2) / (1.0 - 2.89741816e-5 * t + 1.61456053e-7 * t2)
	var d := 1.0 / (2.0 * u - 8.0 * v + 4.0)
	var x := 3.0 * u * d
	var y := 2.0 * v * d
	var X := x / y
	var Z := (1.0 - x - y) / y
	var r := maxf(3.2404542 * X - 1.5371385 - 0.4985314 * Z, 0.0)
	var g := maxf(-0.9692660 * X + 1.8760108 + 0.0415560 * Z, 0.0)
	var b := maxf(0.0556434 * X - 0.2040259 + 1.0572252 * Z, 0.0)
	var luminance := r * 0.2126 + g * 0.7152 + b * 0.0722
	return Color(r / luminance, g / luminance, b / luminance)

func _init() -> void:
	var worst := 0.0
	for k in [1000.0, 2000.0, 3200.0, 5600.0, 6500.0, 10000.0, 15000.0]:
		var a := Color.from_temperature(k)
		var e := cs(k)
		var err := maxf(absf(a.r - e.r), maxf(absf(a.g - e.g), absf(a.b - e.b)))
		worst = maxf(worst, err)
		var m := Color.from_temperature(k, Color.TEMPERATURE_NORMALIZATION_MAX_CHANNEL)
		print("%6d lum (%.7f, %.7f, %.7f) Y=%.8f  max (%.7f, %.7f, %.7f)  |diff vs camera-sim| %.9f" % [k, a.r, a.g, a.b, 0.2126 * a.r + 0.7152 * a.g + 0.0722 * a.b, m.r, m.g, m.b, err])
	var t := 1000.0
	while t <= 15000.0:
		var a := Color.from_temperature(t)
		var e := cs(t)
		worst = maxf(worst, maxf(absf(a.r - e.r), maxf(absf(a.g - e.g), absf(a.b - e.b))))
		t += 1.0
	print("worst over 1000..15000 step 1 K: %.9f" % worst)
	quit()
