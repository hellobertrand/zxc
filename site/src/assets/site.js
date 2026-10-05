// libzxc.org: small progressive enhancements. Every page works without this file.
(function () {
  "use strict";

  // Copy buttons on code blocks.
  if (navigator.clipboard) {
    document.querySelectorAll("pre.code").forEach(function (pre) {
      var btn = document.createElement("button");
      btn.type = "button";
      btn.className = "copy";
      btn.textContent = "copy";
      btn.addEventListener("click", function () {
        var code = pre.querySelector("code");
        navigator.clipboard.writeText((code || pre).innerText.replace(/\n$/, "")).then(function () {
          btn.textContent = "copied";
          setTimeout(function () { btn.textContent = "copy"; }, 1500);
        });
      });
      pre.appendChild(btn);
    });
  }

  // GitHub star count in the footer: refresh the build-time figure,
  // at most once an hour per browser.
  var stars = document.querySelectorAll("[data-gh-stars]");
  if (stars.length && window.fetch) {
    var KEY = "zxc-gh-stars", HOUR = 3600 * 1000;
    var show = function (n) {
      stars.forEach(function (el) {
        el.querySelector("[data-gh-stars-n]").textContent = n.toLocaleString("en-US");
        el.hidden = false;
      });
    };
    var cached = null;
    try { cached = JSON.parse(localStorage.getItem(KEY)); } catch (e) { cached = null; }
    if (cached && typeof cached.n === "number" && Date.now() - cached.t < HOUR) {
      show(cached.n);
    } else {
      fetch("https://api.github.com/repos/hellobertrand/zxc")
        .then(function (r) { return r.ok ? r.json() : null; })
        .then(function (d) {
          if (!d || typeof d.stargazers_count !== "number") return;
          show(d.stargazers_count);
          try { localStorage.setItem(KEY, JSON.stringify({ n: d.stargazers_count, t: Date.now() })); } catch (e) {}
        })
        .catch(function () {});
    }
  }

  // Figure 1: switch processor without reloading.
  var fig = document.querySelector("[data-bench-figure]");
  if (fig) {
    fig.querySelectorAll('input[name="cpu"]').forEach(function (input) {
      input.addEventListener("change", function () {
        var cpu = input.value;
        fig.querySelectorAll("[data-by-cpu]").forEach(function (el) {
          var v = JSON.parse(el.getAttribute("data-by-cpu"))[cpu];
          if (!v) return;
          if (v.w !== undefined) el.style.setProperty("--w", v.w);
          if (v.html !== undefined) el.innerHTML = v.html;
        });
        var cap = fig.querySelector("[data-cpu-detail]");
        if (cap) cap.textContent = JSON.parse(cap.getAttribute("data-cpu-detail"))[cpu] || "";
      });
    });
  }
})();
