/* SPDX-License-Identifier: GPL-2.0 */
// ==================== 国际化翻译 ====================
function t(key) {
    var lang = APP_STATE.lang || 'zh-cn';
    var translations = I18N[lang] || I18N['zh-cn'] || {};
    return translations[key] || key;
}
function tHtml(key) {
    var lang = APP_STATE.lang || 'zh-cn';
    var translations = I18N[lang] || I18N['zh-cn'] || {};
    return translations[key] || key;
}
function applyTranslation() {
    document.querySelectorAll('[data-i18n]').forEach(function(el) {
        var key = el.getAttribute('data-i18n');
        if (key) el.textContent = t(key);
    });
    document.querySelectorAll('[data-i18n-html]').forEach(function(el) {
        var key = el.getAttribute('data-i18n-html');
        if (key) el.innerHTML = tHtml(key);
    });
    document.querySelectorAll('[data-i18n-attr]').forEach(function(el) {
        var attrMap = el.getAttribute('data-i18n-attr');
        if (!attrMap) return;
        attrMap.split(',').forEach(function(pair) {
            var parts = pair.split(':');
            if (parts.length === 2) {
                var attrName = parts[0].trim();
                var key = parts[1].trim();
                el.setAttribute(attrName, t(key));
            }
        });
    });
    document.querySelectorAll('[data-i18n-placeholder]').forEach(function(el) {
        var key = el.getAttribute('data-i18n-placeholder');
        if (key) el.placeholder = t(key);
    });
}
function updatePageTitle() {
    var pageTitles = {
        'index': t('index.title'),
        'bl2': t('bl2.title'),
        'uboot': t('uboot.title'),
        'gpt': t('gpt.title'),
        'simg': t('simg.title'),
        'factory': t('factory.title'),
        'initramfs': t('initramfs.title'),
        'env': t('env.title'),
        'console': t('console.title'),
        'backup': t('backup.title'),
        'flash': t('flash.title'),
        'reboot': t('reboot.title')
    };
    var title = pageTitles[APP_STATE.page];
    if (title) {
        document.title = title + ' - ' + t('app.name');
    }
}

// ==================== 全局状态 ====================
var APP_STATE = {
    lang: "zh-cn",
    theme: "auto",
    page: "",
    sysinfo: null,
    backupinfo: null,
    console: {
        running: false,
        pollTimer: null,
        history: [],
        histPos: -1,
        tokenKey: "failsafe_console_token"
    }
};

// ==================== 基础工具函数 ====================
function ajax(n) {
    var t, i;
    t = window.XMLHttpRequest ? new XMLHttpRequest() : new ActiveXObject("Microsoft.XMLHTTP");
    t.upload.addEventListener("progress", function (t) {
        n.progress && n.progress(t)
    });
    t.onreadystatechange = function () {
        t.readyState == 4 && t.status == 200 && n.done && n.done(t.responseText)
    };
    n.timeout && (t.timeout = n.timeout);
    i = "GET";
    n.data && (i = "POST");
    t.open(i, n.url);
    t.send(n.data)
}
function bytesToHuman(n) {
    var t;
    return n === null || n === undefined ? "" : (t = Number(n), !isFinite(t) || t < 0) ? "" : t >= 1024 * 1024 * 1024 ? (t / (1024 * 1024 * 1024)).toFixed(2) + " GiB" : t >= 1024 * 1024 ? (t / (1024 * 1024)).toFixed(2) + " MiB" : t >= 1024 ? (t / 1024).toFixed(2) + " KiB" : String(Math.floor(t)) + " B"
}
function parseFilenameFromDisposition(n) {
    var t, i;
    return n ? (t = /filename\s*=\s*"([^"]+)"/i.exec(n), t && t[1]) ? t[1] : (i = /filename\s*=\s*([^;\s]+)/i.exec(n), i && i[1] ? i[1].replace(/^"|"$/g, "") : "") : ""
}
function sanitizeFilenameComponent(n) {
    return n ? String(n).replace(/[^a-zA-Z0-9._-]+/g, "_").replace(/^_+|_+$/g, "").slice(0, 48) : ""
}
function getNowYYYYMMDD() {
    var n = new Date(), t = n.getFullYear(), i = n.getMonth() + 1, r = n.getDate();
    return String(t) + String(i).padStart(2, "0") + String(r).padStart(2, "0")
}
function parseUserLen(n) {
    var t, i, r;
    if (!n) return null;
    if (n = String(n).trim(), n === "") return null;
    t = /^\s*(0x[0-9a-fA-F]+|\d+)\s*([a-zA-Z]*)\s*$/.exec(n);
    if (!t) return null;
    i = t[1].toLowerCase().indexOf("0x") === 0 ? parseInt(t[1], 16) : parseInt(t[1], 10);
    if (!isFinite(i) || i < 0) return null;
    r = (t[2] || "").toLowerCase();
    return r === "" ? i : r === "k" || r === "kb" || r === "kib" ? i * 1024 : null
}

// ==================== 新增：存储设备功能 ====================
function flashGetDeviceNameByStorage(storage) {
    var bi = APP_STATE && APP_STATE.backupinfo ? APP_STATE.backupinfo : null;
    var mmcName = "";
    var mtdName = "";
    if (bi && bi.mmc && bi.mmc.present) {
        mmcName = [bi.mmc.vendor || "", bi.mmc.product || ""].join(" ").trim();
        if (!mmcName) mmcName = "MMC";
    }
    if (bi && bi.mtd && bi.mtd.present) {
        mtdName = (bi.mtd.model || "").trim();
        if (!mtdName) mtdName = "MTD";
    }
    if (storage === "mtd") return mtdName || "MTD";
    if (storage === "mmc") return mmcName || "MMC";
    return mtdName || mmcName || "device";
}
function flashBuildErasePlan() {
    var target = document.getElementById("flash_target");
    var startEl = document.getElementById("flash_start");
    var endEl = document.getElementById("flash_end");
    var v, seg, storage, tname, isRaw, startStr, endStr, hasStart, hasEnd, start, end;
    var targetLabel, detail;
    if (!target || !target.value)
        return { error: "请选择目标" };
    v = String(target.value);
    seg = v.split(":");
    storage = seg.length > 1 ? seg[0] : "auto";
    tname = seg.length > 1 ? seg.slice(1).join(":") : v;
    isRaw = tname === "raw";
    startStr = startEl && startEl.value ? String(startEl.value).trim() : "";
    endStr = endEl && endEl.value ? String(endEl.value).trim() : "";
    hasStart = !!startStr;
    hasEnd = !!endStr;
    if (hasStart !== hasEnd)
        return { error: "范围格式错误：起始和结束地址必须同时提供或同时为空" };
    if (hasStart && hasEnd) {
        start = parseUserLen(startStr);
        end = parseUserLen(endStr);
        if (start === null || end === null || end <= start)
            return { error: "范围格式错误" };
    }
    if (isRaw && !hasStart)
        return { error: "范围格式错误 (raw target requires start/end)" };
    targetLabel = isRaw ? "" : (tname + " 分区");
    if (hasStart)
        detail = isRaw ? ("0x" + start.toString(16) + "~0x" + end.toString(16)) :
            (targetLabel + " 的 0x" + start.toString(16) + "~0x" + end.toString(16));
    else
        detail = targetLabel;
    return {
        storage: storage,
        target: v,
        hasRange: hasStart,
        start: hasStart ? start : null,
        end: hasStart ? end : null,
        detail: detail,
        deviceName: flashGetDeviceNameByStorage(storage)
    };
}
function updateGptNavVisibility() {
    var el = document.querySelector("#sidebar [data-nav-id='gpt']");
    if (!el) return;
    var bi = APP_STATE.backupinfo;
    if (!bi || !bi.mmc || typeof bi.mmc.present === "undefined") {
        el.style.display = "none";
        return;
    }
    el.style.display = bi.mmc.present === false ? "none" : "";
}
function updateSimgNavVisibility() {
    var el = document.querySelector("#sidebar [data-nav-id='simg']");
    if (!el) return;
    el.style.display = "none";
    if (APP_STATE._simg_probe_done) return;
    APP_STATE._simg_probe_done = true;
    try {
        fetch("/simg.html?_probe=1", { method: "GET", cache: "no-store" })
            .then(function (r) {
                if (r && r.ok) {
                    el.style.display = "";
                }
            })
            .catch(function () { });
    } catch (e) { }
}
function getStorageInfoForSysinfo() {
    if (APP_STATE.backupinfo) {
        updateGptNavVisibility();
        return;
    }
    ajax({
        url: "/backup/info",
        done: function (txt) {
            try {
                APP_STATE.backupinfo = JSON.parse(txt);
            } catch (e) { return; }
            updateGptNavVisibility();
            if (typeof renderSysInfo === "function") renderSysInfo();
        }
    });
}

