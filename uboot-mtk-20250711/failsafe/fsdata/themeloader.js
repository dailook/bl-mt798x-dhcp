/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 */
(function () {
	function normalizeHexColor(input) {
		var s, hex;
		if (!input) return null;
		s = String(input).trim();
		if (s === "") return null;
		if (s[0] === "#") s = s.slice(1);
		if (!/^[0-9a-fA-F]{3}$/.test(s) && !/^[0-9a-fA-F]{6}$/.test(s)) return null;
		if (s.length === 3) {
			hex = "#" + s[0] + s[0] + s[1] + s[1] + s[2] + s[2];
		} else {
			hex = "#" + s;
		}
		return hex.toLowerCase();
	}

	function normalizeThemeMode(input) {
		if (!input) return null;
		var s = String(input).trim().toLowerCase();
		if (s === "auto" || s === "light" || s === "dark") return s;
		return null;
	}

	function hexToRgb(hex) {
		var n = normalizeHexColor(hex);
		if (!n) return null;
		return {
			r: parseInt(n.slice(1, 3), 16),
			g: parseInt(n.slice(3, 5), 16),
			b: parseInt(n.slice(5, 7), 16)
		};
	}

	function blendColor(hex, targetHex, t) {
		var a = hexToRgb(hex);
		var b = hexToRgb(targetHex);
		if (!a || !b) return hex;
		var r = Math.round(a.r + (b.r - a.r) * t);
		var g = Math.round(a.g + (b.g - a.g) * t);
		var b2 = Math.round(a.b + (b.b - a.b) * t);
		return "#" + r.toString(16).padStart(2, "0") + g.toString(16).padStart(2, "0") + b2.toString(16).padStart(2, "0");
	}

	function setupThemeTransition(root) {
		var timer = null;
		var ready = false;
		var lastThemeAttr = root.getAttribute("data-theme");
		var durationMs = 700;

		function pulse() {
			if (!ready) return;
			if (timer) {
				clearTimeout(timer);
				timer = null;
			}
			root.classList.add("theme-transition");
			timer = setTimeout(function () {
				root.classList.remove("theme-transition");
				timer = null;
			}, durationMs);
		}

		root.__failsafeThemePulse = pulse;

		try {
			var obs = new MutationObserver(function () {
				var now = root.getAttribute("data-theme");
				if (now !== lastThemeAttr) {
					lastThemeAttr = now;
					if (root.__failsafeThemeSilent) return;
					pulse();
				}
			});
			obs.observe(root, { attributes: true, attributeFilter: ["data-theme"] });
		} catch (e) { }

		try {
			var mq = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)");
			if (mq && mq.addEventListener) {
				mq.addEventListener("change", function () {
					var cur = root.getAttribute("data-theme");
					if (!cur || cur === "auto") pulse();
				});
			} else if (mq && mq.addListener) {
				mq.addListener(function () {
					var cur2 = root.getAttribute("data-theme");
					if (!cur2 || cur2 === "auto") pulse();
				});
			}
		} catch (e2) { }

		setTimeout(function () {
			ready = true;
		}, 0);
	}

	function getPreferredScheme() {
		try {
			var mq = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)");
			return mq && mq.matches ? "dark" : "light";
		} catch (e) {
			return "light";
		}
	}

	function applyThemeMode(root, mode, opts) {
		var norm = normalizeThemeMode(mode) || "auto";
		var silent = opts && opts.silent;
		if (!root) return;
		var applyAttr = function (value, isAuto) {
			if (silent) root.__failsafeThemeSilent = true;
			if (isAuto) {
				root.setAttribute("data-theme-auto", "1");
				root.setAttribute("data-theme", value);
			} else {
				root.removeAttribute("data-theme-auto");
				root.setAttribute("data-theme", value);
			}
			if (silent) root.__failsafeThemeSilent = false;
		};

		var scheduleApply = function (value, isAuto, shouldPulse) {
			if (shouldPulse && root.__failsafeThemePulse)
				root.__failsafeThemePulse();
			if (silent) {
				applyAttr(value, isAuto);
				return;
			}
			if (window.requestAnimationFrame) {
				requestAnimationFrame(function () {
					requestAnimationFrame(function () {
						applyAttr(value, isAuto);
					});
				});
			} else {
				setTimeout(function () {
					applyAttr(value, isAuto);
				}, 0);
			}
		};
		if (!applyThemeMode._mq) {
			applyThemeMode._mq = window.matchMedia && window.matchMedia("(prefers-color-scheme: dark)");
		}
		if (!applyThemeMode._onChange) {
			applyThemeMode._onChange = function () {
				if (applyThemeMode._mode !== "auto") return;
				var next = getPreferredScheme();
				scheduleApply(next, true, true);
			};
		}

		if (applyThemeMode._mq) {
			try {
				if (applyThemeMode._mq.addEventListener) {
					applyThemeMode._mq.removeEventListener("change", applyThemeMode._onChange);
					applyThemeMode._mq.addEventListener("change", applyThemeMode._onChange);
				} else if (applyThemeMode._mq.removeListener) {
					applyThemeMode._mq.removeListener(applyThemeMode._onChange);
					applyThemeMode._mq.addListener(applyThemeMode._onChange);
				}
			} catch (e2) { }
		}

		applyThemeMode._mode = norm;
		if (norm === "auto") {
			var cur = getPreferredScheme();
			scheduleApply(cur, true, !silent);
		} else {
			scheduleApply(norm, false, !silent);
		}
	}

	try {
		var cached = localStorage.getItem("failsafe_theme_color_cache");
		var cachedTheme = localStorage.getItem("theme");
		var norm = normalizeHexColor(cached);
		var themeMode = normalizeThemeMode(cachedTheme);
		var rgb;
		var root;
		var lighter;
		var meta;
		root = document.documentElement;
		setupThemeTransition(root);
		applyThemeMode(root, themeMode || "auto", { silent: true });
		window.__failsafeThemeApplyMode = function (mode, opts) {
			applyThemeMode(root, mode, opts);
		};
		if (!norm) return;
		rgb = hexToRgb(norm);
		if (!rgb) return;
		root.style.setProperty("--primary", norm);
		root.style.setProperty("--primary-rgb", rgb.r + ", " + rgb.g + ", " + rgb.b);
		lighter = blendColor(norm, "#ffffff", 0.28);
		root.style.setProperty("--primary-2", lighter);
		meta = document.querySelector("meta[name='theme-color']");
		if (!meta) {
			meta = document.createElement("meta");
			meta.setAttribute("name", "theme-color");
			document.head && document.head.appendChild(meta);
		}
		meta.setAttribute("content", norm);
	} catch (e) {
		/* ignore storage errors */
	}
})();
