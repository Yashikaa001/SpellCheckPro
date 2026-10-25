/* ==========================================================================
   dictionary.js - the Dictionary explorer page.
   Search, filter, sort and page through every word the C++ server knows,
   and add new words (they go into the hash table and trie immediately).
   ========================================================================== */
(function () {
  "use strict";
  const { $, fmt, esc } = SC;

  const q = { page: 1, letter: "" };
  let inited = false;

  async function load() {
    const [sortKey, dir] = $("#d-sort").value.split("-");
    const text = q.letter && !$("#d-q").value ? q.letter : $("#d-q").value;
    const mode = q.letter && !$("#d-q").value ? "prefix" : $("#d-mode").value;
    const r = await SC.safe(() => SC.api.get("/api/dictionary", {
      q: text, mode, sort: sortKey, dir: dir || "asc", page: q.page, pageSize: $("#d-size").value,
      minLen: $("#d-min").value, maxLen: $("#d-max").value || 100, customOnly: $("#d-custom").checked ? 1 : 0,
    }));
    if (!r) return;

    $("#dict-total").textContent = fmt.int(r.total) + " words";
    const maxF = 800;
    $("#d-table").innerHTML =
      "<thead><tr><th>Word</th><th>Letters</th><th>How common</th><th>Hash bucket</th><th></th></tr></thead><tbody>" +
      (r.items.length
        ? r.items.map((w) =>
            `<tr><td class="w">${esc(w.word)}${w.custom ? ' <span class="chip violet">yours</span>' : ""}</td><td class="num">${w.length}</td>` +
            `<td class="num"><span class="freqbar" style="width:${Math.max(4, (w.freq / maxF) * 90)}px"></span>${w.freq}</td>` +
            `<td class="num mono">#${w.bucket}</td>` +
            `<td><a class="chip" href="#/hash?word=${encodeURIComponent(w.word)}">Hash</a> <a class="chip" href="#/trie?word=${encodeURIComponent(w.word)}">Trie</a></td></tr>`).join("")
        : `<tr><td colspan="5"><div class="empty" style="border:0">${"<b>No words match</b>Try fewer letters or clear the filters."}</div></td></tr>`) +
      "</tbody>";

    $("#d-pager").innerHTML =
      `<span class="hint">Page ${r.page} of ${fmt.int(r.pages)}</span><div class="row">` +
      `<button class="btn sm" data-p="1" ${r.page <= 1 ? "disabled" : ""}>First</button>` +
      `<button class="btn sm" data-p="${r.page - 1}" ${r.page <= 1 ? "disabled" : ""}>Previous</button>` +
      `<button class="btn sm" data-p="${r.page + 1}" ${r.page >= r.pages ? "disabled" : ""}>Next</button>` +
      `<button class="btn sm" data-p="${r.pages}" ${r.page >= r.pages ? "disabled" : ""}>Last</button></div>`;
  }

  async function glance() {
    const s = await SC.safe(() => SC.api.get("/api/stats"));
    if (!s) return;
    const d = s.dictionary;
    $("#dict-glance").innerHTML =
      `<dl class="kv"><dt>Total words</dt><dd>${fmt.int(d.words)}</dd><dt>From the word list</dt><dd>${fmt.int(d.baseWords)}</dd>` +
      `<dt>Added by you</dt><dd>${fmt.int(d.customWords)}</dd><dt>Average length</dt><dd>${fmt.dec(d.avgLength, 1)} letters</dd></dl>`;
  }

  const reload = () => { q.page = 1; load(); };

  SC.pages.dictionary = {
    title: "Dictionary",
    subtitle: "Browse every word the checker knows, see where each one lives in the hash table, and add your own.",
    show() {
      if (!inited) {
        inited = true;
        $("#d-letters").innerHTML = "ABCDEFGHIJKLMNOPQRSTUVWXYZ".split("").map((l) => `<button data-l="${l.toLowerCase()}" aria-pressed="false">${l}</button>`).join("");
        $("#d-letters").addEventListener("click", (e) => {
          const b = e.target.closest("button"); if (!b) return;
          q.letter = q.letter === b.dataset.l ? "" : b.dataset.l;
          $("#d-q").value = "";
          document.querySelectorAll("#d-letters button").forEach((x) => x.setAttribute("aria-pressed", String(x.dataset.l === q.letter)));
          reload();
        });
        const typed = SC.debounce(() => {
          if ($("#d-q").value) { q.letter = ""; document.querySelectorAll("#d-letters button").forEach((x) => x.setAttribute("aria-pressed", "false")); }
          reload();
        }, 200);
        $("#d-q").addEventListener("input", typed);
        ["#d-mode", "#d-sort", "#d-size", "#d-custom"].forEach((s) => $(s).addEventListener("change", reload));
        ["#d-min", "#d-max"].forEach((s) => $(s).addEventListener("input", SC.debounce(reload, 250)));
        $("#d-pager").addEventListener("click", (e) => {
          const b = e.target.closest("button[data-p]"); if (!b || b.disabled) return;
          q.page = Number(b.dataset.p); load();
          $("#d-table").scrollIntoView({ block: "nearest" });
        });
        $("#add-form").addEventListener("submit", async (e) => {
          e.preventDefault();
          const word = $("#add-word").value;
          if (!word.trim()) return;
          try {
            const r = await SC.api.post("/api/dictionary/add", { word });
            SC.toast(r.message + " It lives in hash bucket #" + r.bucket + ".", "ok");
            $("#add-word").value = "";
            $("#d-custom").checked = true;
            reload(); glance();
          } catch (err) { SC.toast(err.message, "err"); }
        });
      }
      load(); glance();
    },
  };
})();