// ==================== 国际化 ====================
function normalizeLang(n) {
    if (!n) return "zh-cn";
    var t = String(n).toLowerCase();
    return t.indexOf("zh") === 0 ? "zh-cn" : "en"
}
function setLang(n) {
    APP_STATE.lang = normalizeLang(n);
    try {
        localStorage.setItem("lang", APP_STATE.lang)
    } catch (t) { }
    applyTranslation();
    updatePageTitle();
}

// ==================== 核心功能函数 ====================
function upload(n) {
    var o = document.getElementById("file").files[0],
        u, f, e, r, i, s, a;
    if (!o) {
        alert("请选择文件");
        return;
    }
    a = o.name || "";
    u = document.getElementById("form");
    u && (u.style.display = "none");
    f = document.getElementById("hint");
    f && (f.style.display = "none");
    e = document.getElementById("bar");
    e && (e.style.display = "block");
    r = new FormData();
    r.append(n, o);
    i = document.getElementById("mtd_layout_label");
    i && i.options.length > 0 && (s = i.selectedIndex, r.append("mtd_layout", i.options[s].value));
    ajax({
        url: "/upload",
        data: r,
        done: function (n) {
            var i, r, u, f, e, l, md5InName, md5Hint, md5Ok, md5Match, md5Class;
            if (n == "fail") {
                location = "/fail.html";
                return;
            }
            i = n.split(" ");
            l = document.getElementById("filename");
            if (l && a) {
                l.style.display = "block";
                l.innerHTML = "<span class=\"filename-label\">文件：</span><span class=\"filename-value\">" + a + "</span>";
            }
            r = document.getElementById("size");
            r && (r.style.display = "block", r.innerHTML = "大小：" + i[0]);
            u = document.getElementById("md5");
            md5Match = a ? /(?:^|[._-])md5-([0-9a-fA-F]{32})(?:$|[._-])/.exec(a) : null;
            md5InName = md5Match && md5Match[1] ? md5Match[1] : "";
            if (u) {
                u.style.display = "block";
                md5Ok = i[1] && md5InName && String(i[1]).toLowerCase() === String(md5InName).toLowerCase();
                md5Hint = md5InName ? (md5Ok ? "✓ MD5匹配" : "✗ MD5不匹配") : "";
                md5Class = md5InName ? (md5Ok ? "md5-ok" : "md5-bad") : "";
                u.innerHTML = "MD5：" + i[1] + (md5Hint ? " <span class=\"md5-status " + md5Class + "\">" + md5Hint + "</span>" : "");
            }
            f = document.getElementById("mtd");
            f && i[2] && (f.style.display = "block", f.innerHTML = "MTD 布局：" + i[2]);
            e = document.getElementById("upgrade");
            e && (e.style.display = "block");
        },
        progress: function (n) {
            if (n.total) {
                var i = parseInt(n.loaded / n.total * 100),
                    t = document.getElementById("bar");
                t && (t.style.display = "block", t.style.setProperty("--percent", i))
            }
        }
    });
}
function getversion() {
    ajax({
        url: "/version",
        done: function (n) {
            var t = document.getElementById("version");
            t && (t.innerHTML = n.replace(/\s*default\s*/, '').trim() + " - dailook")
        }
    })
}
function getmtdlayoutlist() {
    ajax({
        url: "/getmtdlayout",
        done: function (n) {
            var i, f, e, u, r, o;
            if (n != "error" && (i = n.split(";"), f = document.getElementById("current_mtd_layout"), f && (f.innerHTML = t("label.current_layout") + ": " + i[0]), e = document.getElementById("choose_mtd_layout"), e && (e.textContent = t("label.choose_mtd")), u = document.getElementById("mtd_layout_label"), u)) {
                for (u.options.length = 0, r = 1; r < i.length; r++) i[r].length > 0 && u.options.add(new Option(i[r], i[r]));
                o = document.getElementById("mtd_layout");
                o && (o.style.display = "")
            }
        }
    })
}

// ==================== 系统信息 ====================
function renderSysInfo() {
    var n = document.getElementById("sysinfo"), i, r, u, f, e;
    if (!n) return;
    i = APP_STATE.sysinfo;
    if (!i) {
        n.textContent = t("sysinfo.loading");
        return
    }
    u = i.board || {};
    f = i.ram || {};
    e = [];
    e.push(t("sysinfo.device") + ": " + (u.model || t("sysinfo.unknown")));
    f.size !== undefined && f.size !== null && f.size !== 0 ? e.push(t("sysinfo.memory") + ": " + bytesToHuman(f.size)) : e.push(t("sysinfo.memory") + ": " + t("sysinfo.unknown"));
    if (i.storage && i.storage.mtd_layout) {
        var mtdSummary = i.storage.mtd_layout || {};
        if (mtdSummary.current) {
            e.push(t("sysinfo.mtd_layout") + ": " + mtdSummary.current);
        }
    }
    n.textContent = e.join(" | ")
}
function getSysInfo() {
    var n = document.getElementById("sysinfo");
    n && renderSysInfo();
    ajax({
        url: "/sysinfo",
        done: function (txt) {
            try {
                APP_STATE.sysinfo = JSON.parse(txt)
            } catch (t) {
                return
            }
            n && renderSysInfo()
        }
    })
}

