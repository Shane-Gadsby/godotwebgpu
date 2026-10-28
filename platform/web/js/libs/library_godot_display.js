/**************************************************************************/
/*  library_godot_display.js                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

const GodotDisplayVK = {

	$GodotDisplayVK__deps: ['$GodotRuntime', '$GodotConfig', '$GodotEventListeners', '$GodotInput'],
	$GodotDisplayVK__postset: 'GodotOS.atexit(function(resolve, reject) { GodotDisplayVK.clear(); resolve(); });',
	$GodotDisplayVK: {
		textinput: null,
		textarea: null,

		available: function () {
			return GodotConfig.virtual_keyboard && 'ontouchstart' in window;
		},

		init: function (input_cb) {
			function create(what) {
				const elem = document.createElement(what);
				elem.style.display = 'none';
				elem.style.position = 'absolute';
				elem.style.zIndex = '-1';
				elem.style.background = 'transparent';
				elem.style.padding = '0px';
				elem.style.margin = '0px';
				elem.style.overflow = 'hidden';
				elem.style.width = '0px';
				elem.style.height = '0px';
				elem.style.border = '0px';
				elem.style.outline = 'none';
				elem.readonly = true;
				elem.disabled = true;
				GodotEventListeners.add(elem, 'input', function (evt) {
					const c_str = GodotRuntime.allocString(elem.value);
					input_cb(c_str, elem.selectionEnd);
					GodotRuntime.free(c_str);
				}, false);
				if (what === 'input') {
					// Handling the "Enter" key.
					const onKey = (pEvent, pEventName) => {
						if (pEvent.key !== 'Enter') {
							return;
						}
						GodotInput.onKeyEvent(pEventName === 'keydown', pEvent);
					};
					GodotEventListeners.add(elem, 'keydown', (pEvent) => onKey(pEvent, 'keydown'), false);
					GodotEventListeners.add(elem, 'keyup', (pEvent) => onKey(pEvent, 'keyup'), false);
				}
				GodotEventListeners.add(elem, 'blur', function (evt) {
					elem.style.display = 'none';
					elem.readonly = true;
					elem.disabled = true;
				}, false);
				GodotConfig.canvas.insertAdjacentElement('beforebegin', elem);
				return elem;
			}
			GodotDisplayVK.textinput = create('input');
			GodotDisplayVK.textarea = create('textarea');
			GodotDisplayVK.updateSize();
		},
		show: function (text, type, start, end) {
			if (!GodotDisplayVK.textinput || !GodotDisplayVK.textarea) {
				return;
			}
			if (GodotDisplayVK.textinput.style.display !== '' || GodotDisplayVK.textarea.style.display !== '') {
				GodotDisplayVK.hide();
			}
			GodotDisplayVK.updateSize();

			let elem = GodotDisplayVK.textinput;
			switch (type) {
			case 0: // DisplayServerEnums::KEYBOARD_TYPE_DEFAULT
				elem.type = 'text';
				elem.inputmode = '';
				break;
			case 1: // DisplayServerEnums::KEYBOARD_TYPE_MULTILINE
				elem = GodotDisplayVK.textarea;
				break;
			case 2: // DisplayServerEnums::KEYBOARD_TYPE_NUMBER
				elem.type = 'text';
				elem.inputmode = 'numeric';
				break;
			case 3: // DisplayServerEnums::KEYBOARD_TYPE_NUMBER_DECIMAL
				elem.type = 'text';
				elem.inputmode = 'decimal';
				break;
			case 4: // DisplayServerEnums::KEYBOARD_TYPE_PHONE
				elem.type = 'tel';
				elem.inputmode = '';
				break;
			case 5: // DisplayServerEnums::KEYBOARD_TYPE_EMAIL_ADDRESS
				elem.type = 'email';
				elem.inputmode = '';
				break;
			case 6: // DisplayServerEnums::KEYBOARD_TYPE_PASSWORD
				elem.type = 'password';
				elem.inputmode = '';
				break;
			case 7: // DisplayServerEnums::KEYBOARD_TYPE_URL
				elem.type = 'url';
				elem.inputmode = '';
				break;
			default:
				elem.type = 'text';
				elem.inputmode = '';
				break;
			}

			elem.readonly = false;
			elem.disabled = false;
			elem.value = text;
			elem.style.display = 'block';
			elem.focus();
			elem.setSelectionRange(start, end);
		},
		hide: function () {
			if (!GodotDisplayVK.textinput || !GodotDisplayVK.textarea) {
				return;
			}
			[GodotDisplayVK.textinput, GodotDisplayVK.textarea].forEach(function (elem) {
				elem.blur();
				elem.style.display = 'none';
				elem.value = '';
			});
		},
		updateSize: function () {
			if (!GodotDisplayVK.textinput || !GodotDisplayVK.textarea) {
				return;
			}
			const rect = GodotConfig.canvas.getBoundingClientRect();
			function update(elem) {
				elem.style.left = `${rect.left}px`;
				elem.style.top = `${rect.top}px`;
				elem.style.width = `${rect.width}px`;
				elem.style.height = `${rect.height}px`;
			}
			update(GodotDisplayVK.textinput);
			update(GodotDisplayVK.textarea);
		},
		clear: function () {
			if (GodotDisplayVK.textinput) {
				GodotDisplayVK.textinput.remove();
				GodotDisplayVK.textinput = null;
			}
			if (GodotDisplayVK.textarea) {
				GodotDisplayVK.textarea.remove();
				GodotDisplayVK.textarea = null;
			}
		},
	},
};
mergeInto(LibraryManager.library, GodotDisplayVK);

/*
 * Display server cursor helper.
 * Keeps track of cursor status and custom shapes.
 */
