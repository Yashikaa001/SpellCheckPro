/* ==========================================================================
   core.js - shared helpers used by every page.
     SC.$ / SC.$$   short versions of querySelector
     SC.esc         makes text safe to put inside HTML
     SC.api         talks to the C++ server (GET / POST, JSON in and out)
     SC.fmt         number and time formatting
     SC.icon        small inline SVG icons
     SC.toast       little pop-up messages
     SC.pages       every page registers itself here (see main.js)
   ========================================================================== */
(function () {
  "use strict";

  const SC = (window.SC = { pages: {}, state: {} });

  SC.$ = (sel, root) => (root || document).querySelector(sel);
  SC.$$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));

  // Escape text so user input can never be treated as HTML.
  SC.esc = (s) =>
    String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));

  // Run `fn` at most once every `ms` milliseconds after the last call (used for typing).
  SC.debounce = function (fn, ms) {
    let t;
    return function (...args) {
      clearTimeout(t);
      t = setTimeout(() => fn.apply(this, args), ms);
    };
  };

  /* ---------- API client ---------- */
  async function request(method, path, params, body) {
    const url = new URL(path, window.location.origin);
    Object.entries(params || {}).forEach(([k, v]) => {
      if (v !== undefined && v !== null && v !== "") url.searchParams.set(k, v);
    });
    let res;
    try {
      res = await fetch(url, {
        method,
        headers: body !== undefined ? { "Content-Type": "text/plain; charset=utf-8" } : {},
        body,
      });
    } catch (e) {
      throw new Error("Cannot reach the C++ server. Is spellcheck still running?");
    }
    let data = null;
    try { data = await res.json(); } catch (e) { /* not JSON */ }
    if (!res.ok) throw new Error((data && data.error) || "Server error (" + res.status + ")");
    return data;
  }
  SC.api = {
    get: (path, params) => request("GET", path, params),
    post: (path, params, body) => request("POST", path, params, body === undefined ? "" : body),
  };

  /* ---------- formatting ---------- */
  const nf = new Intl.NumberFormat("en-US");
  SC.fmt = {
    int: (n) => nf.format(Math.round(n || 0)),
    dec: (n, d = 1) => (n || 0).toFixed(d),
    pct: (n, d = 1) => (n || 0).toFixed(d) + "%",
    // 18.4 ns, 1.2 µs, 3.4 ms ...
    ns(n) {
      n = n || 0;
      if (n < 1000) return (n < 100 ? n.toFixed(1) : Math.round(n)) + " ns";
      if (n < 1e6) return (n / 1000).toFixed(n < 1e4 ? 2 : 1) + " µs";
      return (n / 1e6).toFixed(2) + " ms";
    },
    ms(ms) {
      ms = ms || 0;
      if (ms < 1) return Math.round(ms * 1000) + " µs";
      if (ms < 1000) return ms.toFixed(ms < 10 ? 2 : 1) + " ms";
      return (ms / 1000).toFixed(2) + " s";
    },
    bytes(b) {
      if (b < 1024) return b + " B";
      if (b < 1048576) return (b / 1024).toFixed(1) + " KB";
      return (b / 1048576).toFixed(1) + " MB";
    },
    ago(ts) {
      const s = Math.max(0, (Date.now() - ts) / 1000);
      if (s < 45) return "just now";
      if (s < 3600) return Math.round(s / 60) + " min ago";
      if (s < 86400) return Math.round(s / 3600) + " h ago";
      return new Date(ts).toLocaleDateString(undefined, { month: "short", day: "numeric" });
    },
    clock: (ts) => new Date(ts).toLocaleString(undefined, { dateStyle: "medium", timeStyle: "short" }),
  };

  /* ---------- icons (24x24, drawn with strokes) ---------- */
  const ICONS = {
    grid: '<rect x="3" y="3" width="7" height="7" rx="1.6"/><rect x="14" y="3" width="7" height="7" rx="1.6"/><rect x="3" y="14" width="7" height="7" rx="1.6"/><rect x="14" y="14" width="7" height="7" rx="1.6"/>',
    check: '<circle cx="12" cy="12" r="9"/><path d="M8.5 12.5l2.5 2.5 4.5-5"/>',
    hash: '<path d="M5 9h14M5 15h14M10 4L8 20M16 4l-2 16"/>',
    trie: '<circle cx="12" cy="5" r="2"/><circle cx="6" cy="19" r="2"/><circle cx="18" cy="19" r="2"/><path d="M12 7v4M12 11H6v6M12 11h6v6"/>',
    swap: '<path d="M7 7h11l-3-3M17 17H6l3 3"/>',
    book: '<path d="M4 5a2 2 0 012-2h13v15H6a2 2 0 00-2 2V5z"/><path d="M19 18v3H6"/>',
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    copy: '<rect x="9" y="9" width="11" height="11" rx="2"/><path d="M5 15V6a2 2 0 012-2h9"/>',
    play: '<path d="M8 5l11 7-11 7z"/>',
    refresh: '<path d="M20 11a8 8 0 10-2.3 5.7M20 4v7h-7"/>',
    trash: '<path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="M20 20l-4-4"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    bolt: '<path d="M13 3L5 14h6l-1 7 8-11h-6z"/>',
    up: '<path d="M12 19V5M6 11l6-6 6 6"/>',
    edit: '<path d="M4 20h4L19 9l-4-4L4 16z"/>',
    zap: '<path d="M13 3L5 14h6l-1 7 8-11h-6z"/>',
  };
  SC.icon = (name) =>
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' +
    (ICONS[name] || "") + "</svg>";
  SC.hydrateIcons = (root) =>
    SC.$$("i[data-icon]", root).forEach((el) => {
      el.outerHTML = SC.icon(el.dataset.icon);
    });

  /* ---------- toast messages ---------- */
  SC.toast = function (message, kind) {
    const box = SC.$("#toasts");
    const el = document.createElement("div");
    el.className = "toast " + (kind || "");
    el.textContent = message;
    box.appendChild(el);
    setTimeout(() => el.remove(), kind === "err" ? 6000 : 3200);
  };

  // Runs an async action; shows an error toast if the server complains.
  SC.safe = async function (fn) {
    try { return await fn(); }
    catch (e) { SC.toast(e.message || String(e), "err"); return undefined; }
  };

  // Disables a button and shows a busy look while an async action runs.
  SC.busy = async function (btn, fn) {
    if (btn) btn.classList.add("busy");
    try { return await fn(); } finally { if (btn) btn.classList.remove("busy"); }
  };

  SC.copyText = async function (text) {
    try {
      await navigator.clipboard.writeText(text);
      return true;
    } catch (e) {
      // Fallback for browsers / pages without clipboard permission.
      const ta = document.createElement("textarea");
      ta.value = text;
      ta.style.cssText = "position:fixed;opacity:0";
      document.body.appendChild(ta);
      ta.select();
      let ok = false;
      try { ok = document.execCommand("copy"); } catch (e2) { /* ignore */ }
      ta.remove();
      return ok;
    }
  };

  /* ---------- tiles shown on several pages ---------- */
  SC.kpi = function (icon, value, label, sub) {
    return (
      '<article class="glass kpi"><span class="badge">' + SC.icon(icon) + "</span>" +
      '<div class="v">' + value + '</div><div class="l">' + SC.esc(label) + "</div>" +
      (sub ? '<div class="s">' + sub + "</div>" : "") + "</article>"
    );
  };
  SC.empty = (title, text) => '<div class="empty"><b>' + SC.esc(title) + "</b>" + SC.esc(text || "") + "</div>";
})();