// ==================== 控制台功能 ====================
function consoleInit() {
    var out = document.getElementById("console_out");
    var cmd = document.getElementById("console_cmd");
    var status = document.getElementById("console_status");
    var token = document.getElementById("console_token");
    var persistKey = "failsafe_console_output";
    var persistMax = 200000;
    function loadToken() {
        try {
            var t = localStorage.getItem(APP_STATE.console.tokenKey);
            token && t && (token.value = t);
        } catch (e) { }
    }
    function saveToken() {
        try {
            token && localStorage.setItem(APP_STATE.console.tokenKey, token.value || "");
        } catch (e) { }
    }
    function setStatus(t) {
        status && (status.textContent = t || "");
    }
    function loadPersistedOutput() {
        if (!out) return;
        try {
            var s = sessionStorage.getItem(persistKey);
            if (s) out.textContent = s;
        } catch (e) { }
    }
    function savePersistedOutput() {
        if (!out) return;
        try {
            var s = out.textContent || "";
            if (s.length > persistMax)
                s = s.slice(s.length - persistMax);
            sessionStorage.setItem(persistKey, s);
        } catch (e) { }
    }
    function appendText(t) {
        if (!out) return;
        if (!t) return;
        out.textContent += t;
        if (out.textContent.length > persistMax)
            out.textContent = out.textContent.slice(out.textContent.length - persistMax);
        savePersistedOutput();
        out.scrollTop = out.scrollHeight;
    }
    async function pollOnce() {
        if (!APP_STATE.console.running) return;
        try {
            var fd = new FormData();
            if (token && token.value) fd.append("token", token.value);
            var r = await fetch("/console/poll", { method: "POST", body: fd });
            if (!r.ok) {
                setStatus("HTTP 错误：" + r.status);
                return;
            }
            var txt = await r.text();
            var j;
            try {
                j = JSON.parse(txt);
            } catch (e) {
                setStatus("解析错误");
                return;
            }
            j && j.data && appendText(j.data);
        } catch (e) {
            setStatus("错误：" + (e && e.message ? e.message : String(e)));
        }
    }
    function schedulePoll() {
        APP_STATE.console.pollTimer && clearTimeout(APP_STATE.console.pollTimer);
        APP_STATE.console.pollTimer = setTimeout(async function () {
            await pollOnce();
            schedulePoll();
        }, 300);
    }
    window.consoleSend = async function () {
        if (!cmd || !cmd.value) return;
        saveToken();
        var line = String(cmd.value);
        cmd.value = "";
        APP_STATE.console.history.unshift(line);
        APP_STATE.console.history.length > 50 && (APP_STATE.console.history.length = 50);
        APP_STATE.console.histPos = -1;
        try {
            var fd = new FormData();
            fd.append("cmd", line);
            if (token && token.value) fd.append("token", token.value);
            setStatus("正在运行...");
            var r = await fetch("/console/exec", { method: "POST", body: fd });
            var txt = await r.text();
            if (!r.ok) {
                setStatus("HTTP 错误：" + r.status + (txt ? ": " + txt : ""));
                return;
            }
            try {
                var j = JSON.parse(txt);
                setStatus("返回值：" + (j && typeof j.ret !== "undefined" ? j.ret : "?"));
            } catch (e) {
                setStatus("完成");
            }
        } catch (e) {
            setStatus("错误：" + (e && e.message ? e.message : String(e)));
        }
    };
    window.consoleClear = async function () {
        saveToken();
        try {
            var fd = new FormData();
            if (token && token.value) fd.append("token", token.value);
            var r = await fetch("/console/clear", { method: "POST", body: fd });
            if (r.ok) {
                out && (out.textContent = "");
                try { sessionStorage.removeItem(persistKey); } catch (e) { }
                setStatus("已清空");
            } else {
                setStatus("HTTP 错误：" + r.status);
            }
        } catch (e) {
            setStatus("错误：" + (e && e.message ? e.message : String(e)));
        }
    };
    if (cmd) {
        cmd.addEventListener("keydown", function (e) {
            if (e.key === "Enter") {
                e.preventDefault();
                window.consoleSend();
                return;
            }
            if (e.key === "ArrowUp") {
                var h = APP_STATE.console.history;
                if (!h || !h.length) return;
                APP_STATE.console.histPos = Math.min(h.length - 1, APP_STATE.console.histPos + 1);
                cmd.value = h[APP_STATE.console.histPos] || "";
                e.preventDefault();
                return;
            }
            if (e.key === "ArrowDown") {
                var h2 = APP_STATE.console.history;
                if (!h2 || !h2.length) return;
                APP_STATE.console.histPos = Math.max(-1, APP_STATE.console.histPos - 1);
                cmd.value = APP_STATE.console.histPos >= 0 ? (h2[APP_STATE.console.histPos] || "") : "";
                e.preventDefault();
            }
        });
    }
    APP_STATE.console.running = true;
    loadToken();
    loadPersistedOutput();
    setStatus("就绪");
    schedulePoll();
}

// ==================== 环境变量功能 ====================
function envInit() {
    var list = document.getElementById("env_list");
    var name = document.getElementById("env_name");
    var value = document.getElementById("env_value");
    var status = document.getElementById("env_status");
    var count = document.getElementById("env_count");
    var file = document.getElementById("env_file");
    function setStatus(t) {
        status && (status.textContent = t || "");
    }
    function countLines(txt) {
        if (!txt) return 0;
        var lines = txt.split("\n");
        var c = 0;
        for (var i = 0; i < lines.length; i++) {
            if (lines[i] && lines[i].indexOf("=") > 0)
                c++;
        }
        return c;
    }
    window.envRefresh = async function () {
        try {
            setStatus("正在加载...");
            var r = await fetch("/env/list", { method: "GET" });
            if (!r.ok) {
                setStatus("HTTP 错误：" + r.status);
                return;
            }
            var txt = await r.text();
            list && (list.textContent = txt || "");
            count && (count.textContent = "数量：" + countLines(txt));
            setStatus("就绪");
        } catch (e) {
            setStatus("错误：" + (e && e.message ? e.message : String(e)));
        }
    };
    window.envSet = async function () {
        if (!name || !name.value) {
            alert("请输入变量名称");
            return;
        }
        try {
            var fd = new FormData();
            fd.append("name", name.value);
            fd.append("value", value ? value.value : "");
            setStatus("正在保存...");
            var r = await fetch("/env/set", { method: "POST", body: fd });
            var txt = await r.text();
            if (!r.ok) {
                setStatus("错误：" + (txt || r.status));
                return;
            }
            setStatus("已保存");
            window.envRefresh();
        } catch (e) {
            setStatus("错误：" + (e && e.message ? e.message : String(e)));
        }
    };
    window.envUnset = async function () {
        if (!name || !name.value) {
            alert("请输入变量名称");
            return;
        }
        if (!confirm("确定要删除变量 " + name.value + " ？"))
            return;
        try {
            var fd = new FormData();
            fd.append("name", name.value);
            setStatus("正在保存...");
            var r = await fetch("/env/unset", { method: "POST", body: fd });
            var txt = await r.text();
            if (!r.ok) {
                setStatus("错误：" + (txt || r.status));
                return;
            }
            setStatus("已删除");
            window.envRefresh();
        } catch (e) {
            setStatus("错误：" + (e && e.message ? e.message : String(e)));
        }
    };
    window.envReset = async function () {
        if (!confirm("确定要将环境变量重置为默认值？"))
            return;
        try {
            setStatus("正在保存...");
            var r = await fetch("/env/reset", { method: "POST" });
            var txt = await r.text();
            if (!r.ok) {
                setStatus("错误：" + (txt || r.status));
                return;
            }
            setStatus("已重置");
            window.envRefresh();
        } catch (e) {
            setStatus("错误：" + (e && e.message ? e.message : String(e)));
        }
    };
    window.envRestore = async function () {
        if (!file || !file.files || !file.files.length) {
            alert("请选择环境文件");
            return;
        }
        if (!confirm("确定要从文件恢复环境变量？"))
            return;
        try {
            var fd = new FormData();
            fd.append("envfile", file.files[0]);
            setStatus("正在保存...");
            var r = await fetch("/env/restore", { method: "POST", body: fd });
            var txt = await r.text();
            if (!r.ok) {
                setStatus("错误：" + (txt || r.status));
                return;
            }
            setStatus("已恢复");
            window.envRefresh();
        } catch (e) {
            setStatus("错误：" + (e && e.message ? e.message : String(e)));
        }
    };
    window.envRefresh();
}

