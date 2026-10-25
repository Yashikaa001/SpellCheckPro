/* ==========================================================================
   dashboard.js - the Dashboard page.
   Pulls /api/stats and draws tiles + charts, and runs the two built-in tests
   (speed benchmark and suggestion-accuracy test) on the real C++ backend.
   ========================================================================== */
(function () {
  "use strict";
  const { $, fmt, esc, charts } = SC;

  const TYPE_LABELS = {
    wrong_letter: "Wrong letter",
    extra_letter: "Extra letter",
    missing_letter: "Missing letter",
    swapped_letters: "Swapped letters",
    multiple: "Several mistakes",
    unknown: "No match found",
  };
  const TYPE_COLORS = {
    wrong_letter: "#fbbf24", extra_letter: "#fb7185", missing_letter: "#34d399",
    swapped_letters: "#22d3ee", multiple: "#8b5cf6", unknown: "#7a77ab",
  };

  let inited = false;

  async function load() {
    const s = await SC.safe(() => SC.api.get("/api/stats"));
    if (!s) return;
    const ses = s.session;

    // ---- KPI tiles ----
    $("#kpis").innerHTML =
      SC.kpi("book", fmt.int(s.dictionary.words), "Words in dictionary", s.dictionary.customWords ? "+" + s.dictionary.customWords + " added by you" : "Loaded into hash table and trie") +
      SC.kpi("check", fmt.int(ses.checks), "Spell checks run", fmt.int(ses.wordsChecked) + " words checked") +
      SC.kpi("grid", ses.wordsChecked ? fmt.pct(ses.accuracy) : "—", "Spelling accuracy", ses.wordsChecked ? fmt.int(ses.misspelled) + " misspelled words" : "Check some text to measure") +
      SC.kpi("hash", ses.lookups ? fmt.ns(ses.avgHashNs) : "—", "Hash lookup time", ses.lookups ? "Trie lookup: " + fmt.ns(ses.avgTrieNs) : "per word, measured") +
      SC.kpi("trie", ses.avgNodesVisited ? fmt.ms(ses.avgSuggestMs) : "—", "Suggestion time", ses.avgNodesVisited ? "avg " + fmt.int(ses.avgNodesVisited) + " trie nodes visited" : "per misspelled word") +
      SC.kpi("edit", fmt.int(ses.correctionsApplied), "Corrections applied", ses.correctionsApplied ? fmt.pct(ses.firstChoiceRate, 0) + " used the first suggestion" : "Click a suggestion to apply it");

    // ---- charts from the recent checks ----
    const recent = s.recent;
    const labels = recent.map((r) => new Date(r.time).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" }));
    charts.line($("#ch-speed"), {
      labels, yFmt: (v) => fmt.ns(v).replace(" ", ""), label: "Lookup speed per check",
      series: [
        { name: "Hash table", color: "#8b5cf6", values: recent.map((r) => r.hashNs) },
        { name: "Trie", color: "#22d3ee", values: recent.map((r) => r.trieNs) },
      ],
    });
    charts.line($("#ch-acc"), {
      labels, yFmt: (v) => Math.round(v) + "%", yMax: 100, label: "Accuracy per check",
      series: [{ name: "Accuracy", color: "#34d399", values: recent.map((r) => r.accuracy) }],
    });

    // ---- mistakes donut ----
    const types = Object.entries(s.errorTypes).map(([k, v]) => ({ label: TYPE_LABELS[k] || k, value: v, color: TYPE_COLORS[k] }));
    charts.donut($("#ch-types"), { items: types, center: "mistakes", empty: "Check some text with typos." });

    // ---- most missed ----
    const top = s.topMisspelled;
    $("#top-missed").innerHTML = top.length
      ? '<div class="mini-list">' + top.map((t) =>
          `<div class="it"><span class="mono">${esc(t.word)}</span><span class="muted">${t.count}×</span>` +
          `<div class="bar"><i style="width:${(t.count / top[0].count) * 100}%"></i></div></div>`).join("") + "</div>"
      : SC.empty("No mistakes recorded", "Misspelled words from your checks are ranked here.");

    // ---- word length distribution ----
    const L = s.dictionary.lengths;
    charts.bars($("#ch-lengths"), {
      labels: L.map((_, i) => String(i)).slice(1), height: 200, label: "Dictionary word lengths",
      series: [{ name: "Words", color: "#6366f1", color2: "#3b82f6", values: L.slice(1) }],
      tipFmt: (v) => fmt.int(v) + " words",
    });

    // ---- data structure snapshot ----
    $("#ds-snapshot").innerHTML =
      '<dl class="kv">' +
      `<dt>Hash function</dt><dd>${esc(s.hash.function)}</dd>` +
      `<dt>Buckets</dt><dd>${fmt.int(s.hash.buckets)}</dd>` +
      `<dt>Load factor</dt><dd>${fmt.dec(s.hash.loadFactor, 2)}</dd>` +
      `<dt>Collisions</dt><dd>${fmt.int(s.hash.collisions)}</dd>` +
      `<dt>Longest chain</dt><dd>${s.hash.longestChain}</dd>` +
      `<dt>Trie nodes</dt><dd>${fmt.int(s.trie.nodes)}</dd>` +
      `<dt>Trie depth</dt><dd>${s.trie.maxDepth}</dd>` +
      `<dt>Trie memory</dt><dd>${fmt.bytes(s.trie.memoryBytes)}</dd>` +
      `<dt>Server uptime</dt><dd>${Math.round(ses.uptimeSeconds / 60)} min</dd></dl>`;
  }

  async function runBenchmark(btn) {
    const n = $("#bench-n").value;
    const r = await SC.busy(btn, () => SC.safe(() => SC.api.get("/api/benchmark", { n })));
    if (!r) return;
    const rows = [
      { label: "Hash table", value: r.hash.nsPerLookup, text: fmt.ns(r.hash.nsPerLookup) + " per lookup", color: "#8b5cf6" },
      { label: "Trie", value: r.trie.nsPerLookup, text: fmt.ns(r.trie.nsPerLookup) + " per lookup", color: "#22d3ee" },
      { label: "Scanning a plain list", value: r.linear.nsPerLookup, text: fmt.ns(r.linear.nsPerLookup) + " per lookup", color: "#fb7185" },
    ];
    const speedup = r.linear.nsPerLookup / Math.max(1, r.hash.nsPerLookup);
    $("#bench-out").innerHTML =
      charts.hbars(rows, { log: true }) +
      `<p class="note" style="margin-top:16px">Hash table was <b>${fmt.int(speedup)}× faster</b> than scanning all ${fmt.int(r.dictionarySize)} words. ` +
      `Test used ${fmt.int(r.hits)} real words and ${fmt.int(r.misses)} made-up ones. Bars use a log scale so all three stay visible.</p>`;
  }

  async function runSelfTest(btn) {
    const r = await SC.busy(btn, () => SC.safe(() => SC.api.get("/api/selftest")));
    if (!r) return;
    const misses = r.cases.filter((c) => c.rank !== 1);
    $("#self-out").innerHTML =
      '<div class="rings">' +
      charts.ring(r.top1Pct, "First suggestion", "#34d399") +
      charts.ring(r.top3Pct, "In top 3", "#22d3ee") +
      charts.ring(r.top5Pct, "In top 5", "#8b5cf6") + "</div>" +
      `<p class="note" style="margin:14px 0 10px">${r.tested} misspellings tested, ${fmt.ms(r.avgMs)} per word on average.` +
      (misses.length ? " Where the right word was not first:" : " Every right word ranked first.") + "</p>" +
      (misses.length
        ? '<div class="tbl-wrap" style="max-height:200px"><table class="tbl"><tr><th>Typo</th><th>Expected</th><th>We suggested</th><th>Rank</th></tr>' +
          misses.map((c) => `<tr><td class="mono">${esc(c.typo)}</td><td>${esc(c.expected)}</td><td>${esc(c.top || "—")}</td><td class="num">${c.rank || "not found"}</td></tr>`).join("") +
          "</table></div>"
        : "");
  }

  SC.pages.dashboard = {
    title: "Dashboard",
    subtitle: "Live numbers from the C++ spell checker: speed, accuracy and the structures behind it.",
    show() {
      if (!inited) {
        inited = true;
        $("#bench-run").addEventListener("click", (e) => runBenchmark(e.currentTarget));
        $("#self-run").addEventListener("click", (e) => runSelfTest(e.currentTarget));
        $("#reset-stats").addEventListener("click", async () => {
          await SC.safe(() => SC.api.post("/api/metrics/reset"));
          SC.toast("Session statistics reset", "ok");
          load();
        });
      }
      load();
    },
  };
})();
