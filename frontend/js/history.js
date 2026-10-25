/* ==========================================================================
   history.js - the Correction history page.
   The server keeps the list (data/history.tsv), so it survives restarts.
   ========================================================================== */
(function () {
  "use strict";
  const { $, $$, fmt, esc } = SC;
  let filter = "", inited = false;

  async function load() {
    const r = await SC.safe(() => SC.api.get("/api/history", { type: filter, limit: 150 }));
    if (!r) return;
    const box = $("#hist-list");
    if (!r.items.length) {
      box.innerHTML = SC.empty(filter ? "Nothing here yet" : "No history yet",
        filter === "correction" ? "Click a suggestion in the Spell checker to log a correction." : "Run a spell check and it will appear here.");
      return;
    }
    box.innerHTML = r.items.map((h) => {
      if (h.type === "correction") {
        return `<div class="hitem"><span class="ico">${SC.icon("edit")}</span><div class="txt">` +
          `<p><span class="wd-bad">${esc(h.from)}</span><span class="arrowto">→</span><span class="wd-good">${esc(h.to)}</span></p>` +
          `<div class="meta"><span class="chip">${fmt.ago(h.time)}</span>` +
          (h.rank >= 0 ? `<span class="chip violet">${h.rank === 0 ? "first suggestion" : "suggestion #" + (h.rank + 1)}</span>` : "") + "</div></div>" +
          `<div class="row"><a class="btn sm" href="#/edit?a=${encodeURIComponent(h.from)}&b=${encodeURIComponent(h.to)}">Edit distance</a></div></div>`;
      }
      const cls = h.accuracy >= 95 ? "mint" : h.accuracy >= 80 ? "amber" : "rose";
      return `<div class="hitem"><span class="ico">${SC.icon("check")}</span><div class="txt"><p>${esc(h.text)}</p>` +
        `<div class="meta"><span class="chip">${fmt.ago(h.time)}</span><span class="chip">${h.words} words</span>` +
        `<span class="chip ${h.errors ? "rose" : "mint"}">${h.errors} misspelled</span><span class="chip ${cls}">${fmt.pct(h.accuracy, 0)} accurate</span></div></div>` +
        `<div class="row"><button class="btn sm" data-load="${h.id}">Open in checker</button></div></div>`;
    }).join("");
    box.__items = r.items;
  }

  SC.pages.history = {
    title: "Correction history",
    subtitle: "Every check you ran and every suggestion you applied. Saved on disk by the C++ server.",
    show() {
      if (!inited) {
        inited = true;
        $("#hist-filter").addEventListener("click", (e) => {
          const b = e.target.closest("button"); if (!b) return;
          filter = b.dataset.v;
          $$("#hist-filter button").forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
          load();
        });
        $("#hist-clear").addEventListener("click", async () => {
          if (!confirm("Delete the whole correction history?")) return;
          await SC.safe(() => SC.api.post("/api/history/clear"));
          SC.toast("History cleared", "ok");
          load();
        });
        $("#hist-list").addEventListener("click", (e) => {
          const b = e.target.closest("button[data-load]"); if (!b) return;
          const item = ($("#hist-list").__items || []).find((x) => String(x.id) === b.dataset.load);
          if (item) { SC.state.pendingText = item.text; location.hash = "#/checker"; }
        });
      }
      load();
    },
  };
})();