// ==================== 备份功能 ====================
function makeBackupDownloadName(n) {
    var u = (APP_STATE.sysinfo && APP_STATE.sysinfo.board && APP_STATE.sysinfo.board.model) ? APP_STATE.sysinfo.board.model : "";
    var t = sanitizeFilenameComponent(u) || "board";
    var i = getNowYYYYMMDD();
    var r = String(n || "backup.bin");
    if (r.indexOf("backup_") !== 0) {
        r = r.replace(/^_+/, "");
    }
    if (r.indexOf("backup_" + t + "_") !== 0) {
        r = r.replace(/^backup_/, "backup_" + t + "_");
    }
    if (!/\.[A-Za-z0-9]+$/.test(r)) {
        r = r + ".bin";
    }
    if (!/_\d{8}\.[A-Za-z0-9]+$/.test(r)) {
        r = r.replace(/(\.[A-Za-z0-9]+)$/, "_" + i + "$1");
    }
    return r;
}
function setBackupStatus(n) {
    var t = document.getElementById("backup_status");
    t && (t.style.display = n ? "block" : "none", t.textContent = n || "")
}
function setBackupProgress(n) {
    var t = document.getElementById("bar"), i;
    t && (i = Math.max(0, Math.min(100, parseInt(n || 0))), t.style.display = "block", t.style.setProperty("--percent", i))
}
function backupUpdateRangeHint() {
    var u = document.getElementById("backup_range_hint"), n, i, r;
    u && (n = parseUserLen(document.getElementById("backup_start").value), i = parseUserLen(document.getElementById("backup_end").value), n === null || i === null ? u.textContent = "提示：输入支持十进制、0x 十六进制和 KiB 后缀（如 64KiB）" : (r = i >= n ? i - n : 0, u.textContent = "起始=" + bytesToHuman(n) + ", 结束=" + bytesToHuman(i) + ", 大小=" + bytesToHuman(r)))
}
function backupRefreshI18n() {
    var n = document.getElementById("backup_target"), t, r, u;
    if (!n) return;
    for (t = 0; t < n.options.length; t++) r = n.options[t], r && r.dataset && r.dataset.i18nKey && (r.textContent = window.t(r.dataset.i18nKey));
    for (t = 0; t < n.options.length; t++) {
        r = n.options[t];
        if (!r || !r.dataset) continue;
        r.dataset.kind === "mtd-full" && (u = r.dataset.mtdName || "", r.textContent = "[MTD] " + window.t("backup.target.full_disk") + (u ? " (" + u + ")" : "") + (r.dataset.size ? " (" + bytesToHuman(parseInt(r.dataset.size, 10)) + ")" : ""))
    }
}

// ✅ 关键修复：确保 backup_target 有可见的默认文本
function backupInit() {
    var u = document.getElementById("backup_mode"), r = document.getElementById("backup_range"), n = document.getElementById("backup_target"), s = document.getElementById("backup_target_field"), c = document.getElementById("backup_mode_target_row"), updateBackupUi, f, e;

    // ✅ 精确修复：确保默认选项有文本内容（这是核心问题）
    if (n && n.options.length === 0) {
        var defaultOption = document.createElement("option");
        defaultOption.value = "";
        defaultOption.textContent = "-- 请选择 --"; // ✅ 必须设置此行！
        defaultOption.setAttribute("data-i18n", "label.select");
        n.appendChild(defaultOption);
    }

    function o(t) {
        for (var i = 0; i < n.options.length; i++) if (n.options[i].value === t) return n.selectedIndex = i, true;
        return false
    }
    function h(t) {
        for (var i = 0; i < n.options.length; i++) if (n.options[i].dataset && n.options[i].dataset.kind === t) return n.selectedIndex = i, true;
        return false
    }
    function l() {
        for (var t = 0; t < n.options.length; t++) if (n.options[t].value) {
            n.selectedIndex = t;
            return true
        }
        return false
    }
    function a() {
        var t, i;
        if (!n || n.options.length <= 1) return;
        t = n.options[n.selectedIndex];
        i = t && t.dataset ? t.dataset.kind : "";
        (i === "mmc-part" || i === "mtd-part" || !n.value) && (o("mmc:raw") || h("mtd-full") || l())
    }
    u && r && n && (updateBackupUi = function () {
        var t = u.value === "range";
        t ? (r.style.display = "block", a(), backupUpdateRangeHint()) : (r.style.display = "none");
        s && (s.style.display = t ? "none" : "");
        c && (c.style.gridTemplateColumns = t ? "1fr" : "")
    }, u.onchange = updateBackupUi, f = document.getElementById("backup_start"), e = document.getElementById("backup_end"), f && (f.oninput = backupUpdateRangeHint), e && (e.oninput = backupUpdateRangeHint), updateBackupUi(), setBackupStatus(""), ajax({
        url: "/backup/info",
        done: function (u) {
            var r, e, o, s, f;
            try {
                r = JSON.parse(u)
            } catch (h) {
                setBackupStatus("backupinfo 解析失败");
                return
            }
            e = document.getElementById("backup_info");
            e && (o = [], r.mmc && r.mmc.present ? o.push("MMC: " + (r.mmc.vendor || "") + " " + (r.mmc.product || "")) : o.push("MMC: 未检测到"), r.mtd && r.mtd.present ? o.push("MTD: " + (r.mtd.model || "")) : o.push("MTD: 未检测到"), e.textContent = o.join(" | "));

            // ✅ 精确修复：重新创建选项时确保有文本内容
            n.options.length = 0;
            s = document.createElement("option");
            s.value = "";
            s.textContent = "-- 请选择 --"; // ✅ 关键：显式设置文本内容
            s.setAttribute("data-i18n", "label.select");
            n.appendChild(s);

            r.mmc && r.mmc.present && (f = document.createElement("option"), f.value = "mmc:raw", f.textContent = "[MMC] raw", f.dataset.kind = "mmc-raw", n.appendChild(f), r.mmc.parts && r.mmc.parts.length && r.mmc.parts.forEach(function (t) {
                var i;
                t && t.name && (i = document.createElement("option"), i.value = "mmc:" + t.name, i.textContent = "[MMC] " + t.name + (t.size ? " (" + bytesToHuman(t.size) + ")" : ""), i.dataset.kind = "mmc-part", n.appendChild(i))
            }));
            if (r.mtd && r.mtd.present && r.mtd.parts && r.mtd.parts.length) {
                var c = r.mtd.type, l = c === 3 || c === 4 || c === 8, a = [];
                l && r.mtd.parts.forEach(function (n) {
                    n && n.name && n.master && a.push(n)
                });
                l && a.length && a.forEach(function (p) {
                    var i = document.createElement("option");
                    i.value = "mtd:" + p.name;
                    i.dataset.mtdName = p.name;
                    i.dataset.size = p.size ? String(p.size) : "";
                    i.dataset.kind = "mtd-full";
                    i.textContent = "[MTD] " + p.name + (p.size ? " (" + bytesToHuman(p.size) + ")" : ""); // ✅ 确保有文本
                    n.appendChild(i)
                });
                r.mtd.parts.forEach(function (t) {
                    var i;
                    if (!t || !t.name) return;
                    if (l && t.master) return;
                    i = document.createElement("option");
                    i.value = "mtd:" + t.name;
                    i.textContent = "[MTD] " + t.name + (t.size ? " (" + bytesToHuman(t.size) + ")" : ""); // ✅ 确保有文本
                    i.dataset.kind = "mtd-part";
                    n.appendChild(i)
                })
            }
            n.options.length > 1 && (n.selectedIndex = 1);
            updateBackupUi && updateBackupUi();

            // ✅ 强制应用翻译（确保国际化生效）
            setTimeout(function() {
                if (n) {
                    for (var i = 0; i < n.options.length; i++) {
                        var opt = n.options[i];
                        if (opt.dataset && opt.dataset.i18nKey) {
                            opt.textContent = window.t(opt.dataset.i18nKey) || opt.dataset.i18nKey;
                        }
                    }
                    // 确保第一个选项始终有文本
                    if (n.options.length > 0 && n.options[0].value === "") {
                        n.options[0].textContent = "-- 请选择 --";
                    }
                }
            }, 50);
        }
    }))
}

