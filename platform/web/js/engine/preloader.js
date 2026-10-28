const Preloader = /** @constructor */ function () { // eslint-disable-line no-unused-vars
	function getTrackedResponse(response, load_status) {
		let bodyLoaded = null;
		// Settles when the whole body has arrived, which a raw (streamed) response's own promise
		// does not say: it resolves on the headers.
		load_status.body = new Promise(function (resolve, reject) {
			bodyLoaded = { resolve: resolve, reject: reject };
		});
		load_status.body.catch(function () {}); // Nobody may be waiting on it.
		function onloadprogress(reader, controller) {
			return reader.read().then(function (result) {
				if (load_status.done) {
					return Promise.resolve();
				}
				if (result.value) {
					controller.enqueue(result.value);
					load_status.loaded += result.value.length;
				}
				if (!result.done) {
					return onloadprogress(reader, controller);
				}
				load_status.done = true;
				return Promise.resolve();
			});
		}
		const reader = response.body.getReader();
		return new Response(new ReadableStream({
			start: function (controller) {
				onloadprogress(reader, controller).then(function () {
					controller.close();
					bodyLoaded.resolve();
				}, function (err) {
					// A connection lost mid-body fails the reader (and so a retry of a buffered
					// download) instead of leaving it waiting forever.
					controller.error(err);
					bodyLoaded.reject(err);
				});
			},
		}), { headers: response.headers });
	}

	function loadFetch(file, tracker, fileSize, raw, init) {
		tracker[file] = {
			total: fileSize || 0,
			loaded: 0,
			done: false,
		};
		return fetch(file, init).then(function (response) {
			if (!response.ok) {
				return Promise.reject(new Error(`Failed loading file '${file}'`));
			}
			const tr = getTrackedResponse(response, tracker[file]);
			if (raw) {
				return Promise.resolve(tr);
			}
			return tr.arrayBuffer();
		});
	}

	function retry(func, attempts = 1) {
		function onerror(err) {
			if (attempts <= 1) {
				return Promise.reject(err);
			}
			return new Promise(function (resolve, reject) {
				setTimeout(function () {
					retry(func, attempts - 1).then(resolve).catch(reject);
				}, 1000);
			});
		}
		return func().catch(onerror);
	}

	const DOWNLOAD_ATTEMPTS_MAX = 4;
	// What the engine needs to start, and what the loading bar (`progressFunc`) reports.
	const loadingFiles = {};
	// Downloads nothing waits on (the shader extras): reported only to `prefetchProgressFunc`,
	// so the loading bar never counts a file the engine starts without.
	const backgroundFiles = {};
	const lastProgress = { loaded: 0, total: 0 };
	const lastPrefetchProgress = { loaded: 0, total: 0 };
	let progressFunc = null;
	let prefetchProgressFunc = null;
	let animating = false;

	function sumProgress(trackers) {
		const sum = { loaded: 0, total: 0, final: true };
		let totalIsValid = true;
		trackers.forEach(function (tracker) {
			Object.keys(tracker).forEach(function (file) {
				const stat = tracker[file];
				if (!stat.done) {
					sum.final = false;
				}
				if (!totalIsValid || stat.total === 0) {
					totalIsValid = false;
					sum.total = 0;
				} else {
					sum.total += stat.total;
				}
				sum.loaded += stat.loaded;
			});
		});
		return sum;
	}

	function report(func, last, sum) {
		if (sum.loaded !== last.loaded || sum.total !== last.total) {
			last.loaded = sum.loaded;
			last.total = sum.total;
			if (typeof func === 'function') {
				func(sum.loaded, sum.total);
			}
		}
	}

	const animateProgress = function () {
		animating = false;
		const main = sumProgress([loadingFiles]);
		report(progressFunc, lastProgress, main);
		const all = sumProgress([loadingFiles, backgroundFiles]);
		if (prefetchProgressFunc) {
			report(prefetchProgressFunc, lastPrefetchProgress, all);
		}
		if (!all.final) {
			animating = true;
			requestAnimationFrame(animateProgress);
		}
	};

	// One animation loop at most, however many downloads or callbacks start one.
	function startAnimating() {
		if (!animating) {
			animating = true;
			requestAnimationFrame(animateProgress);
		}
	}

	this.animateProgress = startAnimating;

	// A callback set while downloads are under way (or done, after a prefetch) is told the
	// current totals on the next frame, so it continues from what has been loaded so far.
	this.setProgressFunc = function (callback) {
		progressFunc = callback;
		lastProgress.loaded = -1;
		startAnimating();
	};

	this.setPrefetchProgressFunc = function (callback) {
		prefetchProgressFunc = callback;
		lastPrefetchProgress.loaded = -1;
		startAnimating();
	};

	// Tells the prefetch callback the final totals now and detaches it, so no report from a later
	// animation frame arrives after the caller has been told the prefetch finished.
	this.finishPrefetchProgress = function () {
		if (prefetchProgressFunc) {
			report(prefetchProgressFunc, lastPrefetchProgress, sumProgress([loadingFiles, backgroundFiles]));
		}
		prefetchProgressFunc = null;
	};

	// Settles once the body of a file loaded through loadPromise() has fully arrived.
	this.bodyLoaded = function (file) {
		const stat = loadingFiles[file];
		return (stat && stat.body) || Promise.reject(new Error(`'${file}' is not being loaded`));
	};

	this.loadPromise = function (file, fileSize, raw = false) {
		return retry(loadFetch.bind(null, file, loadingFiles, fileSize, raw, undefined), DOWNLOAD_ATTEMPTS_MAX);
	};

	// Downloads by URL, so a file asked for twice (by a prefetch and then by the startup, or by two
	// preloadFile() calls) is fetched once. A download that failed is forgotten, so the next request
	// fetches it again, with the usual retries.
	let downloads = {};
	this.fetchFile = function (file, fileSize, background = false) {
		if (!Object.hasOwn(downloads, file)) {
			// Low priority where the browser supports it, so a background file never competes with
			// what the game needs to start.
			const init = (background && typeof Request !== 'undefined' && 'priority' in Request.prototype) ? { priority: 'low' } : undefined;
			const tracker = background ? backgroundFiles : loadingFiles;
			const promise = retry(loadFetch.bind(null, file, tracker, fileSize, false, init), DOWNLOAD_ATTEMPTS_MAX);
			downloads[file] = promise;
			promise.catch(function () {
				if (downloads[file] === promise) {
					delete downloads[file];
					// Out of the progress too: the next request starts the file from 0 again.
					delete tracker[file];
				}
			});
			startAnimating();
		}
		return downloads[file];
	};

	// Hands over a download started earlier, and forgets it, so the buffer is freed once the caller
	// is done with it. Null when nothing fetched that file.
	this.takeDownload = function (file) {
		if (!Object.hasOwn(downloads, file)) {
			return null;
		}
		const promise = downloads[file];
		delete downloads[file];
		return promise;
	};

	// Called once the preloaded files are in the engine's file system: drops the buffers of every
	// download, including one the startup ended up not using (a texture pack a prefetch picked
	// differently from the device).
	this.releaseDownloads = function () {
		downloads = {};
	};

	this.preloadedFiles = [];
	// A second preload to the same path replaces the first rather than writing the file twice.
	function addPreloaded(files, path, buffer) {
		for (let i = 0; i < files.length; i++) {
			if (files[i].path === path) {
				files[i].buffer = buffer;
				return;
			}
		}
		files.push({ path: path, buffer: buffer });
	}
	this.preload = function (pathOrBuffer, destPath, fileSize) {
		let buffer = null;
		if (typeof pathOrBuffer === 'string') {
			const me = this;
			return this.fetchFile(pathOrBuffer, fileSize).then(function (buf) {
				addPreloaded(me.preloadedFiles, destPath || pathOrBuffer, buf);
				return Promise.resolve();
			});
		} else if (pathOrBuffer instanceof ArrayBuffer) {
			buffer = new Uint8Array(pathOrBuffer);
		} else if (ArrayBuffer.isView(pathOrBuffer)) {
			buffer = new Uint8Array(pathOrBuffer.buffer);
		}
		if (buffer) {
			addPreloaded(this.preloadedFiles, destPath, pathOrBuffer);
			return Promise.resolve();
		}
		return Promise.reject(new Error('Invalid object for preloading'));
	};
};