const GodotDisplayCursor = {
	$GodotDisplayCursor__deps: ['$GodotOS', '$GodotConfig'],
	$GodotDisplayCursor__postset: 'GodotOS.atexit(function(resolve, reject) { GodotDisplayCursor.clear(); resolve(); });',
	$GodotDisplayCursor: {
		shape: 'default',
		visible: true,
		cursors: {},
		set_style: function (style) {
			GodotConfig.canvas.style.cursor = style;
		},
		set_shape: function (shape) {
			GodotDisplayCursor.shape = shape;
			let css = shape;
			if (shape in GodotDisplayCursor.cursors) {
				const c = GodotDisplayCursor.cursors[shape];
				css = `url("${c.url}") ${c.x} ${c.y}, default`;
			}
			if (GodotDisplayCursor.visible) {
				GodotDisplayCursor.set_style(css);
			}
		},
		clear: function () {
			GodotDisplayCursor.set_style('');
			GodotDisplayCursor.shape = 'default';
			GodotDisplayCursor.visible = true;
			Object.keys(GodotDisplayCursor.cursors).forEach(function (key) {
				URL.revokeObjectURL(GodotDisplayCursor.cursors[key]);
				delete GodotDisplayCursor.cursors[key];
			});
		},
		lockPointer: function () {
			const canvas = GodotConfig.canvas;
			if (!canvas.requestPointerLock) {
				return;
			}
			try {
				const promise = canvas.requestPointerLock();
				// Chrome 113+ returns a promise, older browsers and Safari return undefined.
				// A rejection is routine (no user gesture, a lock request during the
				// browser's own unlock cooldown, or the canvas no longer being in the
				// document), so swallow it instead of letting it surface as an
				// unhandled rejection.
				if (promise) {
					promise.catch(function () {
						// nothing to do.
					});
				}
			} catch (e) {
				// nothing to do.
			}
		},
		releasePointer: function () {
			if (!document.exitPointerLock) {
				return;
			}
			try {
				document.exitPointerLock();
			} catch (e) {
				// nothing to do.
			}
		},
		isPointerLocked: function () {
			return document.pointerLockElement === GodotConfig.canvas;
		},
	},
};
mergeInto(LibraryManager.library, GodotDisplayCursor);

