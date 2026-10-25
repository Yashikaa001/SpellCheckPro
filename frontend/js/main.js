/* ==========================================================================
   main.js - starts the app: icons, page routing and the connection indicator.

   Routing: the address bar holds the page, e.g.  #/checker  or  #/hash?word=cat.
   When it changes we hide every page, show the right one and call its show().
   ========================================================================== */
(function () {
  "use strict";
  const { $, $$ } = SC;

  function route() {
    const raw = location.hash.replace(/^#\/?/, "") || "dashboard";
    const [name, qs] = raw.split("?");
    const page = SC.pages[name] ? name : "dashboard";
    const query = Object.fromEntries(new URLSearchParams(qs || ""));

    $$(".page").forEach((p) => { p.hidden = p.id !== "page-" + page; });
    $$("#nav a").forEach((a) => {
      if (a.dataset.route === page) a.setAttribute("aria-current", "page");
      else a.removeAttribute("aria-current");
    });
    const def = SC.pages[page];
    $("#page-title").textContent = def.title;
    $("#page-sub").textContent = def.subtitle;
    document.title = def.title + " · SpellCheck Pro";
    window.scrollTo(0, 0);
    def.show(query);
  }

  // The little status light in the sidebar.
  async function checkConnection() {
    const box = $("#conn"), text = $("#conn-text");
    try {
      const h = await SC.api.get("/api/health");
      box.className = "conn ok";
      text.textContent = "C++ server · " + SC.fmt.int(h.words) + " words";
    } catch (e) {
      box.className = "conn bad";
      text.textContent = "Server offline";
    }
  }

  document.addEventListener("DOMContentLoaded", () => {
    SC.hydrateIcons(document);
    window.addEventListener("hashchange", route);
    route();
    checkConnection();
    setInterval(checkConnection, 8000);
  });
})();
