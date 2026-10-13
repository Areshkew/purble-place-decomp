// Drives the page in a headless Chromium browser over the DevTools protocol and captures the
// canvas, so the browser build can be checked without a person watching it.
//
//   node smoke-browser.mjs --edge <msedge.exe> --url <page url> --out <png> [--mode live|shot]
//                          [--width 1400] [--height 900] [--settle 4000] [--timeout 180000]
//
// The page reports what it is doing through window.ppleState (see web/shell.html), so this waits
// for a state instead of guessing at timings: "shot-ready" for the --shot capture hook, and
// "running" plus a settle delay for the live screen.  It prints the page's own console text,
// which is the game's stdout, and exits non-zero if the state never arrives.
import { spawn } from 'node:child_process';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

function arg(name, fallback) {
  const at = process.argv.indexOf('--' + name);
  return at >= 0 && at + 1 < process.argv.length ? process.argv[at + 1] : fallback;
}

const edge = arg('edge', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe');
const url = arg('url');
const out = arg('out');
const mode = arg('mode', 'live');
const width = Number(arg('width', '1400'));
const height = Number(arg('height', '900'));
const settle = Number(arg('settle', '4000'));
const timeout = Number(arg('timeout', '180000'));
const profileOut = arg('profile');
const measure = Number(arg('measure', '5000'));
const checkPerf = process.argv.includes('--check-perf');
// --click x,y (viewport CSS pixels) may be repeated; they are sent as real mouse events, so the
// game's own hit testing decides what they mean.
const clicks = [];
const checkUi = process.argv.includes('--check-ui');
for (let i = 0; i < process.argv.length; i++) {
  if (['--click', '--scene-click', '--ui-click', '--key'].includes(process.argv[i]) && i + 1 < process.argv.length) {
    const kind = process.argv[i];
    const value = process.argv[++i];
    clicks.push({ kind, value });
  }
}
if (!url || !out) {
  console.error('usage: node smoke-browser.mjs --url <url> --out <png> [--mode live|shot]');
  process.exit(2);
}

const port = 9333 + (process.pid % 200);
const profile = mkdtempSync(join(tmpdir(), 'pple-edge-'));
const browser = spawn(edge, [
  '--headless=new', '--disable-gpu', '--no-sandbox', '--no-first-run',
  '--no-default-browser-check', '--hide-scrollbars', '--force-device-scale-factor=1',
  '--disable-background-timer-throttling', '--disable-renderer-backgrounding',
  '--disable-backgrounding-occluded-windows',
  `--user-data-dir=${profile}`, `--remote-debugging-port=${port}`,
  `--window-size=${width},${height + 60}`, url
], { stdio: 'ignore' });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function target() {
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      const page = list.find((t) => t.type === 'page' && t.webSocketDebuggerUrl);
      if (page) return page.webSocketDebuggerUrl;
    } catch { /* the browser is still starting */ }
    await sleep(300);
  }
  throw new Error('no DevTools page target appeared');
}

function connect(wsUrl) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(wsUrl);
    let next = 0;
    const pending = new Map();
    const exceptions = [];
    ws.onmessage = (event) => {
      const message = JSON.parse(event.data);
      if (message.method === 'Runtime.exceptionThrown') exceptions.push(message.params.exceptionDetails);
      const settle = pending.get(message.id);
      if (settle) { pending.delete(message.id); settle(message); }
    };
    ws.onerror = (event) => reject(new Error('websocket error: ' + (event.message || '')));
    ws.onopen = () => resolve({
      exceptions,
      send(method, params) {
        const id = ++next;
        ws.send(JSON.stringify({ id, method, params: params || {} }));
        return new Promise((done) => pending.set(id, done));
      },
      close() { ws.close(); }
    });
  });
}

