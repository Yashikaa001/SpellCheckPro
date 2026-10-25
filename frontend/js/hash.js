/* ==========================================================================
   hash.js - the Hash table page.
     * bucket map   : a canvas with one square per bucket (colour = chain length)
     * lookup lab    : shows the hash being calculated step by step
     * bucket viewer : the chain of words stored in one bucket
   The C++ HashTable does all the real work; this file only draws.
   ========================================================================== */
(function () {
  "use strict";
  const { $, fmt, esc, charts } = SC;

  // colour for a bucket holding 0, 1, 2, ... 6+ words
  const HEAT = ["rgba(255,255,255,0.05)", "#3b3a8f", "#5b45c9", "#8b5cf6", "#c084fc", "#f0abfc", "#fb7185"];
  const FORMULA = {
    djb2: (c) => `h × 33 + ${c}`,
    fnv1a: (c) => `(h XOR ${c}) × 16777619`,
    sdbm: (c) => `${c} + (h << 6) + (h << 16) − h`,
    sum: (c) => `h + ${c}`,
  };

  let ov = null, inited = false, cell = 4, cols = 1, selected = -1;

  async function loadOverview() {
    ov = await SC.safe(() => SC.api.get("/api/hash/overview"));
    if (!ov) return;
    renderAll();
  }

  function renderAll() {
    $("#hash-fn").value = ov.function;
    const size = $("#hash-size");
    if (![...size.options].some((o) => Number(o.value) === ov.buckets)) {
      const o = document.createElement("option");
      o.value = ov.buckets; o.textContent = fmt.int(ov.buckets) + " buckets";
      size.appendChild(o);
    }
    size.value = ov.buckets;

    $("#hash-kpis").innerHTML =
      SC.kpi("book", fmt.int(ov.entries), "Words stored", "in " + fmt.int(ov.buckets) + " buckets") +
      SC.kpi("grid", fmt.dec(ov.loadFactor, 2), "Load factor", "words ÷ buckets") +
      SC.kpi("hash", fmt.int(ov.collisions), "Collisions", fmt.pct((ov.collisions / Math.max(1, ov.entries)) * 100, 0) + " of words share a bucket") +
      SC.kpi("trie", ov.longestChain, "Longest chain", "avg " + fmt.dec(ov.avgChainLength, 2) + " in used buckets") +
      SC.kpi("edit", fmt.int(ov.emptyBuckets), "Empty buckets", fmt.pct((ov.emptyBuckets / ov.buckets) * 100, 0) + " of the table");

    drawHeat();
    $("#heat-legend").innerHTML = HEAT.map((c, i) => `<span><i style="background:${c}"></i>${i === 6 ? "6+" : i} word${i === 1 ? "" : "s"}</span>`).join("");

    const h = ov.histogram;
    charts.bars($("#ch-hist"), {
      labels: h.map((_, i) => String(i)), height: 210, label: "Chain lengths",
      series: [{ name: "Buckets", color: "#8b5cf6", color2: "#3b82f6", values: h }],
      tipFmt: (v) => fmt.int(v) + " buckets",
    });

    $("#hash-worst").innerHTML = ov.worst.length
      ? ov.worst.slice(0, 5).map((w) =>
          `<button data-b="${w.bucket}"><div class="w"><b class="mono">Bucket #${w.bucket}</b><span class="chip rose">${w.length} words</span></div>` +
          `<span class="hint">${w.words.map(esc).join(", ")}${w.length > w.words.length ? "…" : ""}</span></button>`).join("")
      : SC.empty("No collisions", "Every word has a bucket to itself.");
  }

  /* ---------------------------- bucket heat map ---------------------------- */
  function drawHeat() {
    const canvas = $("#heat");
    const W = canvas.parentElement.clientWidth;
    if (!ov || W <= 0) return;
    const n = ov.buckets;
    cell = Math.max(2, Math.min(16, Math.floor(Math.sqrt((W * 340) / n))));
    cols = Math.max(1, Math.floor(W / cell));
    const rows = Math.ceil(n / cols), H = rows * cell;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = W * dpr; canvas.height = H * dpr;
    canvas.style.height = H + "px";
    const ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, W, H);
    const pad = cell >= 6 ? 1 : 0;
    for (let i = 0; i < n; i++) {
      ctx.fillStyle = HEAT[Math.min(6, ov.loads[i])];
      ctx.fillRect((i % cols) * cell, Math.floor(i / cols) * cell, cell - pad, cell - pad);
    }
    if (selected >= 0) {
      ctx.strokeStyle = "#22d3ee"; ctx.lineWidth = 2;
      ctx.strokeRect((selected % cols) * cell - 1, Math.floor(selected / cols) * cell - 1, cell + 1, cell + 1);
    }
  }

  function bucketAt(e) {
    const canvas = $("#heat"), r = canvas.getBoundingClientRect();
    const x = Math.floor(((e.clientX - r.left) / r.width) * (canvas.width / (window.devicePixelRatio || 1)) / cell);
    const y = Math.floor((e.clientY - r.top) / cell);
    const i = y * cols + x;
    return x >= 0 && x < cols && i >= 0 && i < ov.buckets ? i : -1;
  }

  /* ----------------------------- bucket viewer ----------------------------- */
  async function openBucket(index) {
    const r = await SC.safe(() => SC.api.get("/api/hash/bucket", { index }));
    if (!r) return;
    selected = r.bucket; drawHeat();
    $("#bucket-card").hidden = false;
    $("#bucket-title").textContent = "Bucket #" + r.bucket + " · " + r.words.length + " word" + (r.words.length === 1 ? "" : "s");
    $("#bucket-body").innerHTML = r.words.length
      ? `<div class="chain"><span class="bucket-tag">#${r.bucket}</span>` +
        r.words.map((w) => `<span class="arrow">→</span><span class="node">${esc(w.word)}<small>hash ${w.hash}</small></span>`).join("") +
        '<span class="arrow">→</span><span class="faint">end</span></div>' +
        `<p class="note" style="margin-top:12px">${r.words.length > 1 ? "These words all produced the same bucket number, so they are stored together as a linked chain. A lookup compares them one by one." : "Only one word lives here, so a lookup needs a single comparison."}</p>`
      : SC.empty("Empty bucket", "No dictionary word hashes to this bucket.");
    $("#bucket-card").scrollIntoView({ behavior: "smooth", block: "nearest" });
  }

  /* ------------------------------- lookup lab ------------------------------ */
  async function lookup(word) {
    word = word.trim();
    if (!word) return;
    const r = await SC.safe(() => SC.api.get("/api/hash/lookup", { word }));
    if (!r) return;
    const start = r.steps.length ? r.steps[0].before : 0;
    const f = FORMULA[r.function] || FORMULA.djb2;

    const stepsHtml =
      `<div class="steps-scroll"><table class="steps"><tr><th>Letter</th><th>Code</th><th>Calculation</th><th>Result</th></tr>` +
      `<tr><td colspan="3" class="faint">Start value</td><td class="n">${fmt.int(start)}</td></tr>` +
      r.steps.map((s) => `<tr><td><span class="ch">${esc(s.ch)}</span></td><td class="n">${s.code}</td>` +
        `<td class="n">${esc(f(s.code))}</td><td class="n">${fmt.int(s.after)}</td></tr>`).join("") + "</table></div>";

    const chainHtml = r.chain.length
      ? `<div class="chain"><span class="bucket-tag">#${r.bucket}</span>` +
        r.chain.map((c, i) => {
          const cls = c.word === r.word ? "hit" : (r.found ? (i < r.position ? "cmp" : "") : "cmp");
          return `<span class="arrow">→</span><span class="node ${cls}">${esc(c.word)}<small>${c.hash}</small></span>`;
        }).join("") + "</div>"
      : `<div class="chain"><span class="bucket-tag">#${r.bucket}</span><span class="arrow">→</span><span class="faint">empty bucket</span></div>`;

    const verdict = r.found
      ? `<span class="chip mint">Found</span> after <b>${r.comparisons}</b> comparison${r.comparisons > 1 ? "s" : ""} (position ${r.position} of ${r.chainLength})`
      : `<span class="chip rose">Not in dictionary</span> checked ${r.comparisons} word${r.comparisons === 1 ? "" : "s"} in the bucket`;

    $("#hash-out").innerHTML =
      `<div class="note"><b>Step 1.</b> Turn “${esc(r.word)}” into a number with <b>${esc(r.function)}</b>:</div>` + stepsHtml +
      `<div class="note" style="margin:14px 0 6px"><b>Step 2.</b> Pick a bucket: <span class="mono">${fmt.int(r.hash)} mod ${fmt.int(r.buckets)} = ${r.bucket}</span></div>` +
      `<div class="note"><b>Step 3.</b> Compare only the words in that bucket:</div>${chainHtml}` +
      `<div class="result-line">${verdict}</div>` +
      `<div class="hbars" style="margin-top:14px">` +
      charts.hbars([
        { label: "Hash table", value: r.hashNs, text: fmt.ns(r.hashNs), color: "#8b5cf6" },
        { label: "Trie", value: r.trieNs, text: fmt.ns(r.trieNs), color: "#22d3ee" },
      ]) + "</div>" +
      `<p class="hint" style="margin-top:8px">Measured over 20,000 repeated lookups. Both are far below a microsecond, so either structure answers “is this a word?” instantly.</p>`;
    // refresh the dashboard counter lazily (lookups are counted by the server)
  }

  async function rebuild(btn) {
    const r = await SC.busy(btn, () => SC.safe(() =>
      SC.api.post("/api/hash/rebuild", { fn: $("#hash-fn").value, buckets: $("#hash-size").value })));
    if (!r) return;
    ov = r.overview; selected = -1;
    $("#bucket-card").hidden = true;
    renderAll();
    SC.toast(`Re-hashed ${fmt.int(ov.entries)} words in ${fmt.ms(r.rebuildMs)}`, "ok");
  }

  SC.pages.hash = {
    title: "Hash table",
    subtitle: "How the dictionary is stored for instant lookups, and what happens when words collide.",
    show(query) {
      if (!inited) {
        inited = true;
        $("#hash-rebuild").addEventListener("click", (e) => rebuild(e.currentTarget));
        $("#hash-form").addEventListener("submit", (e) => { e.preventDefault(); lookup($("#hash-word").value); });
        $("#bucket-close").addEventListener("click", () => { $("#bucket-card").hidden = true; selected = -1; drawHeat(); });
        $("#hash-worst").addEventListener("click", (e) => { const b = e.target.closest("button[data-b]"); if (b) openBucket(Number(b.dataset.b)); });

        const canvas = $("#heat");
        canvas.addEventListener("click", (e) => { const i = bucketAt(e); if (i >= 0) openBucket(i); });
        canvas.addEventListener("mousemove", (e) => {
          const i = bucketAt(e);
          if (i < 0) return charts.hideTip();
          const n = ov.loads[i];
          charts.showTip(`<b class="mono">Bucket #${i}</b><div>${n} word${n === 1 ? "" : "s"}${n > 1 ? " (collision)" : ""}</div>`, e.clientX, e.clientY);
        });
        canvas.addEventListener("mouseleave", charts.hideTip);
        let lastW = 0;
        new ResizeObserver(() => { const w = canvas.parentElement.clientWidth; if (Math.abs(w - lastW) > 3) { lastW = w; drawHeat(); } }).observe(canvas.parentElement);
      }
      loadOverview().then(() => {
        if (query && query.word) { $("#hash-word").value = query.word; lookup(query.word); }
      });
    },
  };
})();