async function startBackup() {
    var u = document.getElementById("backup_mode"), f = document.getElementById("backup_target"), i, r, e, o, s, h, c, l, a, v, y, p, w, b, k;
    if (!u || !f) return;
    if (i = u.value, r = f.value, !r) {
        alert("请选择目标");
        return
    }
    e = new FormData;
    e.append("mode", i);
    e.append("storage", "auto");
    e.append("target", r);
    if (i === "range") {
        o = document.getElementById("backup_start");
        s = document.getElementById("backup_end");
        if (!o || !s || !o.value || !s.value) {
            alert("请输入有效的起始/结束位置");
            return
        }
        e.append("start", o.value);
        e.append("end", s.value)
    }
    setBackupProgress(0);
    setBackupStatus("正在开始...");
    try {
        h = await fetch("/backup/main", { method: "POST", body: e });
        if (!h.ok) {
            setBackupStatus("HTTP 错误：" + h.status);
            return
        }
        c = h.headers.get("Content-Length");
        l = c ? parseInt(c, 10) : 0;
        a = parseFilenameFromDisposition(h.headers.get("Content-Disposition"));
        a || (a = "backup.bin");
        await ensureSysInfoLoaded();
        a = makeBackupDownloadName(a);
        v = 0;
        if (window.showSaveFilePicker) {
            y = await window.showSaveFilePicker({ suggestedName: a, types: [{ description: "Binary", accept: { "application/octet-stream": [".bin"] } }] });
            p = await y.createWritable();
            w = h.body.getReader();
            while (true) {
                b = await w.read();
                if (b.done) break;
                await p.write(b.value);
                v += b.value.length;
                l ? setBackupProgress(v / l * 100) : setBackupProgress(0);
                setBackupStatus("正在下载：" + bytesToHuman(v) + (l ? " / " + bytesToHuman(l) : ""))
            }
            await p.close();
            setBackupProgress(100);
            setBackupStatus("完成：" + a)
        } else {
            k = [];
            w = h.body.getReader();
            while (true) {
                b = await w.read();
                if (b.done) break;
                k.push(b.value);
                v += b.value.length;
                l ? setBackupProgress(v / l * 100) : setBackupProgress(0);
                setBackupStatus("正在下载：" + bytesToHuman(v) + (l ? " / " + bytesToHuman(l) : ""))
            }
            setBackupProgress(100);
            setBackupStatus("正在准备文件...");
            p = new Blob(k, { type: "application/octet-stream" });
            y = document.createElement("a");
            y.href = URL.createObjectURL(p);
            y.download = a;
            document.body.appendChild(y);
            y.click();
            document.body.removeChild(y);
            setBackupStatus("完成：" + a)
        }
    } catch (d) {
        setBackupStatus("失败：" + (d && d.message ? d.message : String(d)))
    }
}
async function ensureSysInfoLoaded() {
    if (APP_STATE.sysinfo && APP_STATE.sysinfo.board && APP_STATE.sysinfo.board.model)
        return APP_STATE.sysinfo;
    if (APP_STATE._sysinfo_promise)
        return await APP_STATE._sysinfo_promise;
    APP_STATE._sysinfo_promise = (async function () {
        try {
            var r = await fetch("/sysinfo", { method: "GET" });
            if (!r || !r.ok) return null;
            var j = await r.json();
            j && (APP_STATE.sysinfo = j);
            return j;
        } catch (e) {
            return null;
        } finally {
            APP_STATE._sysinfo_promise = null;
        }
    })();
    return await APP_STATE._sysinfo_promise;
}

// ==================== Flash编辑器功能 ====================
function flashSetStatus(n) {
    var s = document.getElementById("flash_status");
    var txt = document.getElementById("flash_status_text");
    var spin = document.getElementById("flash_spinner");
    var busy = n === "上传中..." || n === "恢复中..." || n === "擦除中...";
    if (!s) return;
    s.style.display = n ? "flex" : "none";
    txt && (txt.textContent = n || "");
    spin && (spin.style.display = busy ? "block" : "none");
}
function flashSetProgress(n) {
    var t = document.getElementById("flash_restore_bar"), i;
    if (!t) return;
    if (n === null || n === undefined) {
        t.style.display = "none";
        return;
    }
    i = Math.max(0, Math.min(100, parseInt(n || 0)));
    t.style.display = "block";
    t.style.setProperty("--percent", i)
}
function flashUpdateRangeHint() {
    var u = document.getElementById("flash_range_hint"), n, i, r;
    if (!u) return;
    n = parseUserLen(document.getElementById("flash_start").value);
    i = parseUserLen(document.getElementById("flash_end").value);
    n === null || i === null ? u.textContent = "提示：输入支持十进制、0x 十六进制和 KiB 后缀（如 4KiB）" : (r = i >= n ? i - n : 0, u.textContent = "起始=" + bytesToHuman(n) + ", 结束=" + bytesToHuman(i) + ", 大小=" + bytesToHuman(r))
}
function flashPadHex(n, w) {
    var s = n.toString(16).toUpperCase();
    while (s.length < w) s = "0" + s;
    return s
}
function flashExtractBytes(text) {
    var bytes = [];
    if (!text) return bytes;
    var m = text.match(/[0-9a-fA-F]{2}/g);
    if (!m) return bytes;
    for (var i = 0; i < m.length; i++) bytes.push(parseInt(m[i], 16));
    return bytes
}
function flashPosToByteIndex(text, pos) {
    var i, hex = 0;
    if (!text || pos <= 0) return 0;
    for (i = 0; i < pos && i < text.length; i++) {
        if (/[0-9a-fA-F]/.test(text[i])) hex++;
    }
    return Math.floor(hex / 2)
}
function flashByteIndexToPos(byteIndex) {
    if (!isFinite(byteIndex) || byteIndex < 0) return 0;
    var line = Math.floor(byteIndex / 16);
    var col = byteIndex % 16;
    return line * 48 + col * 3
}
function flashSetCaretToByte(byteIndex) {
    var data = document.getElementById("flash_data");
    if (!data) return;
    var pos = flashByteIndexToPos(byteIndex);
    data.focus();
    data.setSelectionRange(pos, pos);
    flashSyncScroll()
}
function flashFormatHexLines(bytes) {
    var out = [];
    for (var i = 0; i < bytes.length; i++) {
        if (i && i % 16 === 0) out.push("\n");
        out.push(flashPadHex(bytes[i], 2));
        if (i % 16 !== 15 && i !== bytes.length - 1) out.push(" ");
    }
    return out.join("")
}
function flashRenderHexViews() {
    var data = document.getElementById("flash_data");
    var off = document.getElementById("flash_offset");
    var asc = document.getElementById("flash_ascii");
    var start = document.getElementById("flash_start");
    if (!data || !off || !asc) return;
    var bytes = flashExtractBytes(data.value || "");
    var base = start ? parseUserLen(start.value) : 0;
    base = base === null ? 0 : base;
    var asciiLines = [];
    var offLines = [];
    var i, j, rowBytes, c;
    for (i = 0; i < bytes.length; i += 16) {
        rowBytes = bytes.slice(i, i + 16);
        offLines.push("0x" + flashPadHex(base + i, 8));
        for (j = 0; j < rowBytes.length; j++) {
            c = rowBytes[j];
            asciiLines.push(c >= 0x20 && c <= 0x7E ? String.fromCharCode(c) : ".");
        }
        if (rowBytes.length < 16) {
            for (j = rowBytes.length; j < 16; j++) asciiLines.push(" ");
        }
        asciiLines.push("\n");
    }
    off.textContent = offLines.join("\n");
    asc.textContent = asciiLines.join("").replace(/\n$/, "");
}
function flashNormalizeHexInput() {
    var data = document.getElementById("flash_data");
    if (!data) return;
    var bytes = flashExtractBytes(data.value || "");
    data.value = flashFormatHexLines(bytes);
    flashRenderHexViews()
}
function flashAlignInput(keepCaret) {
    var data = document.getElementById("flash_data");
    if (!data) return;
    var caret = data.selectionStart || 0;
    var byteIndex = flashPosToByteIndex(data.value || "", caret);
    var bytes = flashExtractBytes(data.value || "");
    data.value = flashFormatHexLines(bytes);
    if (keepCaret)
        flashSetCaretToByte(byteIndex);
    flashRenderHexViews()
}
function flashFormatData() {
    if (!confirm("确认格式化十六进制数据？将重新排版并清除无效字符。")) return;
    flashAlignInput(false);
    flashSetStatus("已格式化")
}
function flashSnapCaret() {
    var data = document.getElementById("flash_data");
    if (!data) return;
    var caret = data.selectionStart || 0;
    var byteIndex = flashPosToByteIndex(data.value || "", caret);
    flashSetCaretToByte(byteIndex)
}
function flashSyncScroll() {
    var data = document.getElementById("flash_data");
    var off = document.getElementById("flash_offset");
    var asc = document.getElementById("flash_ascii");
    if (!data || !off || !asc) return;
    off.scrollTop = data.scrollTop;
    asc.scrollTop = data.scrollTop
}
function flashJumpToOffset() {
    var jump = document.getElementById("flash_jump");
    var start = document.getElementById("flash_start");
    var data = document.getElementById("flash_data");
    if (!jump || !data) return;
    var target = parseUserLen(jump.value);
    if (target === null) {
        flashSetStatus("偏移超出范围");
        return
    }
    var base = start ? parseUserLen(start.value) : 0;
    base = base === null ? 0 : base;
    var bytes = flashExtractBytes(data.value || "");
    var byteIndex = target - base;
    if (byteIndex < 0 || byteIndex >= bytes.length) {
        flashSetStatus("偏移超出范围");
        return
    }
    flashSetCaretToByte(byteIndex);
    var lineHeight = parseFloat(getComputedStyle(data).lineHeight) || 18;
    var line = Math.floor(byteIndex / 16);
    data.scrollTop = line * lineHeight;
    flashSyncScroll();
    flashSetStatus("")
}
function flashFindLastBefore(str, sub, limit) {
    var idx = -1, cur = str.indexOf(sub);
    while (cur !== -1 && cur < limit) {
        idx = cur;
        cur = str.indexOf(sub, cur + 1)
    }
    return idx
}
function flashParseBackupFilename(name) {
    if (!name) return null;
    var rangeIdx = name.indexOf("_0x"), dashIdx, startStr, endStr, start, end;
    if (rangeIdx < 0) return null;
    dashIdx = name.indexOf("-0x", rangeIdx);
    if (dashIdx < 0) return null;
    startStr = name.slice(rangeIdx + 1, dashIdx);
    endStr = name.slice(dashIdx + 1);
    start = /^0x[0-9a-fA-F]+/.exec(startStr);
    end = /^0x[0-9a-fA-F]+/.exec(endStr);
    if (!start || !end) return null;
    start = parseInt(start[0], 16);
    end = parseInt(end[0], 16);
    if (!isFinite(start) || !isFinite(end) || end <= start) return null;
    var mtdIdx = flashFindLastBefore(name, "_mtd_", rangeIdx);
    var mmcIdx = flashFindLastBefore(name, "_mmc_", rangeIdx);
    var stypeIdx = mtdIdx >= 0 && mmcIdx >= 0 ? (mtdIdx > mmcIdx ? mtdIdx : mmcIdx) : (mtdIdx >= 0 ? mtdIdx : mmcIdx);
    if (stypeIdx < 0) return null;
    var storage = stypeIdx === mtdIdx ? "mtd" : "mmc";
    var seg = name.slice(stypeIdx + 5, rangeIdx);
    if (!seg) return null;
    var parts = seg.split("_");
    var target = parts[parts.length - 1];
    if (!target) return null;
    return { storage: storage, target: target, start: start, end: end }
}
function flashSelectTarget(val) {
    var sel = document.getElementById("flash_target"), i;
    if (!sel) return false;
    for (i = 0; i < sel.options.length; i++) if (sel.options[i].value === val) {
        sel.selectedIndex = i;
        return true
    }
    return false
}

