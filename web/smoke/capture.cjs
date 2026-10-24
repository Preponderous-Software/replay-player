// Screenshots of the browser player for the README (docs/media/browser-*.png).
//
//   node web/smoke/capture.cjs BASE_URL DATA_DIR TICK OUT.png [--phone] [--dark]
"use strict";
const { chromium } = require("playwright");

const [base, data, tick, out, ...flags] = process.argv.slice(2);
const phone = flags.includes("--phone");
(async () => {
  const browser = await chromium.launch({ args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"] });
  const page = await browser.newPage(phone
    ? { viewport: { width: 390, height: 844 }, isMobile: true, hasTouch: true, deviceScaleFactor: 2,
        colorScheme: flags.includes("--dark") ? "dark" : "light" }
    : { viewport: { width: 1280, height: 720 }, colorScheme: flags.includes("--dark") ? "dark" : "light" });
  await page.goto(`${base}?autoplay=0&at=${tick}&data=${encodeURIComponent(data)}`);
  await page.waitForFunction("window.__usdWebReplay && window.__usdWebReplay.ready", null, { timeout: 60000 });
  await page.waitForFunction("window.__usdWebReplay.loaded === window.__usdWebReplay.chunks", null, { timeout: 60000 });
  await page.evaluate("window.__usdWebReplay.render()");
  await page.waitForTimeout(500);
  await page.screenshot({ path: out });
  console.log(`${out}: tick ${await page.evaluate("window.__usdWebReplay.tick")}`);
  await browser.close();
})().catch(err => { console.error(err); process.exit(1); });
