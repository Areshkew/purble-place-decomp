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
for (let i = 0; i < process.argv.length; i++) {
  if (process.argv[i] === '--click' && i + 1 < process.argv.length) {
    const [x, y] = process.argv[i + 1].split(',').map(Number);
    clicks.push([x, y]);
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
    ws.onmessage = (event) => {
      const message = JSON.parse(event.data);
      const settle = pending.get(message.id);
      if (settle) { pending.delete(message.id); settle(message); }
    };
    ws.onerror = (event) => reject(new Error('websocket error: ' + (event.message || '')));
    ws.onopen = () => resolve({
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

  const evaluate = async (expression) => {
    const reply = await cdp.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: false });
    return reply.result && reply.result.result ? reply.result.result.value : undefined;
  };

  const wanted = mode === 'shot' ? 'shot-ready' : 'running';
  const deadline = Date.now() + timeout;
  let state = '';
  while (Date.now() < deadline) {
    state = await evaluate('window.ppleState');
    if (state === wanted || state === 'shot-failed' || state === 'ended' || state === 'error') break;
    await sleep(500);
  }
  if (state !== wanted) failure = `state is "${state}", expected "${wanted}"`;
  if (state === wanted && mode === 'live') await sleep(settle);

  for (const [x, y] of clicks) {
    console.log(`click   : ${x},${y}`);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', x, y, buttons: 0 });
    await sleep(120);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1, buttons: 1 });
    await sleep(60);
    await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1, buttons: 0 });
    await sleep(1200);
  }
  if (clicks.length) await sleep(settle);

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
  const log = await evaluate("document.getElementById('log').textContent") || '';
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