const GodotDisplayScreen = {
	$GodotDisplayScreen__deps: ['$GodotConfig', '$GodotOS', '$GodotRuntime', '$GodotEventListeners', '$GL', 'emscripten_webgl_get_current_context'],
	$GodotDisplayScreen: {
		desired_size: [0, 0],
		// Pixel size of a canvas whose control was transferred to an OffscreenCanvas (the web render
		// thread, webgpu_notes/TASKS.md 2.5). The element's width/height can no longer be set and stop
		// tracking the real size, so it is kept here; the OffscreenCanvas itself is resized on its own
		// thread through emscripten_set_canvas_element_size(). null until the first resize.
		transferred_size: null,
		canvasSize: function () {
			const canvas = GodotConfig.canvas;
			if (canvas.controlTransferredOffscreen && GodotDisplayScreen.transferred_size) {
				return GodotDisplayScreen.transferred_size;
			}
			return [canvas.width, canvas.height];
		},
		hidpi: true,
		getPixelRatio: function () {
			return GodotDisplayScreen.hidpi ? window.devicePixelRatio || 1 : 1;
		},
		isFullscreen: function () {
			const elem = document.fullscreenElement || document.mozFullscreenElement
				|| document.webkitFullscreenElement || document.msFullscreenElement;
			if (elem) {
				return elem === GodotConfig.canvas;
			}
			if (GodotDisplayScreen.embedHoldsFullscreen()) {
				return true;
			}
			// But maybe knowing the element is not supported.
			return document.fullscreen || document.mozFullScreen
				|| document.webkitIsFullscreen;
		},
		hasFullscreen: function () {
			return document.fullscreenEnabled || document.mozFullScreenEnabled
				|| document.webkitFullscreenEnabled;
		},
		requestFullscreen: function () {
			if (!GodotDisplayScreen.hasFullscreen()) {
				return 1;
			}
			const canvas = GodotConfig.canvas;
			try {
				const promise = (canvas.requestFullscreen || canvas.msRequestFullscreen
					|| canvas.mozRequestFullScreen || canvas.mozRequestFullscreen
					|| canvas.webkitRequestFullscreen
				).call(canvas);
				// Some browsers (Safari) return undefined.
				// For the standard ones, we need to catch it.
				if (promise) {
					promise.catch(function () {
						// nothing to do.
					});
				}
			} catch (e) {
				return 1;
			}
			return 0;
		},
		exitFullscreen: function () {
			if (!GodotDisplayScreen.isFullscreen()) {
				return 0;
			}
			if (GodotDisplayScreen.embedHoldsFullscreen()) {
				// document.exitFullscreen() would reject: nothing in this document is fullscreen.
				GodotDisplayScreen.embedPost('godot:exit-fullscreen');
				return 2; // Pending until the parent's {on: false}.
			}
			try {
				const promise = document.exitFullscreen();
				if (promise) {
					promise.catch(function () {
						// nothing to do.
					});
				}
			} catch (e) {
				return 1;
			}
			return 0;
		},
		_updateGL: function () {
			const gl_context_handle = _emscripten_webgl_get_current_context();
			const gl = GL.getContext(gl_context_handle);
			if (gl) {
				GL.resizeOffscreenFramebuffer(gl);
			}
		},
		updateSize: function () {
			const isFullscreen = GodotDisplayScreen.isFullscreen();
			const wantsFullWindow = GodotConfig.canvas_resize_policy === 2;
			const noResize = GodotConfig.canvas_resize_policy === 0;
			const dWidth = GodotDisplayScreen.desired_size[0];
			const dHeight = GodotDisplayScreen.desired_size[1];
			const canvas = GodotConfig.canvas;
			let width = dWidth;
			let height = dHeight;
			if (noResize) {
				// Don't resize canvas, just update GL if needed.
				const current = GodotDisplayScreen.canvasSize();
				if (current[0] !== width || current[1] !== height) {
					GodotDisplayScreen.desired_size = current;
					GodotDisplayScreen._updateGL();
					return 1;
				}
				return 0;
			}
			const scale = GodotDisplayScreen.getPixelRatio();
			if (isFullscreen || wantsFullWindow) {
				// We need to match screen size.
				width = Math.floor(window.innerWidth * scale);
				height = Math.floor(window.innerHeight * scale);
			}
			const csw = `${Math.floor(width / scale)}px`;
			const csh = `${Math.floor(height / scale)}px`;
			if (canvas.controlTransferredOffscreen) {
				const current = GodotDisplayScreen.canvasSize();
				if (canvas.style.width !== csw || canvas.style.height !== csh || current[0] !== width || current[1] !== height) {
					GodotDisplayScreen.transferred_size = [width, height];
					canvas.style.width = csw;
					canvas.style.height = csh;
					return 1;
				}
				return 0;
			}
			if (canvas.style.width !== csw || canvas.style.height !== csh || canvas.width !== width || canvas.height !== height) {
				// Size doesn't match.
				// Resize canvas, set correct CSS pixel size, update GL.
				canvas.width = width;
				canvas.height = height;
				canvas.style.width = csw;
				canvas.style.height = csh;
				GodotDisplayScreen._updateGL();
				return 1;
			}
			return 0;
		},

		/*
		 * Fullscreen held by the page that iframes the game (export option html/embed_fullscreen/*).
		 * A click in a parent on another origin is not user activation inside the iframe, so the
		 * parent cannot put the canvas in fullscreen; it fullscreens the iframe instead and says so:
		 *   parent -> game  {type: 'godot:fullscreen', on}  on every fullscreenchange, and in answer to
		 *   game -> parent  {type: 'godot:ready'}           posted once the engine listens;
		 *   game -> parent  {type: 'godot:exit-fullscreen'} window_set_mode(WINDOWED) while it holds it;
		 *   game -> parent  {type: 'godot:request-fullscreen'} window_set_mode(FULLSCREEN), see requestEmbedFullscreen.
		 * While the parent holds it, isFullscreen() is true, so updateSize() sizes the canvas to the
		 * iframe (tracking it every frame while it grows) and window_get_mode() reports FULLSCREEN.
		 * Only messages from window.parent count, and only from allowedOrigins when that is set.
		 * The game's own canvas fullscreen also makes the parent see its iframe as fullscreen; that
		 * echo is ignored (echo) and the canvas path handles it as before.
		 */
		embed: null, // null unless enabled and iframed: { allowed, origin, parent_on, echo, reported, request_timer }
		fullscreen_cb: null, // The C++ fullscreen callback (godot_js_display_fullscreen_cb).
		documentFullscreenElement: function () {
			return document.fullscreenElement || document.mozFullscreenElement
				|| document.webkitFullscreenElement || document.msFullscreenElement || null;
		},
		embedHoldsFullscreen: function () {
			const embed = GodotDisplayScreen.embed;
			return !!embed && embed.parent_on && !embed.echo && !GodotDisplayScreen.documentFullscreenElement();
		},
		embedPost: function (p_type) {
			const embed = GodotDisplayScreen.embed;
			// The origin the parent's last accepted message came from: a parent that navigated away
			// (or one that never spoke) gets nothing. A sandboxed parent's origin is 'null', which
			// postMessage() cannot target.
			if (!embed || !embed.origin) {
				return false;
			}
			try {
				window.parent.postMessage({ 'type': p_type }, embed.origin === 'null' ? '*' : embed.origin);
			} catch (e) {
				return false;
			}
			return true;
		},
		reportFullscreen: function () {
			const embed = GodotDisplayScreen.embed;
			const fullscreen = !!GodotDisplayScreen.isFullscreen();
			if (embed) {
				if (fullscreen === embed.reported) {
					return;
				}
				embed.reported = fullscreen;
			}
			if (GodotDisplayScreen.fullscreen_cb) {
				GodotDisplayScreen.fullscreen_cb(fullscreen ? 1 : 0);
			}
		},
		initEmbed: function () {
			const opts = GodotConfig.embed_fullscreen;
			if (!opts || !opts['enabled'] || window.parent === window) {
				return; // Off, or not in a frame: no listener at all.
			}
			const allowed = opts['allowedOrigins'];
			const embed = {
				allowed: Array.isArray(allowed) && allowed.length ? allowed : null,
				origin: null,
				parent_on: false,
				echo: false,
				reported: !!GodotDisplayScreen.isFullscreen(),
				request_timer: null,
			};
			GodotDisplayScreen.embed = embed;
			GodotEventListeners.add(window, 'message', function (event) {
				if (event.source !== window.parent || window.parent === window) {
					return;
				}
				if (embed.allowed && embed.allowed.indexOf(event.origin) === -1) {
					return;
				}
				const data = event.data;
				if (!data || typeof data !== 'object' || data['type'] !== 'godot:fullscreen') {
					return;
				}
				embed.origin = event.origin;
				const on = !!data['on'];
				if (on && !embed.parent_on && GodotDisplayScreen.documentFullscreenElement()) {
					embed.echo = true; // The parent only reports the canvas fullscreen we asked for.
				} else if (!on) {
					embed.echo = false;
				}
				embed.parent_on = on;
				if (on) {
					GodotDisplayScreen.first_tap_done = true;
					if (embed.request_timer !== null) {
						clearTimeout(embed.request_timer);
						embed.request_timer = null;
					}
				}
				GodotDisplayScreen.reportFullscreen();
			}, false);
			GodotEventListeners.add(document, 'fullscreenchange', function () {
				// The canvas went fullscreen on its own: the parent's {on: true} that follows is its echo.
				if (GodotDisplayScreen.documentFullscreenElement() && !embed.parent_on) {
					embed.echo = true;
				}
				GodotDisplayScreen.reportFullscreen();
			}, false);
			// Carries no data, so any origin may hear it; the parent answers with the current state,
			// which picks up a fullscreen entered on the start screen or while loading.
			window.parent.postMessage({ 'type': 'godot:ready' }, '*');
		},
		/*
		 * window_set_mode(FULLSCREEN) while embedded: ask the parent to fullscreen the iframe, so its
		 * own page state (button, fullscreenchange) stays consistent. The click inside the iframe that
		 * led here is user activation for the parent too (activation propagates to ancestor frames).
		 * If no {on: true} comes back within a second (a parent without that handler), the canvas
		 * request runs as before; a request that failed in the parent did not consume the activation.
		 */
		requestEmbedFullscreen: function () {
			const embed = GodotDisplayScreen.embed;
			if (!embed || !embed.origin || GodotDisplayScreen.isFullscreen() || GodotDisplayScreen.documentFullscreenElement()) {
				return GodotDisplayScreen.requestFullscreen();
			}
			if (!GodotDisplayScreen.embedPost('godot:request-fullscreen')) {
				return GodotDisplayScreen.requestFullscreen();
			}
			if (embed.request_timer === null) {
				embed.request_timer = setTimeout(function () {
					embed.request_timer = null;
					if (!embed.parent_on) {
						GodotDisplayScreen.requestFullscreen();
					}
				}, 1000);
			}
			return 0;
		},

		/*
		 * Screen Wake Lock (screen_set_keep_on) and orientation lock (screen_set_orientation).
		 * Both are requests the browser may refuse for reasons that are normal, not errors: no API
		 * (an older browser, an insecure context), a hidden page, a desktop that cannot rotate, an
		 * orientation lock outside fullscreen. So a refusal is only reported with --verbose, and the
		 * request is retried when the page becomes visible again or enters fullscreen.
		 * Members of these APIs are quoted so the closure compiler (use_closure_compiler=yes) keeps
		 * their names: its externs predate the Screen Wake Lock API.
		 */
		verbose: false,
		log: function (p_msg) {
			if (GodotDisplayScreen.verbose) {
				GodotRuntime.print(p_msg);
			}
		},
		init: function (p_verbose) {
			GodotDisplayScreen.verbose = !!p_verbose;
			// The browser releases a wake lock whenever the page is hidden, and never gives it back.
			GodotEventListeners.add(document, 'visibilitychange', function () {
				if (document.visibilityState === 'visible') {
					GodotDisplayScreen.acquireWakeLock();
				}
			}, false);
			// Chrome for Android only allows an orientation lock in fullscreen (or an installed
			// PWA), and drops it when fullscreen ends, so it is applied again on every entry.
			// Any element counts: a custom shell may put a container in fullscreen, not the canvas.
			const fullscreen_cb = function () {
				if (GodotDisplayScreen.documentIsFullscreen()) {
					GodotDisplayScreen.first_tap_done = true;
					GodotDisplayScreen.applyOrientation();
				}
			};
			GodotEventListeners.add(document, 'fullscreenchange', fullscreen_cb, false);
			GodotDisplayScreen.initEmbed();
			// Fullscreen on the first tap (export option html/fullscreen_on_first_tap), so a phone gets the
			// whole screen and the orientation lock without game code. Requested on touchend: the end of a
			// touch is user activation, its start is not, so Chrome refuses a request made on touchstart.
			// Only where the primary pointer is a touchscreen; tried again on later taps until it succeeds,
			// never again once the page has been in fullscreen (the player may have left it on purpose).
			if (GodotConfig.fullscreen_on_first_tap && GodotDisplayScreen.hasFullscreen() && typeof window.matchMedia === 'function'
				&& window.matchMedia('(pointer: coarse)').matches) {
				GodotEventListeners.add(GodotConfig.canvas, 'touchend', function () {
					if (!GodotDisplayScreen.first_tap_done && !GodotDisplayScreen.documentIsFullscreen() && !GodotDisplayScreen.embedHoldsFullscreen()) {
						GodotDisplayScreen.requestFullscreen();
					}
				}, false);
			}
			GodotOS.atexit(function (resolve, reject) {
				const embed = GodotDisplayScreen.embed;
				if (embed && embed.request_timer !== null) {
					clearTimeout(embed.request_timer);
				}
				GodotDisplayScreen.embed = null; // Its listeners go with GodotEventListeners.clear().
				GodotDisplayScreen.fullscreen_cb = null;
				GodotDisplayScreen.keep_on = false;
				GodotDisplayScreen.releaseWakeLock();
				resolve();
			});
		},

		first_tap_done: false,
		keep_on: false,
		wake_lock: null, // The WakeLockSentinel while the lock is held.
		wake_lock_pending: false,
		hasWakeLock: function () {
			return typeof navigator !== 'undefined' && !!navigator['wakeLock']
				&& typeof navigator['wakeLock']['request'] === 'function';
		},
		acquireWakeLock: function () {
			const self = GodotDisplayScreen;
			if (!self.keep_on || self.wake_lock || self.wake_lock_pending || !self.hasWakeLock()) {
				return;
			}
			if (document.visibilityState !== 'visible') {
				return; // A hidden page is refused; visibilitychange asks again.
			}
			self.wake_lock_pending = true;
			navigator['wakeLock']['request']('screen').then(function (sentinel) {
				self.wake_lock_pending = false;
				if (!self.keep_on) { // Turned off while the request was pending.
					sentinel['release']().catch(function () {});
					return;
				}
				self.wake_lock = sentinel;
				sentinel.addEventListener('release', function () {
					if (self.wake_lock === sentinel) {
						self.wake_lock = null;
					}
				});
				self.log('Screen wake lock acquired.');
			}, function (err) {
				self.wake_lock_pending = false;
				self.log(`Screen wake lock not granted: ${err && err.name}: ${err && err.message}`);
			});
		},
		releaseWakeLock: function () {
			const sentinel = GodotDisplayScreen.wake_lock;
			GodotDisplayScreen.wake_lock = null;
			if (sentinel) {
				sentinel['release']().catch(function () {});
			}
		},
		setKeepOn: function (p_enable) {
			GodotDisplayScreen.keep_on = !!p_enable;
			if (GodotDisplayScreen.keep_on) {
				GodotDisplayScreen.acquireWakeLock();
			} else {
				GodotDisplayScreen.releaseWakeLock();
			}
		},

		// DisplayServer::ScreenOrientation -> OrientationLockType. SCREEN_SENSOR (6) unlocks.
		orientation_types: ['landscape-primary', 'portrait-primary', 'landscape-secondary', 'portrait-secondary', 'landscape', 'portrait'],
		orientation: -1, // The requested ScreenOrientation, -1 before the first request.
		hasOrientationLock: function () {
			return typeof screen !== 'undefined' && !!screen['orientation']
				&& typeof screen['orientation']['lock'] === 'function';
		},
		documentIsFullscreen: function () {
			return !!(document.fullscreenElement || document.webkitFullscreenElement);
		},
		canLockOrientation: function () {
			// Desktop browsers expose lock() but always reject it (no screen to rotate), so only a
			// device with a touchscreen as a pointer is reported as supporting FEATURE_ORIENTATION.
			return GodotDisplayScreen.hasOrientationLock() && typeof window.matchMedia === 'function'
				&& window.matchMedia('(any-pointer: coarse)').matches;
		},
		applyOrientation: function () {
			const self = GodotDisplayScreen;
			if (self.orientation < 0 || !self.hasOrientationLock()) {
				return;
			}
			const type = self.orientation_types[self.orientation];
			if (!type) {
				try {
					screen['orientation']['unlock']();
				} catch (e) {
					// Nothing locked, or nothing to unlock.
				}
				return;
			}
			let promise;
			try {
				promise = screen['orientation']['lock'](type);
			} catch (e) {
				self.log(`Screen orientation lock (${type}) not available: ${e.name}: ${e.message}`);
				return;
			}
			if (promise) {
				promise.then(function () {
					self.log(`Screen orientation locked to ${type}.`);
				}, function (err) {
					self.log(`Screen orientation lock (${type}) not granted${self.documentIsFullscreen() ? '' : ' outside fullscreen'}: ${err && err.name}: ${err && err.message}`);
				});
			}
		},
		setOrientation: function (p_orientation) {
			GodotDisplayScreen.orientation = p_orientation;
			GodotDisplayScreen.applyOrientation();
		},
	},
};
mergeInto(LibraryManager.library, GodotDisplayScreen);