// ==================== 星空流星背景效果 ====================
function initNewYearEffects() {
    if (document.getElementById('newyear-canvas')) return;
    // 创建画布
    var canvas = document.createElement('canvas');
    canvas.id = 'newyear-canvas';
    document.body.insertBefore(canvas, document.body.firstChild);
    var ctx = canvas.getContext('2d');
    if (!ctx) return;
    var width, height;
    var stars = [];
    var shootingStars = [];
    var mouse = { x: 0, y: 0 };

    // ✅ 新增：防抖控制变量
    var lastClickTime = 0;
    var clickCooldown = 300; // 0.3秒冷却时间

    function resize() {
        width = window.innerWidth;
        height = window.innerHeight;
        canvas.width = width;
        canvas.height = height;
    }
    window.addEventListener('resize', resize);
    resize();

    // 星星类
    function Star() {
        this.x = Math.random() * width;
        this.y = Math.random() * height;
        this.size = Math.random() * 2;
        this.speedX = (Math.random() - 0.5) * 0.2;
        this.speedY = (Math.random() - 0.5) * 0.2;
        this.brightness = Math.random();
        this.twinkleSpeed = Math.random() * 0.02 + 0.005;
    }
    Star.prototype.update = function() {
        // 鼠标交互 - 星星会轻微远离鼠标
        var dx = this.x - mouse.x;
        var dy = this.y - mouse.y;
        var distance = Math.sqrt(dx * dx + dy * dy);
        if (distance < 100) {
            var force = (100 - distance) / 100;
            this.x += dx * force * 0.02;
            this.y += dy * force * 0.02;
        }
        this.x += this.speedX;
        this.y += this.speedY;
        this.brightness += this.twinkleSpeed;
        if (this.brightness > 1 || this.brightness < 0.3) {
            this.twinkleSpeed = -this.twinkleSpeed;
        }
        // 边界处理
        if (this.x < 0) this.x = width;
        if (this.x > width) this.x = 0;
        if (this.y < 0) this.y = height;
        if (this.y > height) this.y = 0;
    };
    Star.prototype.draw = function() {
        var alpha = Math.abs(this.brightness);
        ctx.fillStyle = 'rgba(255, 255, 255, ' + alpha + ')';
        ctx.beginPath();
        ctx.arc(this.x, this.y, this.size, 0, Math.PI * 2);
        ctx.fill();
        // 星光十字效果
        if (this.size > 1.5) {
            ctx.strokeStyle = 'rgba(255, 255, 255, ' + (alpha * 0.5) + ')';
            ctx.lineWidth = 0.5;
            ctx.beginPath();
            ctx.moveTo(this.x - this.size * 3, this.y);
            ctx.lineTo(this.x + this.size * 3, this.y);
            ctx.moveTo(this.x, this.y - this.size * 3);
            ctx.lineTo(this.x, this.y + this.size * 3);
            ctx.stroke();
        }
    };

    // 流星类
    function ShootingStar() {
        this.reset();
    }
    ShootingStar.prototype.reset = function() {
        this.x = Math.random() * width + 200;
        this.y = Math.random() * height * 0.5 - 100;
        this.length = Math.random() * 80 + 50;
        this.speed = Math.random() * 10 + 15;
        this.angle = Math.PI / 4 + (Math.random() - 0.5) * 0.2;
        this.opacity = 1;
        this.life = 1;
        this.decay = Math.random() * 0.02 + 0.01;
        this.color = this.getRandomColor();
    };
    ShootingStar.prototype.getRandomColor = function() {
        var colors = [
            '255, 255, 255',  // 白色
            '135, 206, 235',  // 天蓝色
            '255, 215, 0',    // 金色
            '255, 182, 193',  // 粉色
            '173, 216, 230'   // 浅蓝
        ];
        return colors[Math.floor(Math.random() * colors.length)];
    };
    ShootingStar.prototype.update = function() {
        this.x -= Math.cos(this.angle) * this.speed;
        this.y += Math.sin(this.angle) * this.speed;
        this.life -= this.decay;
        this.opacity = this.life;
        if (this.life <= 0 || this.x < -this.length || this.y > height + this.length) {
            this.reset();
        }
        return true;
    };
    ShootingStar.prototype.draw = function() {
        if (this.opacity <= 0) return;
        var tailX = this.x + Math.cos(this.angle) * this.length;
        var tailY = this.y - Math.sin(this.angle) * this.length;
        // 流星尾迹（无渐变，简单线条）
        ctx.strokeStyle = 'rgba(' + this.color + ', ' + this.opacity + ')';
        ctx.lineWidth = 2;
        ctx.lineCap = 'round';
        ctx.beginPath();
        ctx.moveTo(this.x, this.y);
        ctx.lineTo(tailX, tailY);
        ctx.stroke();
        // 流星头部
        ctx.fillStyle = 'rgba(255, 255, 255, ' + this.opacity + ')';
        ctx.beginPath();
        ctx.arc(this.x, this.y, 2, 0, Math.PI * 2);
        ctx.fill();
    };

    // 初始化星星
    for (var i = 0; i < 300; i++) {
        stars.push(new Star());
    }

    // ✅ 自动流星：定时随机生成
    function autoSpawnShootingStar() {
        var star = new ShootingStar();
        // 随机起始位置（从屏幕上方或右侧边缘）
        star.x = Math.random() * width + (Math.random() > 0.5 ? width * 0.3 : 0);
        star.y = Math.random() * height * 0.3 - 50;
        star.length = Math.random() * 80 + 50;
        star.speed = Math.random() * 10 + 15;
        star.angle = Math.PI / 4 + (Math.random() - 0.5) * 0.2;
        shootingStars.push(star);
        // 限制同时存在的流星数量（最多5个）
        if (shootingStars.length > 5) {
            shootingStars.shift();
        }
    }
    // 初始生成1个，之后每2-6秒随机生成一个
    autoSpawnShootingStar();
    setInterval(function() {
        if (shootingStars.length < 3 && Math.random() > 0.3) {
            autoSpawnShootingStar();
        }
    }, 2000 + Math.random() * 4000);

    // 鼠标跟踪
    window.addEventListener('mousemove', function(e) {
        mouse.x = e.clientX;
        mouse.y = e.clientY;
    });

    // ✅ 优化：添加防抖和冷却机制
    document.addEventListener('click', function(e) {
        // 检查点击的是否是交互元素（按钮、链接、输入框等）
        var interactiveElements = ['BUTTON', 'A', 'INPUT', 'TEXTAREA', 'SELECT', 'OPTION'];
        if (interactiveElements.includes(e.target.tagName)) {
            return; // 如果点击的是交互元素，不触发流星
        }

        // ✅ 防抖：检查冷却时间
        var now = Date.now();
        if (now - lastClickTime < clickCooldown) {
            return; // 还在冷却中，不创建新流星
        }
        lastClickTime = now;

        // 创建新的流星
        var newStar = new ShootingStar();
        newStar.x = e.clientX;
        newStar.y = e.clientY - 100; // 从点击点上方开始下落
        newStar.length = Math.random() * 80 + 50;
        newStar.speed = Math.random() * 10 + 15;
        newStar.angle = Math.PI / 4 + (Math.random() - 0.5) * 0.2;
        shootingStars.push(newStar);

        // ✅ 限制同时存在的流星数量（最多3个）
        if (shootingStars.length > 3) {
            shootingStars.shift(); // 移除最早的流星
        }
    });

    // 绘制银河背景
    function drawGalaxy() {
        var gradient = ctx.createRadialGradient(
            width * 0.3, height * 0.3, 0,
            width * 0.5, height * 0.5, width * 0.8
        );
        gradient.addColorStop(0, 'rgba(100, 149, 237, 0.15)');
        gradient.addColorStop(0.5, 'rgba(138, 43, 226, 0.1)');
        gradient.addColorStop(1, 'transparent');
        ctx.fillStyle = gradient;
        ctx.fillRect(0, 0, width, height);
    }

    // 动画循环
    function animate() {
        // 调亮背景：从 rgb(9, 10, 15) 改为 rgb(25, 30, 45)
        ctx.fillStyle = 'rgb(25, 30, 45)';
        ctx.fillRect(0, 0, width, height);
        drawGalaxy();

        // 绘制星星
        stars.forEach(function(star) {
            star.update();
            star.draw();
        });

        // 绘制流星
        for (var i = shootingStars.length - 1; i >= 0; i--) {
            var star = shootingStars[i];
            star.update();
            star.draw();
            // 清理已经消失的流星
            if (star.life <= 0 || star.x < -star.length || star.y > height + star.length) {
                shootingStars.splice(i, 1);
            }
        }

        requestAnimationFrame(animate);
    }
    animate();
}

