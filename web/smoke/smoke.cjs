// Browser smoke test of the replay page in headless Chromium (SwiftShader WebGL 2): the steps and
// assertions of usd-web's tests/browser/test_replay_smoke.py, so the same checks hold for both
// players. It plays, pauses, scrubs, steps, changes speed, reads pixels back, then lays the
// controls out for a phone.
//
//   node web/smoke/smoke.cjs http://127.0.0.1:8080/ DATA_DIR OUT_DIR
//
// DATA_DIR is the bundle's path relative to the page (?data=) and must hold at least 12 samples
// in more than one chunk; the expectations are read from its replay.json. Screenshots go to
// OUT_DIR. Needs the `playwright` package (web/smoke/package.json).
"use strict";
const { chromium } = require("playwright");
const fs = require("fs");
const path = require("path");

const [base, data, out] = process.argv.slice(2);
if (!base || !data || !out) {
  console.error("usage: node smoke.cjs BASE_URL DATA_DIR OUT_DIR");
  process.exit(2);
}
const ARGS = ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"];
const HOOK = "window.__usdWebReplay";

function check(cond, what) {
  if (!cond) throw new Error("smoke: " + what);
  console.log("ok  " + what);
}

(async () => {
  fs.mkdirSync(out, { recursive: true });
  const doc = await (await fetch(new URL(data.replace(/\/?$/, "/") + "replay.json", base))).json();
  const browser = await chromium.launch({ executablePath: process.env.SMOKE_CHROMIUM || undefined, args: ARGS });
  try {
    const page = await browser.newPage({ viewport: { width: 900, height: 600 } });
    const errors = [];
    page.on("console", m => { if (m.type() === "error") errors.push(m.text()); });
    page.on("pageerror", e => errors.push(String(e)));
    const t0 = Date.now();
    await page.goto(`${base}?speed=4&data=${encodeURIComponent(data)}`);
    await page.waitForFunction(`${HOOK} && ${HOOK}.ready`, null, { timeout: 60000 });
    const readyMs = Date.now() - t0;
    check(await page.evaluate(`${HOOK}.samples`) === doc.samples, `the hook reports ${doc.samples} samples`);
    check(await page.evaluate(`${HOOK}.chunks`) > 1, "the bundle has more than one chunk");
    await page.waitForFunction(`${HOOK}.loaded === ${HOOK}.chunks`, null, { timeout: 30000 });
    check(true, "every chunk loads");
    await page.waitForFunction(`${HOOK}.playing && ${HOOK}.tick > ${doc.ticks[1]}`, null, { timeout: 30000 });
    check(true, "it plays");
    check(await page.locator("#title").textContent() === doc.title, "the title is the replay's");
    check((await page.locator("#sub").textContent()).includes(`seed ${doc.meta.seed}`), "the run's seed is shown");

    // Pause, then scrub: the tick and the state hash follow the scrubber.
    await page.locator(".uwr-play").click();
    check(await page.evaluate(`${HOOK}.playing`) === false, "the play button pauses");
    check(await page.locator(".uwr-play").textContent() === "Play", "the button then says Play");
    await page.locator(".uwr-range").fill("10");
    await page.waitForFunction(`${HOOK}.tick === ${doc.ticks[10]}`);
    check(await page.evaluate(`${HOOK}.hash`) === doc.hashes[10], "scrubbing shows that sample's state hash");
    check((await page.locator("#hash").textContent()).includes(doc.hashes[10]), "the panel shows the hash");
    check((await page.locator(".uwr-tick").textContent()).startsWith(`tick ${doc.ticks[10].toLocaleString()}`), "the tick readout follows");
    // The arrow keys step one sample.
    await page.locator("#view").focus();
    await page.keyboard.press("ArrowRight");
    await page.waitForFunction(`${HOOK}.tick === ${doc.ticks[11]}`);
    check(true, "ArrowRight steps one sample");

    // Speed cycles through the steps; space resumes.
    const speed = page.locator(".uwr-speed");
    check(await speed.textContent() === "4×", "the speed starts at ?speed=4");
    await speed.click();
    check(await speed.textContent() === "8×", "the speed button goes to 8x");
    await page.locator("#view").focus();
    await page.keyboard.press(" ");
    await page.waitForFunction(`${HOOK}.playing`);
    check(true, "space resumes");

    // Render and read back in one task: the drawing buffer is not preserved.
    const distinct = await page.evaluate(() => {
      window.__usdWebReplay.render();
      const c = document.getElementById("view");
      const g = c.getContext("webgl2") || c.getContext("webgl");
      // The central half of the canvas (usd-web's test reads the central 64 x 64; a small
      // fixture can leave that empty at the tick the test stops on).
      const w = Math.floor(c.width / 2), h = Math.floor(c.height / 2);
      const px = new Uint8Array(4 * w * h);
      g.readPixels(Math.floor(c.width / 4), Math.floor(c.height / 4), w, h, g.RGBA, g.UNSIGNED_BYTE, px);
      const s = new Set();
      for (let i = 0; i < px.length; i += 4) s.add(px[i] + "," + px[i + 1] + "," + px[i + 2]);
      return s.size;
    });
    check(distinct > 3, `the canvas draws the replay (${distinct} colours in the central half)`);
    await page.locator(".uwr-play").click();
    await page.screenshot({ path: path.join(out, "desktop.png") });
    check((await page.evaluate(`${HOOK}.errors`)).length === 0, "the hook reports no errors");
    check(errors.length === 0, "no console errors" + (errors.length ? ": " + errors.join(" | ") : ""));

    // Frame rate while playing (SwiftShader: a floor, not a measurement of a GPU).
    await page.evaluate(`${HOOK}.play()`);
    const f0 = await page.evaluate(`${HOOK}.frames`);
    await page.waitForTimeout(2000);
    const fps = ((await page.evaluate(`${HOOK}.frames`)) - f0) / 2;
    console.log(`info first frame after ${readyMs} ms; ${fps.toFixed(1)} frames/s while playing (SwiftShader)`);
    await page.close();  // a page left playing would share SwiftShader's CPU with the next one

    // Phone: the scrubber gets its own row and every button is at least 44 px tall.
    const phone = await browser.newPage({ viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true });
    await phone.goto(`${base}?autoplay=0&data=${encodeURIComponent(data)}`);
    await phone.waitForFunction(`${HOOK} && ${HOOK}.ready`, null, { timeout: 120000 });
    check(await phone.evaluate(`${HOOK}.playing`) === false, "?autoplay=0 starts paused");
    const rng = await phone.locator(".uwr-range").boundingBox(), btn = await phone.locator(".uwr-play").boundingBox();
    check(rng.width > 300 && rng.y < btn.y, "on a phone the scrubber has its own row");
    check(btn.height >= 44 && (await phone.locator(".uwr-speed").boundingBox()).height >= 44, "buttons are 44 px tall");
    await phone.locator(".uwr-play").tap();
    await phone.waitForFunction(`${HOOK}.playing && ${HOOK}.tick > 0`, null, { timeout: 30000 });
    check(true, "a tap plays");
    await phone.screenshot({ path: path.join(out, "phone.png") });
    console.log("smoke: all checks passed");
  } finally {
    await browser.close();
  }
})().catch(err => {
  console.error(err.message || err);
  process.exit(1);
});