let failure = '';
try {
  const cdp = await connect(await target());
  await cdp.send('Runtime.enable');
  await cdp.send('Page.enable');
  await cdp.send('Page.bringToFront');

  const evaluate = async (expression) => {
    const reply = await cdp.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: false });
    return reply.result && reply.result.result ? reply.result.result.value : undefined;
  };

  const foreground = async () => {
    await cdp.send('Page.bringToFront');
    if (await evaluate('document.hidden')) {
      const browserWindow = await cdp.send('Browser.getWindowForTarget');
      await cdp.send('Browser.setWindowBounds', { windowId: browserWindow.result.windowId,
        bounds: { windowState: 'normal' } });
      await cdp.send('Page.bringToFront');
    }
  };

  const wanted = arg('expect-state', mode === 'shot' ? 'shot-ready' : 'running');
  const deadline = Date.now() + timeout;
  let state = '';
  while (Date.now() < deadline) {
    state = await evaluate('window.ppleState');
    if (state === wanted || state === 'shot-failed' || state === 'ended' || state === 'error') break;
    await sleep(500);
  }
  if (state !== wanted) failure = `state is "${state}", expected "${wanted}"`;
  if (state === wanted && mode === 'live') await sleep(settle);

  await evaluate(`window.ppleTestFrames = 0; window.ppleTestKeys = [];
    document.addEventListener('keydown', e => window.ppleTestKeys.push(e.key));
    (function count() { window.ppleTestFrames++; requestAnimationFrame(count); })();`);
  for (const { kind, value } of clicks) {
    if (kind === '--key') {
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', key: value, code: value,
        windowsVirtualKeyCode: value === 'Escape' ? 27 : value.toUpperCase().charCodeAt(0) });
      await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: value, code: value,
        windowsVirtualKeyCode: value === 'Escape' ? 27 : value.toUpperCase().charCodeAt(0) });
      await sleep(1200);
      continue;
    }
    const helpBefore = kind === '--ui-click' && value === 'help-toggle'
      ? await evaluate("document.getElementById('help').hidden") : null;
    const fullscreenBefore = kind === '--ui-click' && value === 'fullscreen'
      ? await evaluate('!!document.fullscreenElement') : null;
    let [x, y] = value.split(',').map(Number);
    if (kind === '--scene-click') {
      [x, y] = await evaluate(`(() => {
        const c = document.getElementById('canvas'), r = c.getBoundingClientRect();
        const matches = [...document.getElementById('log').textContent.matchAll(/HUD strip (\\d+) px/g)];
        const strip = Number(matches.at(-1)?.[1] || 0);
        const s = Math.min(c.width / 800, (c.height - strip) / 496);
        return [r.x + ((c.width - 800*s)/2 + ${x}*s)*r.width/c.width,
          r.y + (strip + (c.height-strip-496*s)/2 + ${y}*s)*r.height/c.height];
      })()`);
    }
    if (kind === '--ui-click') {
      // DOM controls are activated with an explicit CDP user gesture. Coordinate
      // clicks are unreliable across headless full-screen/focus transitions;
      // canvas/game clicks below still use real Input.dispatchMouseEvent events.
      console.log(`control : ${value}`);
      await cdp.send('Runtime.evaluate', {
        expression: `document.getElementById(${JSON.stringify(value)}).click()`, userGesture: true
      });
    } else {
      console.log(`click   : ${kind} ${value} -> ${x},${y}`);
      await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y, buttons: 0 });
      await sleep(120);
      await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1, buttons: 1 });
      await sleep(60);
      await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1, buttons: 0 });
    }
    await sleep(1200);
    if (helpBefore !== null && helpBefore === await evaluate("document.getElementById('help').hidden"))
      throw new Error('How to play did not toggle');
    if (fullscreenBefore !== null) {
      let fullscreenAfter, status;
      const fullscreenDeadline = Date.now() + 10000;
      do {
        fullscreenAfter = await evaluate('!!document.fullscreenElement');
        status = await evaluate("document.getElementById('status').textContent");
        if (fullscreenBefore !== fullscreenAfter || status.includes('Full screen is not available')) break;
        await sleep(250);
      } while (Date.now() < fullscreenDeadline);
      if (fullscreenBefore === fullscreenAfter) {
        if (!status.includes('Full screen is not available')) {
          const detail = await evaluate(`({ frames: ppleTestFrames, visibility: document.visibilityState,
            focus: document.hasFocus(), enabled: document.fullscreenEnabled })`);
          throw new Error('Full screen did not settle: ' + JSON.stringify(detail));
        }
        console.log('full screen: browser declined; visible fallback verified');
      } else console.log('full screen: ' + fullscreenAfter);
      // Headless Edge can hide its page when leaving full screen. The real user
      // returns to a visible tab; restore that condition before further input.
      await foreground();
      await sleep(500);
    }
  }
  if (clicks.length) await sleep(settle);
  state = await evaluate('window.ppleState');
  if (cdp.exceptions.length) throw new Error('browser exceptions: ' + JSON.stringify(cdp.exceptions));
  if (arg('expect-final-state') && state !== arg('expect-final-state')) {
    const details = await evaluate(`({ visibility: document.visibilityState, focus: document.hasFocus(),
      active: document.activeElement?.id, frames: window.ppleTestFrames, keys: window.ppleTestKeys,
      loop: typeof MainLoop === 'undefined' ? null : { timingMode: MainLoop.timingMode, running: MainLoop.running, currentlyRunningMainloop: MainLoop.currentlyRunningMainloop },
      log: logLines.slice(-20) })`);
    throw new Error(`final state is ${state}, expected ${arg('expect-final-state')}: ${JSON.stringify(details)}`);
  }

  const checkLayout = async () => {
    const assertLayout = async () => {
      const layout = await evaluate(`(() => {
        const r = document.getElementById('canvas').getBoundingClientRect();
        return { open: document.getElementById('diagnostics').open,
          debug: new URLSearchParams(location.search).get('debug') === '1',
          left: r.left, top: r.top, right: r.right, bottom: r.bottom,
          width: innerWidth, height: innerHeight,
          backing: [document.getElementById('canvas').width, document.getElementById('canvas').height],
          transform: document.getElementById('canvas').style.transform,
          boxWidth: document.getElementById('framebox').clientWidth,
          log: document.getElementById('log').textContent };
      })()`);
      if (layout.backing[0] !== 1120 || layout.backing[1] !== 694)
        throw new Error('fixed canvas backing size changed: ' + JSON.stringify(layout.backing));
      if (layout.open !== layout.debug) throw new Error('diagnostics visibility is incorrect');
      if (!layout.debug && !layout.log.includes('HUD strip 0 px')) throw new Error('normal play still has a debug HUD');
      if (layout.left < -1 || layout.top < -1 || layout.right > layout.width + 1 || layout.bottom > layout.height + 1)
        throw new Error('canvas does not fit the viewport: ' + JSON.stringify(layout));
    };
    await assertLayout();
    await cdp.send('Emulation.setDeviceMetricsOverride', { width: 640, height: 480, deviceScaleFactor: 1, mobile: false });
    await foreground();
    await sleep(2000);
    await assertLayout();
    await cdp.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false });
    await foreground();
    await sleep(2000);
    await assertLayout();
    console.log('UI checks passed: diagnostic visibility and canvas fit at both viewport sizes');
  };
  if (profileOut && state === wanted && mode === 'live') {
    await evaluate(`window.ppleFrames = []; window.ppleMeasure = true;
      (function sample(last) { requestAnimationFrame(function(now) {
        if (last) window.ppleFrames.push(now - last);
        if (window.ppleMeasure) sample(now);
      }); })(0);`);
    await cdp.send('Profiler.enable');
    await cdp.send('Profiler.start');
    const measuredAt = Date.now();
    await sleep(measure);
    const profile = await cdp.send('Profiler.stop');
    await evaluate('window.ppleMeasure = false');
    const frames = await evaluate('window.ppleFrames');
    frames.sort((a, b) => a - b);
    const cpu = profile.result.profile;
    const nodes = new Map(cpu.nodes.map(n => [n.id, n]));
    const parents = new Map();
    cpu.nodes.forEach(n => n.children?.forEach(c => parents.set(c, n.id)));
    let loopUs = 0, clockUs = 0;
    cpu.samples.forEach((id, i) => {
      let inLoop = false, inClock = false;
      while (id) {
        const name = nodes.get(id).callFrame.functionName;
        inLoop ||= name === 'MainLoop_runner';
        inClock ||= name === '_emscripten_get_now';
        id = parents.get(id);
      }
      if (inLoop) {
        loopUs += cpu.timeDeltas[i];
        if (inClock) clockUs += cpu.timeDeltas[i];
      }
    });
    const stats = { count: frames.length, fps: frames.length * 1000 / (Date.now() - measuredAt),
      median: frames[Math.floor(frames.length / 2)], p95: frames[Math.floor(frames.length * .95)],
      max: frames.at(-1), loopMs: loopUs / 1000, clockMs: clockUs / 1000 };
    writeFileSync(profileOut, JSON.stringify({ frames: stats, profile: cpu }));
    console.log('frame performance: ' + JSON.stringify(stats));
    if (checkPerf && (frames.length < 30 || clockUs > loopUs * .2))
      throw new Error('frame pacing regression: too few frames or a clock-heavy main loop');
  }
  if (checkUi && mode === 'live') await checkLayout();
  const log = await evaluate("document.getElementById('log').textContent") || '';
  for (let i = 0; i < process.argv.length; i++) {
    if (process.argv[i] === '--expect-log' && !log.includes(process.argv[++i]))
      throw new Error('expected page log text missing: ' + process.argv[i]);
  }
  const title = await evaluate('document.title');
  const shot = await cdp.send('Page.captureScreenshot', {
    format: 'png',
    clip: { x: 0, y: 0, width, height, scale: 1 },
    captureBeyondViewport: true
  });
  if (!shot.result || !shot.result.data) throw new Error('captureScreenshot returned nothing');
  writeFileSync(out, Buffer.from(shot.result.data, 'base64'));

  console.log(`state   : ${state}`);
  console.log(`title   : ${title}`);
  console.log(`captured: ${out} (${width}x${height})`);
  console.log('--- page console ---');
  console.log(log.trim());
  cdp.close();
} catch (err) {
  failure = err.message;
} finally {
  browser.kill();
}

if (failure) {
  console.error(`FAILED: ${failure}`);
  process.exit(1);
}
console.log('\nbrowser check passed');