// ==================== 初始化 ====================
function appInit(n) {
    APP_STATE.page = n || "";

    try {
        var savedLang = localStorage.getItem("lang");
        if (savedLang) APP_STATE.lang = savedLang;
    } catch (e) {}

    var langSelect = document.getElementById("lang_select");
    if (langSelect) langSelect.value = APP_STATE.lang;

    getversion();
    getSysInfo();
    getStorageInfoForSysinfo();

    // 不再调用 generateSidebar，直接高亮导航项
    var currentPage = APP_STATE.page || 'index';
    var navItem = document.getElementById('nav-' + currentPage);
    if (navItem) {
        navItem.classList.add('active');
    }

    (n === "index" || n === "initramfs") && getmtdlayoutlist();
    n === "backup" && backupInit();
    n === "flash" && flashInit();
    n === "console" && consoleInit();
    n === "env" && envInit();

    initNewYearEffects();

    applyTranslation();
    console.log('%c 🧧 新年快乐！恭喜发财！ ', 'background: linear-gradient(135deg, #c41e3a, #8b0000); color: #ffd700; font-size: 20px; padding: 10px 20px; border-radius: 10px;');
}

// ✅ 添加缺失的 flashInit 函数
function flashInit() {
    var target = document.getElementById("flash_target");
    var start = document.getElementById("flash_start");
    var end = document.getElementById("flash_end");
    var data = document.getElementById("flash_data");
    var info = document.getElementById("flash_info");
    var restoreInfo = document.getElementById("flash_restore_info");
    var backup = document.getElementById("flash_backup");
    start && (start.oninput = function () { flashUpdateRangeHint(); flashRenderHexViews(); });
    end && (end.oninput = flashUpdateRangeHint);
    flashUpdateRangeHint();
    flashRenderHexViews();
    flashSetStatus("");
    if (data) {
        data.addEventListener("input", function () { flashAlignInput(true); });
        data.addEventListener("blur", function () { flashAlignInput(false); });
        data.addEventListener("click", flashSnapCaret);
        data.addEventListener("keyup", flashSnapCaret);
        data.addEventListener("scroll", flashSyncScroll);
    }
    backup && (backup.onchange = function () {
        var f = backup.files && backup.files.length ? backup.files[0] : null;
        var d = f ? flashParseBackupFilename(f.name) : null;
        if (!d) {
            restoreInfo && (restoreInfo.textContent = "未识别到有效备份文件名");
            return
        }
        restoreInfo && (restoreInfo.textContent = d.storage + ":" + d.target + " 0x" + d.start.toString(16) + "-0x" + d.end.toString(16));
        flashSelectTarget(d.storage + ":" + d.target);
        start && (start.value = "0x" + d.start.toString(16));
        end && (end.value = "0x" + d.end.toString(16));
        flashUpdateRangeHint();
        flashRenderHexViews();
    });
    ajax({
        url: "/backup/info",
        done: function (u) {
            var r, e, o, s, f;
            try {
                r = JSON.parse(u)
            } catch (h) {
                flashSetStatus("backupinfo 解析失败");
                return
            }
            info && (o = [], r.mmc && r.mmc.present ? o.push("MMC: " + (r.mmc.vendor || "") + " " + (r.mmc.product || "")) : o.push("MMC: 未检测到"), r.mtd && r.mtd.present ? o.push("MTD: " + (r.mtd.model || "")) : o.push("MTD: 未检测到"), info.textContent = o.join(" | "));
            if (!target) return;
            target.options.length = 0;
            s = document.createElement("option");
            s.value = "";
            s.textContent = "-- 请选择 --"; // ✅ 确保有文本
            target.appendChild(s);
            r.mmc && r.mmc.present && (f = document.createElement("option"), f.value = "mmc:raw", f.textContent = "[MMC] raw", target.appendChild(f), r.mmc.parts && r.mmc.parts.length && r.mmc.parts.forEach(function (t) {
                var i;
                t && t.name && (i = document.createElement("option"), i.value = "mmc:" + t.name, i.textContent = "[MMC] " + t.name + (t.size ? " (" + bytesToHuman(t.size) + ")" : ""), target.appendChild(i))
            }));
            r.mtd && r.mtd.present && r.mtd.parts && r.mtd.parts.length && r.mtd.parts.forEach(function (t) {
                var i;
                t && t.name && (i = document.createElement("option"), i.value = "mtd:" + t.name, i.textContent = "[MTD] " + t.name + (t.size ? " (" + bytesToHuman(t.size) + ")" : ""), target.appendChild(i))
            });
            target.options.length > 1 && (target.selectedIndex = 1);
        }
    })
}

