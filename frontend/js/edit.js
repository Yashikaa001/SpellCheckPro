/* ==========================================================================
   edit.js - the Edit distance page.
   Sends two words to the C++ backend, which fills in the dynamic-programming
   table and walks back through it to find the cheapest list of edits.
   ========================================================================== */
(function () {
  "use strict";
  const { $, fmt, esc } = SC;

  const PRESETS = [["speling", "spelling"], ["teh", "the"], ["recieve", "receive"], ["kitten", "sitting"], ["definately", "definitely"]];
  let inited = false;

  const OPS = {
    M: { name: "Keep", text: (o) => `“${o.from}”`, note: "same letter" },
    S: { name: "Replace", text: (o) => `${o.from} → ${o.to}`, note: "wrong letter" },
    I: { name: "Insert", text: (o) => `+ ${o.to}`, note: "missing letter" },
    D: { name: "Delete", text: (o) => `− ${o.from}`, note: "extra letter" },
    T: { name: "Swap", text: (o) => `${o.from}${o.to} → ${o.to}${o.from}`, note: "neighbours swapped" },
  };

  async function compute() {
    const a = $("#ed-a").value.trim(), b = $("#ed-b").value.trim();
    const r = await SC.safe(() => SC.api.get("/api/edit", { a, b }));
    if (!r) return;

    const longest = Math.max(r.a.length, r.b.length, 1);
    const sim = Math.round((1 - r.distance / longest) * 100);
    const ops = r.ops;
    $("#ed-result").innerHTML =
      `<div class="row" style="gap:22px;align-items:flex-end;margin-bottom:16px"><div><div class="bigdist">${r.distance}</div><div class="hint">edit${r.distance === 1 ? "" : "s"} needed</div></div>` +
      `<div><div style="font-size:22px;font-weight:600">${sim}%</div><div class="hint">similar</div></div></div>` +
      (ops.length
        ? `<div class="ops">${ops.map((o) => `<div class="op ${o.type}"><b>${OPS[o.type].name}</b><span class="mono">${esc(OPS[o.type].text(o))}</span><small>${OPS[o.type].note}</small></div>`).join("")}</div>`
        : '<p class="note">Both fields are empty.</p>') +
      '<p class="note" style="margin-top:16px">Swapping two neighbouring letters counts as <b>one</b> edit, because it is the most common typing slip (teh → the).</p>';

    // The table: rows = letters of A, columns = letters of B, highlighted cells = cheapest path.
    const onPath = new Set(r.path.map((p) => p[0] + "," + p[1]));
    const head = '<tr><th></th><th></th>' + [...r.b].map((c) => `<th>${esc(c)}</th>`).join("") + "</tr>";
    const rows = r.matrix.map((row, i) =>
      "<tr>" + `<th>${i === 0 ? "" : esc(r.a[i - 1])}</th>` +
      row.map((v, j) => {
        const cls = (onPath.has(i + "," + j) ? "path" : "") + (i === r.a.length && j === r.b.length ? " goal" : "");
        return `<td class="${cls}">${v}</td>`;
      }).join("") + "</tr>").join("");
    $("#ed-matrix").innerHTML = `<table class="matrix" aria-label="Edit distance table">${head}${rows}</table>` +
      '<p class="hint" style="margin-top:10px;text-align:center">Purple cells are the cheapest path from the top-left to the bottom-right.</p>';
  }

  SC.pages.edit = {
    title: "Edit distance",
    subtitle: "How the spell checker measures how close two words are. Fewer edits means a better suggestion.",
    show(query) {
      if (!inited) {
        inited = true;
        $("#ed-presets").innerHTML = '<span class="lbl">Try:</span>' +
          PRESETS.map((p, i) => `<button class="chip" data-i="${i}">${p[0]} → ${p[1]}</button>`).join("");
        $("#ed-presets").addEventListener("click", (e) => {
          const b = e.target.closest("button[data-i]"); if (!b) return;
          const p = PRESETS[Number(b.dataset.i)];
          $("#ed-a").value = p[0]; $("#ed-b").value = p[1]; compute();
        });
        const run = SC.debounce(compute, 200);
        $("#ed-a").addEventListener("input", run);
        $("#ed-b").addEventListener("input", run);
      }
      if (query && query.a) { $("#ed-a").value = query.a; if (query.b) $("#ed-b").value = query.b; }
      compute();
    },
  };
})();