/**
 * Display server interface.
 *
 * Exposes all the functions needed by DisplayServer implementation.
 */
const GodotDisplay = {
	$GodotDisplay__deps: ['$GodotConfig', '$GodotRuntime', '$GodotDisplayCursor', '$GodotEventListeners', '$GodotDisplayScreen', '$GodotDisplayVK'],
	$GodotDisplay: {
		window_icon: '',
		getDPI: function () {
			// devicePixelRatio is given in dppx
			// https://drafts.csswg.org/css-values/#resolution
			// > due to the 1:96 fixed ratio of CSS *in* to CSS *px*, 1dppx is equivalent to 96dpi.
			const dpi = Math.round(window.devicePixelRatio * 96);
			return dpi >= 96 ? dpi : 96;
		},
	},

	godot_js_display_is_swap_ok_cancel__proxy: 'sync',
	godot_js_display_is_swap_ok_cancel__sig: 'i',
	godot_js_display_is_swap_ok_cancel: function () {
		const win = (['Windows', 'Win64', 'Win32', 'WinCE']);
		const plat = navigator.platform || '';
		if (win.indexOf(plat) !== -1) {
			return 1;
		}
		return 0;
	},

	godot_js_tts_is_speaking__proxy: 'sync',
	godot_js_tts_is_speaking__sig: 'i',
	godot_js_tts_is_speaking: function () {
		return window.speechSynthesis.speaking;
	},

	godot_js_tts_is_paused__proxy: 'sync',
	godot_js_tts_is_paused__sig: 'i',
	godot_js_tts_is_paused: function () {
		return window.speechSynthesis.paused;
	},

	godot_js_tts_get_voices__proxy: 'sync',
	godot_js_tts_get_voices__sig: 'vi',
	godot_js_tts_get_voices: function (p_callback) {
		const func = GodotRuntime.get_func(p_callback);
		try {
			const arr = [];
			const voices = window.speechSynthesis.getVoices();
			for (let i = 0; i < voices.length; i++) {
				arr.push(`${voices[i].lang};${voices[i].name}`);
			}
			const c_ptr = GodotRuntime.allocStringArray(arr);
			func(arr.length, c_ptr);
			GodotRuntime.freeStringArray(c_ptr, arr.length);
		} catch (e) {
			// Fail graciously.
		}
	},

	godot_js_tts_speak__proxy: 'sync',
	godot_js_tts_speak__sig: 'viiiffii',
	godot_js_tts_speak: function (p_text, p_voice, p_volume, p_pitch, p_rate, p_utterance_id, p_callback) {
		const func = GodotRuntime.get_func(p_callback);

		function listener_end(evt) {
			evt.currentTarget.cb(1 /* DisplayServerEnums::TTS_UTTERANCE_ENDED */, evt.currentTarget.id, 0);
		}

		function listener_start(evt) {
			evt.currentTarget.cb(0 /* DisplayServerEnums::TTS_UTTERANCE_STARTED */, evt.currentTarget.id, 0);
		}

		function listener_error(evt) {
			evt.currentTarget.cb(2 /* DisplayServerEnums::TTS_UTTERANCE_CANCELED */, evt.currentTarget.id, 0);
		}

		function listener_bound(evt) {
			evt.currentTarget.cb(3 /* DisplayServerEnums::TTS_UTTERANCE_BOUNDARY */, evt.currentTarget.id, evt.charIndex);
		}

		const utterance = new SpeechSynthesisUtterance(GodotRuntime.parseString(p_text));
		utterance.rate = p_rate;
		utterance.pitch = p_pitch;
		utterance.volume = p_volume / 100.0;
		utterance.addEventListener('end', listener_end);
		utterance.addEventListener('start', listener_start);
		utterance.addEventListener('error', listener_error);
		utterance.addEventListener('boundary', listener_bound);
		utterance.id = p_utterance_id;
		utterance.cb = func;
		const voice = GodotRuntime.parseString(p_voice);
		const voices = window.speechSynthesis.getVoices();
		for (let i = 0; i < voices.length; i++) {
			if (voices[i].name === voice) {
				utterance.voice = voices[i];
				break;
			}
		}
		window.speechSynthesis.resume();
		window.speechSynthesis.speak(utterance);
	},

	godot_js_tts_pause__proxy: 'sync',
	godot_js_tts_pause__sig: 'v',
	godot_js_tts_pause: function () {
		window.speechSynthesis.pause();
	},

	godot_js_tts_resume__proxy: 'sync',
	godot_js_tts_resume__sig: 'v',
	godot_js_tts_resume: function () {
		window.speechSynthesis.resume();
	},

	godot_js_tts_stop__proxy: 'sync',
	godot_js_tts_stop__sig: 'v',
	godot_js_tts_stop: function () {
		window.speechSynthesis.cancel();
		window.speechSynthesis.resume();
	},

	godot_js_display_alert__proxy: 'sync',
	godot_js_display_alert__sig: 'vi',
	godot_js_display_alert: function (p_text) {
		window.alert(GodotRuntime.parseString(p_text)); // eslint-disable-line no-alert
	},

	godot_js_display_screen_dpi_get__proxy: 'sync',
	godot_js_display_screen_dpi_get__sig: 'i',
	godot_js_display_screen_dpi_get: function () {
		return GodotDisplay.getDPI();
	},

	godot_js_display_pixel_ratio_get__proxy: 'sync',
	godot_js_display_pixel_ratio_get__sig: 'f',
	godot_js_display_pixel_ratio_get: function () {
		return GodotDisplayScreen.getPixelRatio();
	},

	godot_js_display_fullscreen_request__proxy: 'sync',
	godot_js_display_fullscreen_request__sig: 'i',
	godot_js_display_fullscreen_request: function () {
		return GodotDisplayScreen.requestEmbedFullscreen();
	},

	godot_js_display_fullscreen_exit__proxy: 'sync',
	godot_js_display_fullscreen_exit__sig: 'i',
	godot_js_display_fullscreen_exit: function () {
		return GodotDisplayScreen.exitFullscreen();
	},

	godot_js_display_desired_size_set__proxy: 'sync',
	godot_js_display_desired_size_set__sig: 'iii',
	godot_js_display_desired_size_set: function (width, height) {
		// Returns whether the canvas changed size, like godot_js_display_size_update(): the caller must then
		// run the same resize path (root window, canvas element, rendering surface). Before it returned
		// nothing, so the canvas was resized here and the next size_update() found nothing left to report.
		if (GodotConfig.canvas_resize_policy === 0) {
			// The page owns the canvas size: it never follows the requested size.
			return 0;
		}
		GodotDisplayScreen.desired_size = [width, height];
		const updated = GodotDisplayScreen.updateSize();
		if (updated) {
			GodotDisplayVK.updateSize();
		}
		return updated;
	},

	godot_js_display_size_update__proxy: 'sync',
	godot_js_display_size_update__sig: 'i',
	godot_js_display_size_update: function () {
		const updated = GodotDisplayScreen.updateSize();
		if (updated) {
			GodotDisplayVK.updateSize();
		}
		return updated;
	},

	godot_js_display_screen_size_get__proxy: 'sync',
	godot_js_display_screen_size_get__sig: 'vii',
	godot_js_display_screen_size_get: function (width, height) {
		const scale = GodotDisplayScreen.getPixelRatio();
		GodotRuntime.setHeapValue(width, window.screen.width * scale, 'i32');
		GodotRuntime.setHeapValue(height, window.screen.height * scale, 'i32');
	},

	godot_js_display_window_size_get__proxy: 'sync',
	godot_js_display_window_size_get__sig: 'vii',
	godot_js_display_window_size_get: function (p_width, p_height) {
		const size = GodotDisplayScreen.canvasSize();
		GodotRuntime.setHeapValue(p_width, size[0], 'i32');
		GodotRuntime.setHeapValue(p_height, size[1], 'i32');
	},

	godot_js_display_has_webgl__proxy: 'sync',
	godot_js_display_has_webgl__sig: 'ii',
	godot_js_display_has_webgl: function (p_version) {
		if (p_version !== 1 && p_version !== 2) {
			return false;
		}
		try {
			return !!document.createElement('canvas').getContext(p_version === 2 ? 'webgl2' : 'webgl');
		} catch (e) { /* Not available */ }
		return false;
	},

	/*
	 * Canvas
	 */
	godot_js_display_canvas_focus__proxy: 'sync',
	godot_js_display_canvas_focus__sig: 'v',
	godot_js_display_canvas_focus: function () {
		GodotConfig.canvas.focus();
	},

	godot_js_display_canvas_is_focused__proxy: 'sync',
	godot_js_display_canvas_is_focused__sig: 'i',
	godot_js_display_canvas_is_focused: function () {
		return document.activeElement === GodotConfig.canvas;
	},

	/*
	 * Touchscreen
	 */
	godot_js_display_touchscreen_is_available__proxy: 'sync',
	godot_js_display_touchscreen_is_available__sig: 'i',
	godot_js_display_touchscreen_is_available: function () {
		return 'ontouchstart' in window;
	},

	/*
	 * Clipboard
	 */
	godot_js_display_clipboard_set__proxy: 'sync',
	godot_js_display_clipboard_set__sig: 'ii',
	godot_js_display_clipboard_set: function (p_text) {
		const text = GodotRuntime.parseString(p_text);
		if (!navigator.clipboard || !navigator.clipboard.writeText) {
			return 1;
		}
		navigator.clipboard.writeText(text).catch(function (e) {
			// Setting OS clipboard is only possible from an input callback.
			GodotRuntime.error('Setting OS clipboard is only possible from an input callback for the Web platform. Exception:', e);
		});
		return 0;
	},

	godot_js_display_clipboard_get__proxy: 'sync',
	godot_js_display_clipboard_get__sig: 'ii',
	godot_js_display_clipboard_get: function (callback) {
		const func = GodotRuntime.get_func(callback);
		try {
			navigator.clipboard.readText().then(function (result) {
				const ptr = GodotRuntime.allocString(result);
				func(ptr);
				GodotRuntime.free(ptr);
			}).catch(function (e) {
				// Fail graciously.
			});
		} catch (e) {
			// Fail graciously.
		}
	},

	/*
	 * Window
	 */
	godot_js_display_window_title_set__proxy: 'sync',
	godot_js_display_window_title_set__sig: 'vi',
	godot_js_display_window_title_set: function (p_data) {
		document.title = GodotRuntime.parseString(p_data);
	},

	godot_js_display_window_icon_set__proxy: 'sync',
	godot_js_display_window_icon_set__sig: 'vii',
	godot_js_display_window_icon_set: function (p_ptr, p_len) {
		let link = document.getElementById('-gd-engine-icon');
		const old_icon = GodotDisplay.window_icon;
		if (p_ptr) {
			if (link === null) {
				link = document.createElement('link');
				link.rel = 'icon';
				link.id = '-gd-engine-icon';
				document.head.appendChild(link);
			}
			const png = new Blob([GodotRuntime.heapSlice(HEAPU8, p_ptr, p_len)], { type: 'image/png' });
			GodotDisplay.window_icon = URL.createObjectURL(png);
			link.href = GodotDisplay.window_icon;
		} else {
			if (link) {
				link.remove();
			}
			GodotDisplay.window_icon = null;
		}
		if (old_icon) {
			URL.revokeObjectURL(old_icon);
		}
	},

	/*
	 * Cursor
	 */
	godot_js_display_cursor_set_visible__proxy: 'sync',
	godot_js_display_cursor_set_visible__sig: 'vi',
	godot_js_display_cursor_set_visible: function (p_visible) {
		const visible = p_visible !== 0;
		if (visible === GodotDisplayCursor.visible) {
			return;
		}
		GodotDisplayCursor.visible = visible;
		if (visible) {
			GodotDisplayCursor.set_shape(GodotDisplayCursor.shape);
		} else {
			GodotDisplayCursor.set_style('none');
		}
	},

	godot_js_display_cursor_is_hidden__proxy: 'sync',
	godot_js_display_cursor_is_hidden__sig: 'i',
	godot_js_display_cursor_is_hidden: function () {
		return !GodotDisplayCursor.visible;
	},

	godot_js_display_cursor_set_shape__proxy: 'sync',
	godot_js_display_cursor_set_shape__sig: 'vi',
	godot_js_display_cursor_set_shape: function (p_string) {
		GodotDisplayCursor.set_shape(GodotRuntime.parseString(p_string));
	},

	godot_js_display_cursor_set_custom_shape__proxy: 'sync',
	godot_js_display_cursor_set_custom_shape__sig: 'viiiii',
	godot_js_display_cursor_set_custom_shape: function (p_shape, p_ptr, p_len, p_hotspot_x, p_hotspot_y) {
		const shape = GodotRuntime.parseString(p_shape);
		const old_shape = GodotDisplayCursor.cursors[shape];
		if (p_len > 0) {
			const png = new Blob([GodotRuntime.heapSlice(HEAPU8, p_ptr, p_len)], { type: 'image/png' });
			const url = URL.createObjectURL(png);
			GodotDisplayCursor.cursors[shape] = {
				url: url,
				x: p_hotspot_x,
				y: p_hotspot_y,
			};
		} else {
			delete GodotDisplayCursor.cursors[shape];
		}
		if (shape === GodotDisplayCursor.shape) {
			GodotDisplayCursor.set_shape(GodotDisplayCursor.shape);
		}
		if (old_shape) {
			URL.revokeObjectURL(old_shape.url);
		}
	},

	godot_js_display_cursor_lock_set__proxy: 'sync',
	godot_js_display_cursor_lock_set__sig: 'vi',
	godot_js_display_cursor_lock_set: function (p_lock) {
		if (p_lock) {
			GodotDisplayCursor.lockPointer();
		} else {
			GodotDisplayCursor.releasePointer();
		}
	},

	godot_js_display_cursor_is_locked__proxy: 'sync',
	godot_js_display_cursor_is_locked__sig: 'i',
	godot_js_display_cursor_is_locked: function () {
		return GodotDisplayCursor.isPointerLocked() ? 1 : 0;
	},

	/*
	 * Listeners
	 */
	godot_js_display_fullscreen_cb__proxy: 'sync',
	godot_js_display_fullscreen_cb__sig: 'vi',
	godot_js_display_fullscreen_cb: function (callback) {
		const canvas = GodotConfig.canvas;
		const func = GodotRuntime.get_func(callback);
		GodotDisplayScreen.fullscreen_cb = func;
		function change_cb(evt) {
			if (evt.target === canvas) {
				if (GodotDisplayScreen.embed) {
					GodotDisplayScreen.reportFullscreen(); // Deduplicated against the parent's messages.
				} else {
					func(GodotDisplayScreen.isFullscreen());
				}
			}
		}
		GodotEventListeners.add(document, 'fullscreenchange', change_cb, false);
		GodotEventListeners.add(document, 'mozfullscreenchange', change_cb, false);
		GodotEventListeners.add(document, 'webkitfullscreenchange', change_cb, false);
	},

	godot_js_display_window_blur_cb__proxy: 'sync',
	godot_js_display_window_blur_cb__sig: 'vi',
	godot_js_display_window_blur_cb: function (callback) {
		const func = GodotRuntime.get_func(callback);
		GodotEventListeners.add(window, 'blur', function () {
			func();
		}, false);
	},

	godot_js_display_notification_cb__proxy: 'sync',
	godot_js_display_notification_cb__sig: 'viiiii',
	godot_js_display_notification_cb: function (callback, p_enter, p_exit, p_in, p_out) {
		const canvas = GodotConfig.canvas;
		const func = GodotRuntime.get_func(callback);
		const notif = [p_enter, p_exit, p_in, p_out];
		['mouseover', 'mouseleave', 'focus', 'blur'].forEach(function (evt_name, idx) {
			GodotEventListeners.add(canvas, evt_name, function () {
				func(notif[idx]);
			}, true);
		});
	},

	godot_js_display_setup_canvas__proxy: 'sync',
	godot_js_display_setup_canvas__sig: 'viiii',
	godot_js_display_setup_canvas: function (p_width, p_height, p_fullscreen, p_hidpi) {
		const canvas = GodotConfig.canvas;
		GodotEventListeners.add(canvas, 'contextmenu', function (ev) {
			ev.preventDefault();
		}, false);
		GodotEventListeners.add(canvas, 'webglcontextlost', function (ev) {
			alert('WebGL context lost, please reload the page'); // eslint-disable-line no-alert
			ev.preventDefault();
		}, false);
		GodotDisplayScreen.hidpi = !!p_hidpi;
		switch (GodotConfig.canvas_resize_policy) {
		case 0: // None
			GodotDisplayScreen.desired_size = [canvas.width, canvas.height];
			break;
		case 1: // Project
			GodotDisplayScreen.desired_size = [p_width, p_height];
			break;
		default: // Full window
			// Ensure we display in the right place, the size will be handled by updateSize
			canvas.style.position = 'absolute';
			canvas.style.top = 0;
			canvas.style.left = 0;
			break;
		}
		GodotDisplayScreen.updateSize();
		if (p_fullscreen) {
			GodotDisplayScreen.requestFullscreen();
		}
	},

	/*
	 * Virtual Keyboard
	 */
	godot_js_display_vk_show__proxy: 'sync',
	godot_js_display_vk_show__sig: 'viiii',
	godot_js_display_vk_show: function (p_text, p_type, p_start, p_end) {
		const text = GodotRuntime.parseString(p_text);
		const start = p_start > 0 ? p_start : 0;
		const end = p_end > 0 ? p_end : start;
		GodotDisplayVK.show(text, p_type, start, end);
	},

	godot_js_display_vk_hide__proxy: 'sync',
	godot_js_display_vk_hide__sig: 'v',
	godot_js_display_vk_hide: function () {
		GodotDisplayVK.hide();
	},

	godot_js_display_vk_available__proxy: 'sync',
	godot_js_display_vk_available__sig: 'i',
	godot_js_display_vk_available: function () {
		return GodotDisplayVK.available();
	},

	godot_js_display_tts_available__proxy: 'sync',
	godot_js_display_tts_available__sig: 'i',
	godot_js_display_tts_available: function () {
		return 'speechSynthesis' in window;
	},

	/*
	 * Keep screen on, orientation
	 */
	godot_js_display_screen_init__proxy: 'sync',
	godot_js_display_screen_init__sig: 'vi',
	godot_js_display_screen_init: function (p_verbose) {
		GodotDisplayScreen.init(p_verbose);
	},

	godot_js_display_wake_lock_available__proxy: 'sync',
	godot_js_display_wake_lock_available__sig: 'i',
	godot_js_display_wake_lock_available: function () {
		return GodotDisplayScreen.hasWakeLock() ? 1 : 0;
	},

	godot_js_display_wake_lock_set__proxy: 'sync',
	godot_js_display_wake_lock_set__sig: 'vi',
	godot_js_display_wake_lock_set: function (p_enable) {
		GodotDisplayScreen.setKeepOn(p_enable);
	},

	godot_js_display_orientation_available__proxy: 'sync',
	godot_js_display_orientation_available__sig: 'i',
	godot_js_display_orientation_available: function () {
		return GodotDisplayScreen.canLockOrientation() ? 1 : 0;
	},

	godot_js_display_orientation_set__proxy: 'sync',
	godot_js_display_orientation_set__sig: 'vi',
	godot_js_display_orientation_set: function (p_orientation) {
		GodotDisplayScreen.setOrientation(p_orientation);
	},

	godot_js_display_vk_cb__proxy: 'sync',
	godot_js_display_vk_cb__sig: 'vi',
	godot_js_display_vk_cb: function (p_input_cb) {
		const input_cb = GodotRuntime.get_func(p_input_cb);
		if (GodotDisplayVK.available()) {
			GodotDisplayVK.init(input_cb);
		}
	},
};

autoAddDeps(GodotDisplay, '$GodotDisplay');
mergeInto(LibraryManager.library, GodotDisplay);