// ==================== 全局确认框拦截（不修改任何HTML） ====================
(function() {
    var _nativeConfirm = window.confirm;

    // 动态创建确认框DOM
    function ensureConfirmDOM() {
        if (document.getElementById('confirm_overlay')) return true;
        if (!document.body) return false;

        var overlay = document.createElement('div');
        overlay.id = 'confirm_overlay';
        overlay.className = 'confirm-overlay';
        overlay.style.display = 'none';
        overlay.innerHTML = '<div class="confirm-box">' +
            '<div class="confirm-title" id="confirm_title"></div>' +
            '<div class="confirm-msg" id="confirm_msg"></div>' +
            '<div class="confirm-actions">' +
            '<button class="confirm-btn cancel" id="confirm_no"></button>' +
            '<button class="confirm-btn" id="confirm_yes"></button>' +
            '</div></div>';
        document.body.insertBefore(overlay, document.body.firstChild);

        // 点击遮罩关闭
        overlay.addEventListener('click', function(e) {
            if (e.target === overlay) hideConfirmDialog();
        });
        return true;
    }

    // 显示确认框
window.showConfirmDialog = function(options) {
        if (!ensureConfirmDOM()) {
        if (options.onConfirm) options.onConfirm();
        return;
    }
        var overlay = document.getElementById('confirm_overlay');
    var titleEl = document.getElementById('confirm_title');
    var msgEl = document.getElementById('confirm_msg');
    var yesBtn = document.getElementById('confirm_yes');
    var noBtn = document.getElementById('confirm_no');

        if (titleEl) titleEl.textContent = options.title || '';
    if (msgEl) msgEl.textContent = options.message || '';
    if (yesBtn) {
            yesBtn.textContent = options.yesText || '确定';
        yesBtn.onclick = function() {
            hideConfirmDialog();
            if (options.onConfirm) options.onConfirm();
        };
    }
    if (noBtn) {
            noBtn.textContent = options.noText || '取消';
        noBtn.onclick = function() {
            hideConfirmDialog();
            if (options.onCancel) options.onCancel();
        };
    }

    overlay.style.display = 'flex';
    void overlay.offsetWidth;
    overlay.classList.add('show');
};

window.hideConfirmDialog = function() {
    var overlay = document.getElementById('confirm_overlay');
    if (!overlay) return;
    overlay.classList.remove('show');
    setTimeout(function() {
        overlay.style.display = 'none';
    }, 200);
};

    // ✅ 关键修复：window.confirm 改为异步回调模式
    // 返回一个带有 then 方法的对象，让调用者可以用 await 或 then
    window.confirm = function(message) {
        var result = { value: false, confirmed: false };

    showConfirmDialog({
            title: (typeof t === 'function' ? t('app.name') : null) || 'Recovery Mode',
            message: message || '',
            yesText: (typeof t === 'function' ? t('common.confirm') : null) || '确定',
            noText: (typeof t === 'function' ? t('common.cancel') : null) || '取消',
            onConfirm: function() { result.value = true; result.confirmed = true; },
            onCancel: function() { result.value = false; result.confirmed = true; }
    });

        // 返回 thenable 对象，支持 await confirm("msg")
        result.then = function(onResolve, onReject) {
            var check = function() {
                if (result.confirmed) {
                    if (onResolve) onResolve(result.value);
                } else {
                    setTimeout(check, 50);
    }
            };
            check();
            return result;
        };
        return result;
    };

    // ✅ 新增：异步版本的 confirm，返回 Promise
    window.confirmAsync = function(message) {
        return new Promise(function(resolve) {
            showConfirmDialog({
                title: (typeof t === 'function' ? t('app.name') : null) || 'Recovery Mode',
                message: message || '',
                yesText: (typeof t === 'function' ? t('common.confirm') : null) || '确定',
                noText: (typeof t === 'function' ? t('common.cancel') : null) || '取消',
                onConfirm: function() { resolve(true); },
                onCancel: function() { resolve(false); }
            });
        });
    };

    // ✅ 新增：在 appInit 之后，自动替换 reboot 页面的按钮事件
    function patchRebootButtons() {
        if (APP_STATE.page !== 'reboot') return;

        var btnNormal = document.getElementById('btn_reboot');
        var btnFailsafe = document.getElementById('btn_reboot_failsafe');

        if (btnNormal) {
            btnNormal.onclick = function() { requestRebootAsync('normal'); };
                }
        if (btnFailsafe) {
            btnFailsafe.onclick = function() { requestRebootAsync('failsafe'); };
        }
    }

    // 在 appInit 完成后执行补丁
    var origAppInit = window.appInit;
    window.appInit = function(page) {
        var result = origAppInit(page);
        // 延迟一点确保DOM就绪
        setTimeout(patchRebootButtons, 100);
        return result;
    };

    // ✅ 新增：异步版本的 requestReboot
    window.requestRebootAsync = async function(mode) {
        var status = document.getElementById('reboot_status');
        var btnNormal = document.getElementById('btn_reboot');
        var btnFailsafe = document.getElementById('btn_reboot_failsafe');
        var spinner = document.getElementById('l');
        var pollTimer = null;
        var pollStart = Date.now();
        var confirmKey = mode === 'failsafe' ? 'reboot.confirm_failsafe' : 'reboot.confirm';
        var statusKey = mode === 'failsafe' ? 'reboot.status.failsafe' : 'reboot.status.normal';
        var url = mode === 'failsafe' ? '/reboot-failsafe' : '/reboot';

        // 使用异步确认框
        var confirmed = await confirmAsync(t(confirmKey));
        if (!confirmed) return;

        if (btnNormal) btnNormal.disabled = true;
        if (btnFailsafe) btnFailsafe.disabled = true;
        if (spinner) spinner.style.display = 'block';

        if (status) {
            status.setAttribute('data-i18n', statusKey);
            status.textContent = t(statusKey);
        }

        ajax({
            url: url,
            done: function () {},
            timeout: 30000
        });

        if (mode === 'failsafe') {
            var pollOnce = async function () {
                try {
                    var r = await fetch('/version', { method: 'GET', cache: 'no-store' });
                    if (r && r.ok) {
                        location.href = '/';
                        return;
                    }
                } catch (e) {}
                if (Date.now() - pollStart > 120000) {
                    if (pollTimer) { clearTimeout(pollTimer); pollTimer = null; }
                    return;
                }
                pollTimer = setTimeout(pollOnce, 1500);
            };
            pollTimer = setTimeout(pollOnce, 1500);
        }
    };

    // ✅ 拦截所有指向 reboot.html 的链接点击
    function interceptRebootLinks() {
        document.addEventListener('click', function(e) {
            var el = e.target;
            while (el && el.tagName !== 'A' && el !== document.body) {
                el = el.parentElement;
            }
            if (!el || el.tagName !== 'A') return;

            var href = el.getAttribute('href');
            if (!href || href.indexOf('reboot.html') === -1) return;

            e.preventDefault();
            e.stopPropagation();

            showConfirmDialog({
                title: (typeof t === 'function' ? t('app.name') : null) || 'Recovery Mode',
                message: (typeof t === 'function' ? t('reboot.confirm') : null) || '确定要重启设备吗？',
                yesText: (typeof t === 'function' ? t('common.confirm') : null) || '确定',
                noText: (typeof t === 'function' ? t('common.cancel') : null) || '取消',
                onConfirm: function() {
                    window.location.href = href;
                }
            });
        }, true);
            }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', interceptRebootLinks);
    } else {
        interceptRebootLinks();
    }
})();
