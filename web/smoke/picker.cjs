// The replay picker in headless Chromium: without ?data= the page opens the first replay that
// replays/index.json lists, the picker lists them all, and each one picked loads and draws.
// Runs on a desktop and on a phone (iPhone 13) profile; screenshots go to OUT_DIR.
//
//   node web/smoke/picker.cjs http://127.0.0.1:8080/ OUT_DIR
"use strict";
const { chromium, devices } = require("playwright");
const fs = require("fs");
const path = require("path");

const [base, out] = process.argv.slice(2);
if (!base || !out) {
  console.error("usage: node picker.cjs BASE_URL OUT_DIR");
  process.exit(2);
}
const ARGS = ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"];
const HOOK = "window.__usdWebReplay";

function check(cond, what) {
  if (!cond) throw new Error("picker: " + what);
  console.log("ok  " + what);
}

async function run(browser, name, options) {
  const { replays } = await (await fetch(new URL("replays/index.json", base))).json();
  const titles = [];
  for (const r of replays) titles.push((await (await fetch(new URL(r.data + "replay.json", base))).json()).title);
  const context = await browser.newContext(options);
  const page = await context.newPage();
  const errors = [];
  page.on("console", m => { if (m.type() === "error") errors.push(m.text()); });
  page.on("pageerror", e => errors.push(String(e)));
  await page.goto(base);
  await page.waitForFunction(`${HOOK} && ${HOOK}.ready`, null, { timeout: 120000 });
  check(await page.evaluate(`${HOOK}.replays`) === replays.length, `${name}: the hook reports ${replays.length} replays`);
  check(await page.locator("#pick option").count() === replays.length, `${name}: the picker lists them`);
  check(await page.locator("#title").textContent() === titles[0], `${name}: without ?data= the first replay opens`);
  if (options.isMobile) {
    const box = await page.locator("#pick").boundingBox();
    check(box.height >= 44, `${name}: the picker is at least 44 px tall`);
  }
  for (let i = 0; i < replays.length; i++) {
    if (i > 0) {
      await Promise.all([page.waitForNavigation(), page.locator("#pick").selectOption(replays[i].data)]);
      await page.waitForFunction(`${HOOK} && ${HOOK}.ready`, null, { timeout: 120000 });
    }
    check(await page.locator("#title").textContent() === titles[i], `${name}: picking ${replays[i].id} opens "${titles[i]}"`);
    check(await page.locator("#pick").inputValue() === replays[i].data, `${name}: the picker shows ${replays[i].id}`);
    await page.waitForFunction(`${HOOK}.loaded === ${HOOK}.chunks`, null, { timeout: 120000 });
    await page.evaluate(`${HOOK}.pause()`);
    await page.evaluate(`${HOOK}.seek(${HOOK}.tick)`);
    await page.screenshot({ path: path.join(out, `${name}-${replays[i].id}.png`), timeout: 120000 });
  }
  check((await page.evaluate(`${HOOK}.errors`)).length === 0, `${name}: the hook reports no errors`);
  check(errors.length === 0, `${name}: no console errors` + (errors.length ? ": " + errors.join(" | ") : ""));
  await context.close();
}

(async () => {
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch({ executablePath: process.env.SMOKE_CHROMIUM || undefined, args: ARGS });
  try {
    await run(browser, "desktop", { viewport: { width: 1280, height: 720 } });
    await run(browser, "phone", { ...devices["iPhone 13"] });
    console.log("picker: all checks passed");
  } finally {
    await browser.close();
  }
})().catch(err => {
  console.error(err);
  process.exit(1);
});
