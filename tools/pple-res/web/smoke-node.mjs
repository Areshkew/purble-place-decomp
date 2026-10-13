// A headless check of the web build's data path.
//
// It loads the wasm build of the resource tools in node, writes the real game files into the
// Emscripten file system exactly as the page does, and runs the checks that need no SDL.
// Rendering is checked in a browser instead (see README.md), because SDL needs a canvas.
//
//   node smoke-node.mjs <build-dir> <game-dir>
//
// <game-dir> holds PurblePlace.dll, PurblePlace2.dll, CardGames.dll and en-US\PurblePlace.exe.mui.
// Each case runs in its own node process: an Emscripten runtime is a per-process thing, and
// re-requiring the same generated script would hand back the cached module instead of a fresh
// runtime.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';

const [buildDir, gameDir, one, ...oneArgs] = process.argv.slice(2);
if (!buildDir || !gameDir) {
  console.error('usage: node smoke-node.mjs <build-dir> <game-dir>');
  process.exit(2);
}

const GAME = [
  [join(gameDir, 'PurblePlace.dll'), '/assets/PurblePlace.dll'],
  [join(gameDir, 'PurblePlace2.dll'), '/assets/PurblePlace2.dll'],
  [join(gameDir, 'CardGames.dll'), '/assets/CardGames.dll'],
  [join(gameDir, 'en-US', 'PurblePlace.exe.mui'), '/assets/PurblePlace.exe.mui']
];

// ---- one tool, one process ----------------------------------------------------------------
if (one) {
  const require = createRequire(import.meta.url);
  // The web build emits these tools modularized (see CMakeLists.txt): node's module scope
  // already owns an identifier called Module, so a generated script cannot pick a caller's
  // Module up the way the browser page can.  The factory takes it instead.
  const factory = require(join(buildDir, one));
  await factory({
    print: (line) => console.log(line),
    printErr: (line) => console.log(line),
    // Emscripten calls a preRun callback with the module as its argument.  FS.writeFile does not
    // create the directory it writes into, so the asset folder is made first.
    preRun: [(module) => {
      if (!module.FS.analyzePath('/assets').exists) module.FS.mkdir('/assets');
      for (const [host, guest] of GAME) {
        module.FS.writeFile(guest, new Uint8Array(readFileSync(host)));
      }
    }],
    arguments: oneArgs
  });
} else {

// ---- the cases, each in its own process ----------------------------------------------------
const dlls = ['/assets/PurblePlace.dll', '/assets/PurblePlace2.dll', '/assets/CardGames.dll'];
const mui = '/assets/PurblePlace.exe.mui';

function tool(script, args) {
  const result = spawnSync(process.execPath, [import.meta.filename, buildDir, gameDir, script, ...args],
                           { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
  return (result.stdout || '') + (result.stderr || '');
}

let failures = 0;
function check(name, condition, detail) {
  if (condition) {
    console.log(`PASS  ${name}${detail ? '  (' + detail + ')' : ''}`);
  } else {
    failures++;
    console.log(`FAIL  ${name}${detail ? '  (' + detail + ')' : ''}`);
  }
}
const firstMatch = (text, re) => (text.match(re) || [])[0] || '';

// 1. PE resources: the three modules parse and their resource tables read the same counts as
//    the desktop tools (976 + 164 + 224 resources).
const summary = tool('pple-res.js', ['summary', ...dlls]);
const counts = [...summary.matchAll(/resources\s*:\s*(\d+)/g)].map((m) => Number(m[1]));
check('summary reads all three modules',
      counts.join('+') === '976+164+224',
      counts.length ? counts.join(' + ') + ' resources' : firstMatch(summary, /resources.*/));

// 2. Every CAB resource inflates and matches its own header: the strongest single data check.
const verify = tool('pple-res.js', ['verify', ...dlls]);
const inflated = [...verify.matchAll(/(\d+) ok, (\d+) failed/g)];
check('every CAB inflates and verifies',
      inflated.length > 0 && inflated.every((m) => m[2] === '0') && /block size mismatches\s*:\s*0/.test(verify),
      inflated.map((m) => m[1]).join(' + ') + ' CABs inflated, 0 failed');

// 3. PNG decoding across all three modules, against the same 67 / 20 / 27 the desktop decodes.
const png = tool('pple-res.js', ['pngscan', ...dlls]);
const pngCounts = [...png.matchAll(/PNG resources: (\d+), decoded: (\d+)/g)];
check('PNG scan decodes every PNG in all three modules',
      pngCounts.map((m) => m[1]).join('/') === '67/20/27' &&
      pngCounts.every((m) => m[1] === m[2]),
      pngCounts.map((m) => m[1]).join(' / ') + ' decoded');

// 4. The .mui string table.
const strings = tool('pple-res.js', ['strings', mui]);
check('the .mui string table loads',
      /RT_STRING blocks, \d+ non-empty strings/.test(strings), firstMatch(strings, /\d+ RT_STRING blocks.*/));

// 5. The rules, with no SDL at all: a whole Pairs round played out, with the documented score.
const pairs = tool('pple-res.js', ['pairs', '/assets/PurblePlace2.dll', '--autoplay']);
const score = pairs.split('\n').filter((l) => /score|rank/i.test(l)).pop() || '';
check('a Pairs round plays headlessly and scores 390 / 2690',
      /points 390\b/.test(pairs) && /2690/.test(pairs), score.trim());

// 6. The XML inventory, which resolves references across all three modules.
const xml = tool('pple-xml.js', [...dlls, '--refs']);
const xmlCounts = firstMatch(xml, /\d+ references[^\n]*/);
check('the XML inventory resolves references', /resolved/.test(xml), xmlCounts.trim());

// 7. The double-DPI x2 pages the game's own high-DPI art needs are present in the data.
const split = tool('pple-res.js', ['split', ...dlls]);
check('the split-sheet table lists its x2 pages',
      /split sheet\(s\)/.test(split) && /x21\.jpg/.test(split), firstMatch(split, /\d+ split sheet\(s\)/));

console.log(failures === 0 ? '\nall web data checks passed' : `\n${failures} check(s) failed`);
process.exit(failures === 0 ? 0 : 1);
}
