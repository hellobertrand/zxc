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
