// Language and theme, decided before paint and remembered between visits. Follows the CookingFoil site (site/site.js).
(() => {
  const LANGS = { ko: "한국어", en: "English" };
  const THEMES = ["light", "dark", "auto"];
  const root = document.documentElement;
  const read = (k) => { try { return localStorage.getItem(k); } catch { return null; } };
  const write = (k, v) => { try { localStorage.setItem(k, v); } catch { /* private mode */ } };

  const langListeners = [];
  const TITLES = { ko: "PotLuck · 친구와 함께, 로컬 멀티플레이", en: "PotLuck · Local multiplayer with your friends" };
  const setLang = (l) => {
    root.dataset.lang = l;
    root.lang = l;
    document.title = TITLES[l];
    const address = new URL(location.href);
    address.searchParams.set("lang", l);
    history.replaceState(null, "", address);
    for (const f of langListeners) f(l);
  };
  window.potluckOnLang = (f) => langListeners.push(f);
  const nav = (navigator.language || "").slice(0, 2).toLowerCase();
  const saved = read("lang");
  const requested = new URLSearchParams(location.search).get("lang");
  setLang(Object.hasOwn(LANGS, requested) ? requested : Object.hasOwn(LANGS, saved) ? saved : Object.hasOwn(LANGS, nav) ? nav : "en");
  if (Object.hasOwn(LANGS, requested)) write("lang", requested);

  const setTheme = (t) => { root.dataset.theme = t; };
  const savedTheme = read("theme");
  setTheme(THEMES.includes(savedTheme) ? savedTheme : "light");

  addEventListener("DOMContentLoaded", () => {
    const btn = document.getElementById("lang-btn");
    const menu = document.getElementById("lang-menu");
    const code = document.getElementById("lang-code");
    const close = () => { menu.hidden = true; btn.setAttribute("aria-expanded", "false"); };
    const sync = () => {
      code.textContent = root.dataset.lang.toUpperCase();
      for (const b of menu.querySelectorAll("button")) b.setAttribute("aria-checked", String(b.dataset.lang === root.dataset.lang));
    };
    for (const [l, name] of Object.entries(LANGS)) {
      const b = document.createElement("button");
      b.type = "button"; b.role = "menuitemradio"; b.dataset.lang = l; b.lang = l; b.textContent = name;
      b.addEventListener("click", () => { setLang(l); write("lang", l); sync(); close(); btn.focus(); });
      menu.append(b);
    }
    btn.addEventListener("click", () => {
      const open = menu.hidden;
      menu.hidden = !open; btn.setAttribute("aria-expanded", String(open));
      if (open) menu.querySelector('[aria-checked="true"]')?.focus();
    });
    addEventListener("click", (e) => { if (!e.target.closest(".lang")) close(); });
    addEventListener("keydown", (e) => { if (e.key === "Escape" && !menu.hidden) { close(); btn.focus(); } });
    sync();

    const theme = document.getElementById("theme-btn");
    const showTheme = () => {
      theme.title = { auto: "Theme: auto", dark: "Theme: dark", light: "Theme: light" }[root.dataset.theme];
    };
    theme.addEventListener("click", () => {
      const next = THEMES[(THEMES.indexOf(root.dataset.theme) + 1) % THEMES.length];
      setTheme(next); write("theme", next); showTheme();
    });
    showTheme();

  });
})();
