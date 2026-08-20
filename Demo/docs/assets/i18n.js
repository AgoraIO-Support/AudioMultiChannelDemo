/**
 * 共享中英文切换逻辑（与 Demo/web 的 Multiopus.vue 同一体验）：
 * - 语言状态持久化在 localStorage 的 "locale" 键（"zh" / "en"），与 Web demo 共用，
 *   在 demo 页和文档页之间切换时语言互相跟随。
 * - 默认按 navigator.language 判断（zh 开头为中文，否则英文）。
 * - 正文用 <div data-lang="zh"> / <div data-lang="en"> 成对包裹，按 locale 显示/隐藏。
 * - 切换时同步 document.title（取 <title> 上的 data-title-zh / data-title-en）
 *   与 <html lang>；按钮文案随语言变化（中文界面显示 "EN"，英文界面显示 "中文"）。
 */
(function () {
  "use strict";

  var STORAGE_KEY = "locale";

  function getLocale() {
    var saved = null;
    try {
      saved = localStorage.getItem(STORAGE_KEY);
    } catch (e) {
      /* localStorage 不可用时忽略 */
    }
    if (saved === "zh" || saved === "en") return saved;
    var lang = (navigator.language || "").toLowerCase();
    return lang.indexOf("zh") === 0 ? "zh" : "en";
  }

  function apply(locale) {
    var blocks = document.querySelectorAll("[data-lang]");
    for (var i = 0; i < blocks.length; i++) {
      blocks[i].style.display =
        blocks[i].getAttribute("data-lang") === locale ? "" : "none";
    }
    document.documentElement.lang = locale === "zh" ? "zh-CN" : "en";
    var titleEl = document.querySelector("title");
    if (titleEl) {
      var localized = titleEl.getAttribute("data-title-" + locale);
      if (localized) document.title = localized;
    }
    var btn = document.getElementById("lang-toggle");
    if (btn) btn.textContent = locale === "zh" ? "EN" : "中文";
  }

  function toggle() {
    var next = getLocale() === "zh" ? "en" : "zh";
    try {
      localStorage.setItem(STORAGE_KEY, next);
    } catch (e) {
      /* ignore */
    }
    apply(next);
  }

  function init() {
    var btn = document.getElementById("lang-toggle");
    if (btn) btn.addEventListener("click", toggle);
    apply(getLocale());
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
