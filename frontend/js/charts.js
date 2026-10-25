/* ==========================================================================
   charts.js - a tiny chart library (no downloads needed, works offline).
     SC.charts.line(el, cfg)    line / area chart with hover tooltip
     SC.charts.bars(el, cfg)    (grouped) bar chart with hover tooltip
     SC.charts.donut(el, cfg)   donut chart with legend
     SC.charts.hbars(el, rows)  horizontal progress bars (HTML)
     SC.charts.ring(pct, ...)   circular percentage gauge
   Charts redraw themselves when their container changes width.
   ========================================================================== */
(function () {
  "use strict";
  const NS = "http://www.w3.org/2000/svg";
  const COLORS = ["#8b5cf6", "#3b82f6", "#22d3ee", "#34d399", "#fb7185", "#fbbf24"];
  let uid = 0;

  function el(tag, attrs, parent) {
    const n = document.createElementNS(NS, tag);
    for (const k in attrs || {}) n.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(n);
    return n;
  }

  // ---- shared tooltip ----
  let tip;
  function showTip(html, x, y) {
    if (!tip) { tip = document.createElement("div"); tip.className = "tooltip"; document.body.appendChild(tip); }
    tip.innerHTML = html;
    tip.hidden = false;
    const r = tip.getBoundingClientRect();
    let left = x + 14, top = y + 14;
    if (left + r.width > window.innerWidth - 8) left = x - r.width - 14;
    if (top + r.height > window.innerHeight - 8) top = y - r.height - 14;
    tip.style.left = Math.max(8, left) + "px";
    tip.style.top = Math.max(8, top) + "px";
  }
  function hideTip() { if (tip) tip.hidden = true; }

  // Draws `draw(width)` now and again whenever the container width changes.
  function mount(box, draw) {
    box.__draw = draw;
    const render = () => {
      box.innerHTML = "";
      box.__w = box.clientWidth;
      if (box.__w > 0) box.__draw(Math.max(220, box.__w));
    };
    render();
    if (!box.__ro && window.ResizeObserver) {
      box.__ro = new ResizeObserver(() => { if (Math.abs(box.clientWidth - (box.__w || 0)) > 3) render(); });
      box.__ro.observe(box);
    }
  }

  // "Nice" axis maximum and tick list: 0, 50, 100, 150 ...
  function niceScale(max, ticks) {
    if (max <= 0) return { max: 1, step: 0.25 };
    const raw = max / ticks;
    const mag = Math.pow(10, Math.floor(Math.log10(raw)));
    const norm = raw / mag;
    const step = (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 5 ? 5 : 10) * mag;
    return { max: Math.ceil(max / step) * step, step };
  }

  function emptyMessage(box, text) {
    if (box.__legend) { box.__legend.remove(); box.__legend = null; }
    box.innerHTML = '<div class="empty" style="min-height:120px"><b>No data yet</b>' + text + "</div>";
  }

  /* ---------------------------- line chart ---------------------------- */
  function line(box, cfg) {
    const all = cfg.series.flatMap((s) => s.values);
    if (!all.length) return emptyMessage(box, cfg.empty || "Run a spell check to fill this chart.");
    mount(box, (W) => {
      const H = cfg.height || 230, m = { l: 48, r: 14, t: 12, b: 28 };
      const iw = W - m.l - m.r, ih = H - m.t - m.b;
      const n = Math.max(...cfg.series.map((s) => s.values.length));
      const sc = cfg.yMax ? { max: cfg.yMax, step: cfg.yMax / 4 } : niceScale(Math.max(...all) * 1.05, 4);
      const x = (i) => m.l + (n === 1 ? iw / 2 : (i / (n - 1)) * iw);
      const y = (v) => m.t + ih - (v / sc.max) * ih;
      const fmt = cfg.yFmt || ((v) => String(v));

      const svg = el("svg", { width: W, height: H, viewBox: `0 0 ${W} ${H}`, role: "img", "aria-label": cfg.label || "chart" }, box);
      const defs = el("defs", {}, svg);
      for (let t = 0; t <= sc.max + 1e-9; t += sc.step) {
        el("line", { x1: m.l, x2: W - m.r, y1: y(t), y2: y(t), class: "grid-line" }, svg);
        const tx = el("text", { x: m.l - 8, y: y(t) + 4, "text-anchor": "end" }, svg);
        tx.textContent = fmt(t);
      }
      // x labels: first, middle, last
      [0, Math.floor((n - 1) / 2), n - 1].filter((v, i, a) => a.indexOf(v) === i).forEach((i) => {
        const t = el("text", { x: x(i), y: H - 6, "text-anchor": i === 0 ? "start" : i === n - 1 ? "end" : "middle" }, svg);
        t.textContent = (cfg.labels && cfg.labels[i]) || "#" + (i + 1);
      });

      cfg.series.forEach((s, si) => {
        const color = s.color || COLORS[si % COLORS.length];
        const pts = s.values.map((v, i) => [x(i), y(v)]);
        if (cfg.area !== false && pts.length > 1) {
          const gid = "lg" + ++uid;
          const g = el("linearGradient", { id: gid, x1: 0, y1: 0, x2: 0, y2: 1 }, defs);
          el("stop", { offset: "0%", "stop-color": color, "stop-opacity": 0.32 }, g);
          el("stop", { offset: "100%", "stop-color": color, "stop-opacity": 0 }, g);
          el("path", { d: "M" + pts.map((p) => p.join(",")).join("L") + `L${pts[pts.length - 1][0]},${y(0)}L${pts[0][0]},${y(0)}Z`, fill: `url(#${gid})` }, svg);
        }
        if (pts.length > 1) el("path", { d: "M" + pts.map((p) => p.join(",")).join("L"), fill: "none", stroke: color, "stroke-width": 2.4, "stroke-linejoin": "round", "stroke-linecap": "round" }, svg);
        if (pts.length <= 40) pts.forEach((p) => el("circle", { cx: p[0], cy: p[1], r: 3.2, fill: "#0d0b24", stroke: color, "stroke-width": 2 }, svg));
      });

      // hover layer
      const guide = el("line", { y1: m.t, y2: m.t + ih, stroke: "rgba(255,255,255,.35)", "stroke-dasharray": "3 4", opacity: 0 }, svg);
      const hit = el("rect", { x: m.l, y: m.t, width: iw, height: ih, fill: "transparent" }, svg);
      hit.addEventListener("mousemove", (e) => {
        const r = svg.getBoundingClientRect();
        const px = e.clientX - r.left;
        const i = n === 1 ? 0 : Math.max(0, Math.min(n - 1, Math.round(((px - m.l) / iw) * (n - 1))));
        guide.setAttribute("x1", x(i)); guide.setAttribute("x2", x(i)); guide.setAttribute("opacity", 1);
        const rows = cfg.series.filter((s) => s.values[i] !== undefined).map((s, si) =>
          `<div><i style="display:inline-block;width:8px;height:8px;border-radius:50%;background:${s.color || COLORS[si % COLORS.length]};margin-right:6px"></i>${s.name}: <b>${fmt(s.values[i])}</b></div>`);
        showTip(`<div class="faint">${(cfg.labels && cfg.labels[i]) || "Check #" + (i + 1)}</div>${rows.join("")}`, e.clientX, e.clientY);
      });
      hit.addEventListener("mouseleave", () => { guide.setAttribute("opacity", 0); hideTip(); });
    });
    if (cfg.series.length > 1 || cfg.legend) legend(box, cfg.series);
  }

  function legend(box, series) {
    const d = document.createElement("div");
    d.className = "legend";
    d.innerHTML = series.map((s, i) => `<span><i style="background:${s.color || COLORS[i % COLORS.length]}"></i>${s.name}</span>`).join("");
    box.__legend && box.__legend.remove();
    box.after(d);
    box.__legend = d;
  }

  /* ----------------------------- bar chart ---------------------------- */
  function bars(box, cfg) {
    if (!cfg.labels.length || !cfg.series.some((s) => s.values.some((v) => v > 0)))
      return emptyMessage(box, cfg.empty || "Nothing to show yet.");
    mount(box, (W) => {
      const H = cfg.height || 220, m = { l: 48, r: 8, t: 12, b: 28 };
      const iw = W - m.l - m.r, ih = H - m.t - m.b;
      const n = cfg.labels.length, ns = cfg.series.length;
      const sc = niceScale(Math.max(...cfg.series.flatMap((s) => s.values)) * 1.05, 4);
      const y = (v) => m.t + ih - (v / sc.max) * ih;
      const fmt = cfg.yFmt || ((v) => (Math.abs(v) >= 1000 ? Math.round(v / 100) / 10 + "k" : String(v)));
      const svg = el("svg", { width: W, height: H, viewBox: `0 0 ${W} ${H}`, role: "img", "aria-label": cfg.label || "chart" }, box);
      const defs = el("defs", {}, svg);
      for (let t = 0; t <= sc.max + 1e-9; t += sc.step) {
        el("line", { x1: m.l, x2: W - m.r, y1: y(t), y2: y(t), class: "grid-line" }, svg);
        el("text", { x: m.l - 8, y: y(t) + 4, "text-anchor": "end" }, svg).textContent = fmt(t);
      }
      const slot = iw / n, gap = Math.min(6, slot * 0.2);
      const bw = Math.max(2, (slot - gap * 2) / ns);
      const every = Math.ceil(n / Math.max(1, Math.floor(iw / 34)));
      cfg.series.forEach((s, si) => {
        const color = s.color || COLORS[si % COLORS.length];
        const gid = "bg" + ++uid;
        const g = el("linearGradient", { id: gid, x1: 0, y1: 0, x2: 0, y2: 1 }, defs);
        el("stop", { offset: "0%", "stop-color": color }, g);
        el("stop", { offset: "100%", "stop-color": s.color2 || color, "stop-opacity": 0.55 }, g);
        s.values.forEach((v, i) => {
          const bx = m.l + i * slot + gap + si * bw;
          const rect = el("rect", { x: bx, y: y(v), width: Math.max(1, bw - 1), height: Math.max(0, m.t + ih - y(v)), rx: Math.min(5, bw / 2), fill: `url(#${gid})` }, svg);
          rect.addEventListener("mousemove", (e) => {
            rect.setAttribute("opacity", 0.8);
            showTip(`<div class="faint">${cfg.labels[i]}</div><div>${s.name}: <b>${(cfg.tipFmt || fmt)(v)}</b></div>`, e.clientX, e.clientY);
          });
          rect.addEventListener("mouseleave", () => { rect.removeAttribute("opacity"); hideTip(); });
        });
      });
      cfg.labels.forEach((l, i) => {
        if (i % every) return;
        el("text", { x: m.l + i * slot + slot / 2, y: H - 8, "text-anchor": "middle" }, svg).textContent = l;
      });
    });
    if (cfg.series.length > 1) legend(box, cfg.series);
  }

  /* ----------------------------- donut chart -------------------------- */
  function donut(box, cfg) {
    const total = cfg.items.reduce((a, b) => a + b.value, 0);
    if (!total) return emptyMessage(box, cfg.empty || "Nothing to show yet.");
    mount(box, (W) => {
      const S = Math.min(200, W), R = S / 2, r = R * 0.62;
      const svg = el("svg", { width: S, height: S, viewBox: `0 0 ${S} ${S}`, style: "margin:0 auto", role: "img", "aria-label": cfg.label || "chart" }, box);
      let a0 = -Math.PI / 2;
      cfg.items.forEach((it, i) => {
        const frac = it.value / total, a1 = a0 + frac * Math.PI * 2 - (cfg.items.length > 1 ? 0.03 : 0);
        const big = a1 - a0 > Math.PI ? 1 : 0;
        const pt = (rad, a) => [R + rad * Math.cos(a), R + rad * Math.sin(a)];
        const [x0, y0] = pt(R - 1, a0), [x1, y1] = pt(R - 1, a1), [x2, y2] = pt(r, a1), [x3, y3] = pt(r, a0);
        const d = frac >= 0.999
          ? `M${R},1 A${R - 1},${R - 1} 0 1 1 ${R - 0.01},1 L${R - 0.01},${R - r} A${r},${r} 0 1 0 ${R},${R - r}Z`
          : `M${x0},${y0} A${R - 1},${R - 1} 0 ${big} 1 ${x1},${y1} L${x2},${y2} A${r},${r} 0 ${big} 0 ${x3},${y3}Z`;
        const path = el("path", { d, fill: it.color || COLORS[i % COLORS.length] }, svg);
        path.addEventListener("mousemove", (e) => showTip(`<b>${it.label}</b><div>${it.value} (${Math.round(frac * 100)}%)</div>`, e.clientX, e.clientY));
        path.addEventListener("mouseleave", hideTip);
        a0 += frac * Math.PI * 2;
      });
      el("text", { x: R, y: R - 2, "text-anchor": "middle", style: "font:700 24px Sora,sans-serif;fill:#eceaff" }, svg).textContent = total;
      el("text", { x: R, y: R + 16, "text-anchor": "middle" }, svg).textContent = cfg.center || "total";
    });
    legend(box, cfg.items.map((it, i) => ({ name: `${it.label} (${it.value})`, color: it.color || COLORS[i % COLORS.length] })));
  }

  /* -------------------------- horizontal bars ------------------------- */
  // rows: [{label, value, color, note}]; log scale keeps tiny and huge values readable.
  function hbars(rows, opts) {
    opts = opts || {};
    const vals = rows.map((r) => Math.max(r.value, 1e-9));
    const lo = Math.log10(Math.min(...vals)), hi = Math.log10(Math.max(...vals));
    const pctOf = (v) => {
      if (!opts.log) return (v / Math.max(...vals)) * 100;
      return hi === lo ? 100 : 12 + ((Math.log10(Math.max(v, 1e-9)) - lo) / (hi - lo)) * 88;
    };
    return '<div class="hbars">' + rows.map((r, i) =>
      `<div class="hbar"><div class="top"><b>${r.label}</b><span>${r.text}</span></div>` +
      `<div class="track"><div class="fill" style="width:${pctOf(r.value).toFixed(1)}%;background:${r.color || COLORS[i]}"></div></div>` +
      (r.note ? `<div class="hint" style="margin-top:4px">${r.note}</div>` : "") + "</div>").join("") + "</div>";
  }

  /* ----------------------------- ring gauge --------------------------- */
  function ring(pct, label, color) {
    const R = 38, C = 2 * Math.PI * R, off = C * (1 - Math.min(100, Math.max(0, pct)) / 100);
    return `<div class="ring"><svg viewBox="0 0 92 92" role="img" aria-label="${label} ${Math.round(pct)}%">
      <circle cx="46" cy="46" r="${R}" fill="none" stroke="rgba(255,255,255,.09)" stroke-width="9"/>
      <circle cx="46" cy="46" r="${R}" fill="none" stroke="${color}" stroke-width="9" stroke-linecap="round"
        stroke-dasharray="${C}" stroke-dashoffset="${off}" transform="rotate(-90 46 46)"/>
      <text x="46" y="52" text-anchor="middle">${Math.round(pct)}%</text></svg><div class="l">${label}</div></div>`;
  }

  window.SC.charts = { line, bars, donut, hbars, ring, COLORS, showTip, hideTip };
})();
