// ==================== 工具函数 ====================

function ajax(n) {
    var t, i;
    t = window.XMLHttpRequest ? new XMLHttpRequest : new ActiveXObject("Microsoft.XMLHTTP");
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
    var n = new Date, t = n.getFullYear(), i = n.getMonth() + 1, r = n.getDate();
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

// ==================== 核心功能函数 ====================

function upload(n) {
    var o = document.getElementById("file").files[0],
        u, f, e, r, i, s;
    if (!o) {
        alert("请选择文件");
        return;
    }
    u = document.getElementById("form");
    u && (u.style.display = "none");
    f = document.getElementById("hint");
    f && (f.style.display = "none");
    e = document.getElementById("bar");
    e && (e.style.display = "block");
    r = new FormData;
    r.append(n, o);
    i = document.getElementById("mtd_layout_label");
    i && i.options.length > 0 && (s = i.selectedIndex, r.append("mtd_layout", i.options[s].value));
    ajax({
        url: "/upload",
        data: r,
        done: function (n) {
            var i, r, u, f, e;
            n == "fail" ? location = "/fail.html" : (i = n.split(" "), r = document.getElementById("size"), r && (r.style.display = "block", r.innerHTML = "大小：" + i[0]), u = document.getElementById("md5"), u && (u.style.display = "block", u.innerHTML = "MD5：" + i[1]), f = document.getElementById("mtd"), f && i[2] && (f.style.display = "block", f.innerHTML = "MTD 布局：" + i[2]), e = document.getElementById("upgrade"), e && (e.style.display = "block"))
        },
        progress: function (n) {
            if (n.total) {
                var i = parseInt(n.loaded / n.total * 100),
                    t = document.getElementById("bar");
                t && (t.style.display = "block", t.style.setProperty("--percent", i))
            }
        }
    })
}

function getversion() {
    ajax({
        url: "/version",
        done: function (n) {
            var t = document.getElementById("version");
            t && (t.innerHTML = n + "- dailook")
        }
    })
}

function getmtdlayoutlist() {
    ajax({
        url: "/getmtdlayout",
        done: function (n) {
            var i, f, e, u, r, o;
            if (n != "error" && (i = n.split(";"), f = document.getElementById("current_mtd_layout"), f && (f.innerHTML = "当前 MTD 布局：" + i[0]), e = document.getElementById("choose_mtd_layout"), e && (e.textContent = "选择 MTD 布局："), u = document.getElementById("mtd_layout_label"), u)) {
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
        n.textContent = "正在加载系统信息...";
        return
    }
    u = i.board || {};
    f = i.ram || {};
    e = [];
    e.push("设备：" + (u.model || "未知"));
    f.size !== undefined && f.size !== null && f.size !== 0 ? e.push("内存：" + bytesToHuman(f.size)) : e.push("内存：未知");

    n.textContent = e.join("\n")
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
            renderSysInfo();
        }
    });
}

function updateGptNavVisibility() {
    var el = document.querySelector("#sidebar [data-nav-id='gpt']");
    if (!el) return;
    var bi = APP_STATE.backupinfo;
    if (bi && bi.mmc && bi.mmc.present === false) {
        el.style.display = "none";
    } else {
        el.style.display = "";
    }
}

// ==================== 控制台 ====================

function consoleInit() {
    var out = document.getElementById("console_out");
    var cmd = document.getElementById("console_cmd");
    var status = document.getElementById("console_status");
    var token = document.getElementById("console_token");
    var persistKey = "failsafe_console_output";
    var persistMax = 200000;

    APP_STATE.console = APP_STATE.console || {
        running: false,
        pollTimer: null,
        history: [],
        histPos: -1,
        tokenKey: "failsafe_console_token"
    };

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

// ==================== 环境变量 ====================

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

// ==================== 备份 ====================

function makeBackupDownloadName(n) {
    var u = (APP_STATE.sysinfo && APP_STATE.sysinfo.board && APP_STATE.sysinfo.board.model) ? APP_STATE.sysinfo.board.model : "";
    var t = sanitizeFilenameComponent(u) || "board";
    var i = getNowYYYYMMDD();
    var r = String(n || "backup.bin");

    r.indexOf("backup_") === 0 || (r = "backup_" + r.replace(/^_+/, ""));

    r.indexOf("backup_" + t + "_") === 0 || (r = r.replace(/^backup_/, "backup_" + t + "_"));

    /\.[A-Za-z0-9]+$/.test(r) || (r = r + ".bin");

    /_\d{8}\.[A-Za-z0-9]+$/.test(r) || (r = r.replace(/(\.[A-Za-z0-9]+)$/, "_" + i + "$1"));

    return r
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
        r.dataset.kind === "mtd-full" && (u = r.dataset.mtdName || "", r.textContent = "[MTD] 全盘备份" + (u ? " (" + u + ")" : "") + (r.dataset.size ? " (" + bytesToHuman(parseInt(r.dataset.size, 10)) + ")" : ""))
    }
}

function backupInit() {
    var u = document.getElementById("backup_mode"), r = document.getElementById("backup_range"), n = document.getElementById("backup_target"), s = document.getElementById("backup_target_field"), c = document.getElementById("backup_mode_target_row"), updateBackupUi, f, e;
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
            n.options.length = 0;
            s = document.createElement("option");
            s.value = "";
            s.textContent = "-- 请选择 --";
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
                    n.appendChild(i)
                });

                r.mtd.parts.forEach(function (t) {
                    var i;
                    if (!t || !t.name) return;
                    if (l && t.master) return;
                    i = document.createElement("option");
                    i.value = "mtd:" + t.name;
                    i.textContent = "[MTD] " + t.name + (t.size ? " (" + bytesToHuman(t.size) + ")" : "");
                    i.dataset.kind = "mtd-part";
                    n.appendChild(i)
                })
            }
            n.options.length > 1 && (n.selectedIndex = 1);
            backupRefreshI18n();
            updateBackupUi && updateBackupUi()
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

// ==================== 初始化 ====================

function appInit(n) {
    APP_STATE.page = n || "";
    getversion();
    getSysInfo();
    getStorageInfoForSysinfo();
    (n === "index" || n === "initramfs") && getmtdlayoutlist();
    n === "backup" && backupInit();
    n === "console" && consoleInit();
    n === "env" && envInit()
}

function startup() {
    appInit("index")
}

// ==================== 全局状态 ====================

var APP_STATE = {
    page: ""
};