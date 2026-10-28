# Loaded only by the post-change editors (the pre-change one has no Color.from_temperature()).
static func ft(t: float, n: int) -> Color:
	return Color.from_temperature(t, n)
