/* ==========================================================================
   trie.js - the Trie page.
     * tree explorer : an SVG drawing of part of the trie (one circle per letter)
     * word tracing   : the nodes visited while spelling one word
     * autocomplete   : most common words below a prefix
     * fuzzy search   : how many nodes the suggestion search touched vs skipped
   ========================================================================== */
(function () {
  "use strict";
  const { $, $$, fmt, esc, charts } = SC;
  const NS = "http://www.w3.org/2000/svg";

  const ui = { prefix: "", depth: 2, width: 7, path: new Set() };
  let inited = false;

  /* ------------------------------ statistics ------------------------------ */
  async function loadStats() {
    const s = await SC.safe(() => SC.api.get("/api/trie/stats"));
    if (!s) return;
    $("#trie-kpis").innerHTML =
      SC.kpi("trie", fmt.int(s.nodes), "Nodes", "one per distinct letter path") +
      SC.kpi("book", fmt.int(s.words), "Words stored", "marked with a word-end flag") +
      SC.kpi("grid", s.maxDepth, "Deepest word", "letters from the root") +
      SC.kpi("hash", fmt.dec(s.avgBranching, 2), "Average branching", "children per non-leaf node") +
      SC.kpi("edit", fmt.pct(s.sharing * 100, 0), "Letters shared", "saved by common prefixes") +
      SC.kpi("copy", fmt.bytes(s.memoryBytes), "Memory", "estimated, 27 child slots per node");
    charts.bars($("#ch-depth"), {
      labels: s.nodesPerDepth.map((_, i) => String(i)), height: 210, label: "Nodes per depth",
      series: [{ name: "Nodes", color: "#22d3ee", color2: "#6366f1", values: s.nodesPerDepth }],
      tipFmt: (v) => fmt.int(v) + " nodes",
    });
  }

  /* ------------------------------ tree explorer ----------------------------- */
  async function loadTree() {
    const t = await SC.safe(() => SC.api.get("/api/trie/tree", { prefix: ui.prefix, depth: ui.depth, maxChildren: ui.width }));
    if (!t) {
      // Unknown prefix: fall back to the root so the page is never empty.
      if (ui.prefix) { ui.prefix = ""; return loadTree(); }
      return;
    }
    drawTree(t);
    renderCrumbs();
    $("#tree-prefix").value = ui.prefix;
  }

  function renderCrumbs() {
    const parts = ui.prefix.split("");
    $("#crumbs").innerHTML =
      '<button class="chip violet" data-p="">root</button>' +
      parts.map((c, i) => `<span class="faint">›</span><button class="chip" data-p="${ui.prefix.slice(0, i + 1)}">${esc(c)}</button>`).join("") +
      (ui.prefix ? '<button class="btn sm" data-p="__up" style="margin-left:8px">' + SC.icon("up") + "Up one level</button>" : "");
  }

  function drawTree(root) {
    const COLW = 78, ROWH = 30, PAD = 28, R = 12;
    let leaf = 0, maxD = 0;
    // Give every node an x (by depth) and a y (leaves stacked, parents centred).
    (function place(n, d) {
      n.d = d; maxD = Math.max(maxD, d);
      if (!n.children.length) { n.y = leaf++ * ROWH; return; }
      n.children.forEach((c) => place(c, d + 1));
      n.y = (n.children[0].y + n.children[n.children.length - 1].y) / 2;
    })(root, 0);

    const W = PAD * 2 + maxD * COLW + 70, H = PAD * 2 + Math.max(0, leaf - 1) * ROWH + 4;
    const svg = document.createElementNS(NS, "svg");
    svg.setAttribute("width", W); svg.setAttribute("height", H);
    svg.setAttribute("role", "img"); svg.setAttribute("aria-label", "Trie drawing");
    const defs = `<defs><linearGradient id="tg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#8b5cf6"/><stop offset="1" stop-color="#3b82f6"/></linearGradient>` +
      `<linearGradient id="tw" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#10b981"/><stop offset="1" stop-color="#0ea5e9"/></linearGradient></defs>`;
    let links = "", nodes = "";
    const X = (n) => PAD + n.d * COLW, Y = (n) => PAD + n.y;

    (function emit(n) {
      n.children.forEach((c) => {
        const x1 = X(n), y1 = Y(n), x2 = X(c), y2 = Y(c), mx = (x1 + x2) / 2;
        const on = ui.path.has(c.prefix) && (ui.path.has(n.prefix) || n.prefix === "");
        links += `<path class="tlink${on ? " on" : ""}" d="M${x1 + R},${y1} C${mx},${y1} ${mx},${y2} ${x2 - R},${y2}"/>`;
        emit(c);
      });
      const hidden = n.childCount - n.children.length;
      const onPath = ui.path.has(n.prefix);
      const fill = n.d === 0 ? "rgba(255,255,255,.1)" : n.isWord ? "url(#tw)" : "url(#tg)";
      const stroke = onPath ? "#22d3ee" : n.isWord ? "#6ee7b7" : "rgba(255,255,255,.28)";
      const label = n.d === 0 ? (n.prefix ? n.ch : "•") : n.ch;
      nodes += `<g class="tnode" data-p="${n.prefix}" transform="translate(${X(n)},${Y(n)})" tabindex="0" role="button" aria-label="${esc(n.prefix || "root")}">` +
        `<circle r="${R}" fill="${fill}" stroke="${stroke}" stroke-width="${onPath ? 3 : 2}"/>` +
        `<text>${esc(label)}</text>` +
        (hidden > 0 || (n.d === maxD && n.childCount > 0) ? `<text class="more" x="${R + 5}" y="0">+${n.d === maxD ? n.childCount : hidden}</text>` : "") + "</g>";
      n.__tip = `<b class="mono">${esc(n.prefix || "root")}</b><div>${fmt.int(n.words)} word${n.words === 1 ? "" : "s"} below</div>` +
        (n.isWord ? `<div style="color:#6ee7b7">Complete word (frequency ${n.freq})</div>` : "");
    })(root);

    svg.innerHTML = defs + links + nodes;
    const box = $("#tree");
    box.innerHTML = "";
    box.appendChild(svg);

    // tooltips
    const byPrefix = {};
    (function index(n) { byPrefix[n.prefix] = n; n.children.forEach(index); })(root);
    $$(".tnode", box).forEach((g) => {
      const n = byPrefix[g.dataset.p];
      g.addEventListener("mousemove", (e) => charts.showTip(n.__tip, e.clientX, e.clientY));
      g.addEventListener("mouseleave", charts.hideTip);
      g.addEventListener("keydown", (e) => { if (e.key === "Enter") g.dispatchEvent(new Event("click", { bubbles: true })); });
    });
  }

  function goPrefix(p) {
    ui.prefix = (p || "").toLowerCase().replace(/[^a-z']/g, "");
    ui.path = new Set();
    loadTree();
  }

  /* ------------------------------ word tracing ------------------------------ */
  async function trace(word) {
    word = word.trim();
    if (!word) return;
    const r = await SC.safe(() => SC.api.get("/api/trie/path", { word }));
    if (!r) return;
    const wd = r.word;
    const steps = r.nodes.map((n, i) => {
      const next = wd[i];
      const kids = n.children.map((c) => `<i class="${c === next ? "take" : ""}">${esc(c)}</i>`).join("");
      const last = i === r.nodes.length - 1;
      const cls = last && r.complete && r.isWord ? "end" : "";
      return `<div class="pstep ${cls}"><span class="pre">${esc(n.prefix || "root")}</span>` +
        `<span class="hint">${fmt.int(n.words)} words below</span><div class="kids" title="Next letters available">${kids}</div></div>`;
    });
    if (!r.complete) {
      steps.push(`<div class="pstep fail"><span class="pre">${esc(wd.slice(0, r.reached + 1))}</span><span class="hint">No such node: nothing in the dictionary continues with “${esc(wd[r.reached])}”.</span></div>`);
    }
    const verdict = r.isWord ? '<span class="chip mint">Complete word</span>'
      : r.complete ? '<span class="chip amber">Valid prefix, but not a complete word</span>'
      : '<span class="chip rose">Falls off the trie</span> at letter ' + (r.reached + 1) + " (this is where a misspelling is detected)";
    $("#path-out").innerHTML = `<div class="path-steps">${steps.join("")}</div><div class="result-line">${verdict}</div>`;

    // Show the path inside the tree too.
    ui.path = new Set(r.nodes.map((n) => n.prefix));
    ui.prefix = wd.slice(0, Math.max(0, Math.min(r.reached, wd.length) - 2));
    ui.depth = 3;
    $$("#tree-depth button").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.v === "3")));
    loadTree();
  }

  /* ------------------------------- autocomplete ------------------------------ */
  async function autocomplete(prefix) {
    prefix = prefix.trim();
    if (!prefix) { $("#ac-out").innerHTML = SC.empty("Start typing", "The most common words with that start appear here."); $("#ac-meta").textContent = ""; return; }
    const r = await SC.safe(() => SC.api.get("/api/trie/autocomplete", { prefix, limit: 10 }));
    if (!r) return;
    $("#ac-meta").textContent = `${fmt.int(r.total)} words · ${fmt.ms(r.ms)}`;
    if (!r.words.length) { $("#ac-out").innerHTML = SC.empty("No words start with “" + prefix + "”", "Check the letters, or try a shorter start."); return; }
    const max = r.words[0].freq || 1;
    $("#ac-out").innerHTML = r.words.map((w) =>
      `<div class="ac-item"><button data-w="${esc(w.word)}">${esc(w.word)}</button><span class="hint">${w.freq}</span><div class="bar"><i style="width:${(w.freq / max) * 100}%"></i></div></div>`).join("");
  }

  /* -------------------------------- fuzzy search ------------------------------ */
  async function fuzzy(word) {
    word = word.trim();
    if (!word) return;
    const r = await SC.safe(() => SC.api.get("/api/suggest", { word, distance: $("#fuzzy-dist").value, max: 8 }));
    if (!r) return;
    const f = r.fuzzy, pct = (f.nodesVisited / f.trieNodes) * 100;
    const sugs = r.suggestions.length
      ? '<div class="sugs" style="margin-top:12px">' + r.suggestions.map((s) =>
          `<span class="chip violet">${esc(s.word)} <span class="faint">${s.distance} edit${s.distance > 1 ? "s" : ""}</span></span>`).join("") + "</div>"
      : '<p class="note" style="margin-top:12px">No word within that many edits.</p>';
    $("#fuzzy-out").innerHTML =
      (r.known ? '<div class="chip mint" style="margin-bottom:12px">This is already a correct word</div>' : "") +
      `<div class="prune"><div class="note">Visited <b>${fmt.int(f.nodesVisited)}</b> of ${fmt.int(f.trieNodes)} nodes (<b>${fmt.dec(pct, 1)}%</b>) in ${fmt.ms(f.ms)}.</div>` +
      `<div class="meter"><i class="v" style="width:${Math.max(1.5, pct)}%"></i><i class="p" style="flex:1"></i></div>` +
      `<div class="note"><b>${fmt.int(f.subtreesPruned)}</b> branches were dropped as soon as they could not be close enough, which skipped <b>${fmt.int(f.wordsPruned)}</b> dictionary words. ${f.candidates} candidate${f.candidates === 1 ? "" : "s"} were within range.</div></div>` + sugs;
  }

  SC.pages.trie = {
    title: "Trie",
    subtitle: "The prefix tree that finds words near a misspelling without checking the whole dictionary.",
    show(query) {
      if (!inited) {
        inited = true;
        $("#tree").addEventListener("click", (e) => {
          const g = e.target.closest(".tnode");
          if (g) { goPrefix(g.dataset.p); }
        });
        $("#crumbs").addEventListener("click", (e) => {
          const b = e.target.closest("[data-p]");
          if (!b) return;
          goPrefix(b.dataset.p === "__up" ? ui.prefix.slice(0, -1) : b.dataset.p);
        });
        $("#tree-form").addEventListener("submit", (e) => { e.preventDefault(); goPrefix($("#tree-prefix").value); });
        $("#tree-depth").addEventListener("click", (e) => {
          const b = e.target.closest("button"); if (!b) return;
          ui.depth = Number(b.dataset.v);
          $$("#tree-depth button").forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
          loadTree();
        });
        $("#tree-width").addEventListener("change", (e) => { ui.width = Number(e.target.value); loadTree(); });
        $("#path-form").addEventListener("submit", (e) => { e.preventDefault(); trace($("#path-word").value); });
        $("#fuzzy-form").addEventListener("submit", (e) => { e.preventDefault(); fuzzy($("#fuzzy-word").value); });
        $("#ac-prefix").addEventListener("input", SC.debounce((e) => autocomplete(e.target.value), 150));
        $("#ac-out").addEventListener("click", (e) => {
          const b = e.target.closest("button[data-w]");
          if (b) { $("#path-word").value = b.dataset.w; trace(b.dataset.w); }
        });
      }
      loadStats();
      loadTree().then(() => {
        if (query && query.word) { $("#path-word").value = query.word; trace(query.word); }
      });
    },
  };
})();
