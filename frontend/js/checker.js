/* ==========================================================================
   checker.js - the Spell checker page.

   How the highlighting works
   --------------------------
   A <textarea> cannot colour individual words. So we stack two layers:
     * #backdrop  - a <div> with the SAME text and font; misspelled words are
                    wrapped in <mark> (the text itself is transparent)
     * #text      - the real <textarea>, transparent background, on top
   The marks show through the textarea, so the words look underlined.

   Positions
   ---------
   The server reports each mistake as [start, end) in UTF-16 units, which is
   exactly how JavaScript indexes strings, so text.slice(start, end) works.
   ========================================================================== */
(function () {
  "use strict";
  const { $, fmt, esc } = SC;

  const SAMPLE =
    "Teh quick brown fox jumpd over the lazy dgo. I definately recieve emails every wich day, " +
    "but my adress is wrong. Accomodation in Londn was beautifull and the weather was realy nice.\n\n" +
    "Please sepatate the NASA files from the others, and dont forget to check the grammer.";

  const TYPE_LABELS = {
    wrong_letter: "wrong letter", extra_letter: "extra letter", missing_letter: "missing letter",
    swapped_letters: "swapped letters", multiple: "several mistakes", unknown: "no match",
  };

  const st = {
    errors: [],           // from the server
    checkedText: "",      // the exact text those errors belong to
    summary: null,
    active: -1,           // index of the selected error
    seq: 0,               // guards against out-of-order responses
    ignored: new Set(JSON.parse(localStorage.getItem("scp.ignored") || "[]")),
  };
  let ta, backdrop, pop, inited = false;

  /* ------------------------------ helpers ------------------------------ */
  const saveIgnored = () => { try { localStorage.setItem("scp.ignored", JSON.stringify([...st.ignored])); } catch (e) { /* ignore */ } };

  function options(record) {
    return {
      maxSuggestions: $("#opt-max").value,
      maxDistance: $("#opt-dist").value,
      ignoreCaps: $("#opt-caps").checked ? 1 : 0,
      ignore: [...st.ignored].join(","),
      record: record ? 1 : 0,
    };
  }

  function autoGrow() {
    ta.style.height = "auto";
    ta.style.height = Math.max(340, ta.scrollHeight) + "px";
  }

  /* ------------------------------ checking ----------------------------- */
  async function runCheck(record) {
    const text = ta.value;
    const seq = ++st.seq;
    if (!text.trim()) { clearResults(); return; }
    let data;
    try {
      data = await SC.api.post("/api/check", options(record), text);
    } catch (e) {
      if (record) SC.toast(e.message, "err");
      return;
    }
    if (seq !== st.seq || ta.value !== text) return;   // the text changed while we waited
    st.errors = data.errors;
    st.checkedText = text;
    st.summary = data.summary;
    st.active = -1;
    renderAll();
    if (record) {
      const s = data.summary;
      SC.toast(s.misspelled ? `${s.misspelled} misspelled word${s.misspelled > 1 ? "s" : ""} found` : "No spelling mistakes found", s.misspelled ? "" : "ok");
    }
  }

  function clearResults() {
    st.errors = []; st.checkedText = ""; st.summary = null; st.active = -1;
    renderAll();
  }

  /* ------------------------------ rendering ---------------------------- */
  function renderAll() {
    renderBackdrop();
    renderStatus();
    renderIssues();
    renderExplain();
    renderCorrected();
    $("#btn-fixall").disabled = !st.errors.some((e) => e.suggestions.length);
    $("#btn-apply").disabled = $("#btn-fixall").disabled;
    $("#issue-count").textContent = st.errors.length;
    $("#issue-count").className = "chip " + (st.errors.length ? "rose" : "mint");
    closePop();
  }

  function renderBackdrop() {
    const text = ta.value;
    let html = "", pos = 0;
    if (st.checkedText === text) {
      st.errors.forEach((e, i) => {
        html += esc(text.slice(pos, e.start));
        html += `<mark class="err${i === st.active ? " active" : ""}" data-i="${i}">${esc(text.slice(e.start, e.end))}</mark>`;
        pos = e.end;
      });
    }
    html += esc(text.slice(pos));
    backdrop.innerHTML = html + "​";   // keeps a trailing new line visible
  }

  function renderStatus() {
    const s = st.summary, bar = $("#statusbar");
    if (!s) { bar.innerHTML = "<span>Ready. Click an underlined word to see suggestions.</span>"; return; }
    const cls = s.accuracy >= 95 ? "good" : s.accuracy >= 80 ? "mid" : "low";
    bar.innerHTML =
      `<span>Words <b>${s.words}</b></span><span>Misspelled <b>${s.misspelled}</b></span>` +
      `<span class="acc">Accuracy <b class="${cls}">${fmt.pct(s.accuracy)}</b></span>` +
      (s.ignored ? `<span>Skipped <b>${s.ignored}</b></span>` : "") +
      `<span>Hash <b>${fmt.ns(s.hashNsPerLookup)}</b>/word</span><span>Trie <b>${fmt.ns(s.trieNsPerLookup)}</b>/word</span>` +
      `<span>Suggestions <b>${fmt.ms(s.suggestMs)}</b></span><span>Total <b>${fmt.ms(s.totalMs)}</b></span>`;
  }

  function renderIssues() {
    const box = $("#issues");
    if (!st.summary) { box.innerHTML = SC.empty("Nothing to show yet", "Check some text to see mistakes here."); return; }
    if (!st.errors.length) { box.innerHTML = SC.empty("No mistakes", "Every checked word is in the dictionary."); return; }
    box.innerHTML = st.errors.map((e, i) =>
      `<div class="issue${i === st.active ? " active" : ""}" data-i="${i}" role="button" tabindex="0">` +
      `<div class="t"><b>${esc(e.word)}</b><span class="chip">${TYPE_LABELS[e.type] || e.type}</span></div>` +
      (e.suggestions.length
        ? '<div class="sugs">' + e.suggestions.map((s, k) =>
            `<button class="chip ${k === 0 ? "violet" : ""}" data-i="${i}" data-k="${k}" title="${s.distance} edit${s.distance > 1 ? "s" : ""}">${esc(s.word)}</button>`).join("") + "</div>"
        : '<span class="hint">No dictionary word is within the edit limit.</span>') + "</div>").join("");
  }

  function renderExplain() {
    const box = $("#explain");
    const e = st.errors[st.active >= 0 ? st.active : 0];
    if (!e) { box.innerHTML = SC.empty("Pick a misspelled word", "See which structures answered, and how much work the trie skipped."); return; }
    const f = e.fuzzy;
    const visitedPct = (f.nodesVisited / f.trieNodes) * 100;
    box.innerHTML =
      `<div class="prune"><div class="note"><b>1. Hash table</b>: “${esc(e.word.toLowerCase())}” hashed to bucket <span class="mono">#${e.bucket}</span> and was <b style="color:var(--rose)">not found</b>, so it is misspelled.</div>` +
      `<div class="note"><b>2. Trie</b>: searched for words within ${$("#opt-dist").value} edit${$("#opt-dist").value > 1 ? "s" : ""}. It touched <b>${fmt.int(f.nodesVisited)}</b> of ${fmt.int(f.trieNodes)} nodes (${fmt.dec(visitedPct, 1)}%).</div>` +
      `<div class="meter" title="Share of the trie that was searched"><i class="v" style="width:${Math.max(1.5, visitedPct)}%"></i><i class="p" style="flex:1"></i></div>` +
      `<div class="note">Pruned <b>${fmt.int(f.subtreesPruned)}</b> branches, skipping <b>${fmt.int(f.wordsPruned)}</b> words without comparing them. A plain scan would compare every word.</div>` +
      `<div class="note"><b>3. Ranking</b>: ${f.candidates} candidate${f.candidates === 1 ? "" : "s"} within range, sorted by edit distance then how common the word is. Took <b>${fmt.ms(f.ms)}</b>.</div></div>`;
  }

  // Text with the FIRST suggestion applied to each misspelled word.
  function correctedParts() {
    const text = st.checkedText, parts = [];
    let pos = 0;
    st.errors.forEach((e) => {
      parts.push({ t: text.slice(pos, e.start) });
      if (e.suggestions.length) parts.push({ t: e.suggestions[0].word, changed: true });
      else parts.push({ t: text.slice(e.start, e.end) });
      pos = e.end;
    });
    parts.push({ t: text.slice(pos) });
    return parts;
  }

  function renderCorrected() {
    const box = $("#corrected");
    if (!st.summary || st.checkedText !== ta.value) {
      box.innerHTML = '<span class="faint">Corrections appear here. The first suggestion is used for every misspelled word.</span>';
      return;
    }
    box.innerHTML = correctedParts().map((p) => (p.changed ? `<ins>${esc(p.t)}</ins>` : esc(p.t))).join("");
  }

  /* ------------------------- applying suggestions ---------------------- */
  function logCorrection(from, to, rank) {
    SC.api.post("/api/history/correction", { from, to, rank }).catch(() => {});
  }

  function applySuggestion(i, k) {
    const e = st.errors[i], s = e && e.suggestions[k];
    if (!e || !s || st.checkedText !== ta.value) return;
    const text = ta.value;
    ta.value = text.slice(0, e.start) + s.word + text.slice(e.end);
    logCorrection(e.word, s.word, k);
    afterEdit();
    ta.focus();
    const caret = e.start + s.word.length;
    ta.setSelectionRange(caret, caret);
  }

  function fixAll() {
    if (st.checkedText !== ta.value) return;
    const fixes = st.errors.filter((e) => e.suggestions.length);
    if (!fixes.length) return;
    fixes.forEach((e) => logCorrection(e.word, e.suggestions[0].word, 0));
    ta.value = correctedParts().map((p) => p.t).join("");
    SC.toast(`Applied ${fixes.length} correction${fixes.length > 1 ? "s" : ""}`, "ok");
    afterEdit();
  }

  // After the text was changed by a click (not by typing): refresh layout and re-check.
  function afterEdit() {
    autoGrow();
    st.checkedText = "";
    renderBackdrop();
    closePop();
    runCheck(false);
  }

  /* ------------------------------ popover ------------------------------ */
  function closePop() { pop.hidden = true; }

  function openPop(i) {
    const e = st.errors[i];
    const mark = backdrop.querySelector(`mark[data-i="${i}"]`);
    if (!e || !mark) return;
    st.active = i;
    backdrop.querySelectorAll("mark.active").forEach((m) => m.classList.remove("active"));
    mark.classList.add("active");
    renderIssues(); renderExplain();

    pop.innerHTML =
      `<div class="ph"><b>${esc(e.word)}</b><span class="chip">${TYPE_LABELS[e.type] || ""}</span></div>` +
      (e.suggestions.length
        ? '<div class="sug-list">' + e.suggestions.map((s, k) =>
            `<button class="sug" data-k="${k}"><b>${esc(s.word)}</b><span>${s.distance} edit${s.distance > 1 ? "s" : ""}</span>` +
            `<div class="sim"><i style="width:${Math.round(s.similarity * 100)}%"></i></div></button>`).join("") + "</div>"
        : '<p class="note">No dictionary word is close enough. You can add it to the dictionary.</p>') +
      '<div class="pf"><button class="btn sm" data-act="ignore">Ignore</button><button class="btn sm" data-act="add">Add to dictionary</button></div>';
    pop.dataset.i = i;
    pop.hidden = false;

    const r = mark.getBoundingClientRect(), pr = pop.getBoundingClientRect();
    let left = Math.min(Math.max(12, r.left), window.innerWidth - pr.width - 12);
    let top = r.bottom + 8;
    if (top + pr.height > window.innerHeight - 8) top = Math.max(8, r.top - pr.height - 8);
    pop.style.left = left + "px";
    pop.style.top = top + "px";
    const first = pop.querySelector(".sug");
    if (first && document.activeElement !== ta) first.focus();
  }

  function errorAtCaret() {
    if (st.checkedText !== ta.value || ta.selectionStart !== ta.selectionEnd) return -1;
    const c = ta.selectionStart;
    return st.errors.findIndex((e) => c >= e.start && c <= e.end);
  }

  async function addToDictionary(word) {
    const r = await SC.safe(() => SC.api.post("/api/dictionary/add", { word }));
    if (r) { SC.toast(r.message, "ok"); runCheck(false); }
  }

  function renderIgnored() {
    const row = $("#ignored-row");
    row.hidden = st.ignored.size === 0;
    $("#ignored-list").innerHTML = [...st.ignored].map((w) => `<span class="chip">${esc(w)}</span>`).join("");
  }

  /* ------------------------------- wiring ------------------------------ */
  function init() {
    ta = $("#text"); backdrop = $("#backdrop"); pop = $("#pop");

    ta.addEventListener("input", () => {
      autoGrow();
      st.checkedText = "";            // old positions are no longer valid
      renderBackdrop();
      closePop();
      if ($("#opt-live").checked) liveCheck();
    });
    const liveCheck = SC.debounce(() => runCheck(false), 450);

    ta.addEventListener("click", () => { const i = errorAtCaret(); if (i >= 0) openPop(i); else closePop(); });
    ta.addEventListener("keyup", (e) => {
      if (e.key === "Escape") return closePop();
      if (e.key.startsWith("Arrow")) { const i = errorAtCaret(); if (i < 0) closePop(); }
    });
    window.addEventListener("resize", () => { autoGrow(); closePop(); });
    document.addEventListener("keydown", (e) => { if (e.key === "Escape") closePop(); });
    document.addEventListener("mousedown", (e) => { if (!pop.hidden && !pop.contains(e.target) && e.target !== ta) closePop(); });

    pop.addEventListener("click", (e) => {
      const i = Number(pop.dataset.i), err = st.errors[i];
      const sug = e.target.closest(".sug"), act = e.target.closest("[data-act]");
      if (sug) applySuggestion(i, Number(sug.dataset.k));
      else if (act && err) {
        if (act.dataset.act === "ignore") { st.ignored.add(err.word.toLowerCase()); saveIgnored(); renderIgnored(); closePop(); runCheck(false); }
        else addToDictionary(err.word);
      }
    });

    $("#issues").addEventListener("click", (e) => {
      const chip = e.target.closest("button[data-k]");
      if (chip) return applySuggestion(Number(chip.dataset.i), Number(chip.dataset.k));
      const card = e.target.closest(".issue");
      if (card) {
        const i = Number(card.dataset.i);
        const mark = backdrop.querySelector(`mark[data-i="${i}"]`);
        if (mark) mark.scrollIntoView({ block: "center", behavior: "smooth" });
        setTimeout(() => openPop(i), 250);
      }
    });
    $("#issues").addEventListener("keydown", (e) => { if (e.key === "Enter" && e.target.classList.contains("issue")) e.target.click(); });

    $("#btn-check").addEventListener("click", (e) => SC.busy(e.currentTarget, () => runCheck(true)));
    $("#btn-fixall").addEventListener("click", fixAll);
    $("#btn-apply").addEventListener("click", fixAll);
    $("#btn-sample").addEventListener("click", () => { ta.value = SAMPLE; afterEdit(); runCheck(true); });
    $("#btn-clear").addEventListener("click", () => { ta.value = ""; autoGrow(); clearResults(); ta.focus(); });
    $("#btn-copy").addEventListener("click", async () => {
      if (!st.summary || st.checkedText !== ta.value) return SC.toast("Check the text first, then copy the corrected version.", "err");
      const ok = await SC.copyText(correctedParts().map((p) => p.t).join(""));
      SC.toast(ok ? "Corrected text copied" : "Could not copy. Select the text and press Ctrl+C.", ok ? "ok" : "err");
    });
    $("#btn-unignore").addEventListener("click", () => { st.ignored.clear(); saveIgnored(); renderIgnored(); runCheck(false); });
    ["#opt-caps", "#opt-max", "#opt-dist"].forEach((s) => $(s).addEventListener("change", () => runCheck(false)));
    renderIgnored();
  }

  SC.pages.checker = {
    title: "Spell checker",
    subtitle: "Type or paste text. Mistakes are found with the hash table; suggestions come from the trie and edit distance.",
    show() {
      if (!inited) { inited = true; init(); }
      // Text sent here from the History page.
      if (SC.state.pendingText) {
        ta.value = SC.state.pendingText;
        SC.state.pendingText = null;
        autoGrow(); st.checkedText = ""; renderBackdrop();
        runCheck(false);
      } else if (ta.value) {
        autoGrow(); renderBackdrop();
      }
      autoGrow();
    },
  };
})();
