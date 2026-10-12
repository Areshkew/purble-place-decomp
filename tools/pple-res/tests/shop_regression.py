"""Shop selection/feedback/curtain/reveal through normal scene clicks, both DPI modes.
Run: shop_regression.py BUILD_DIR SAMPLE_DIR FRESH_TRACE_DIR
Uses only the supplied local game assets. Captures/logs remain in TRACE_DIR.
"""
from pathlib import Path
import itertools
import re
import subprocess
import sys
from PIL import Image, ImageChops

build, sample, out = [Path(p).resolve() for p in sys.argv[1:4]]
out.mkdir(parents=True, exist_ok=True)
assets = [str(sample / name) for name in ('PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll')]
runs = 0
colors = ('Red', 'Purple', 'Yellow', 'Blue', 'Green')
features = ('Hair', 'Eye', 'Nose', 'Mouth', 'Accessory')


def run(name, dpi, difficulty, script='', guess='', delay=700):
    global runs
    args = [str(build / 'pple-view.exe'), *assets, '--shop', difficulty,
            '--shop-seed', '1', '--shop-dump', '--mute', '--' + dpi,
            '--width', '800', '--height', '559', '--shot-delay', str(delay),
            '--shot', str(out / (name + '.ppm'))]
    if script:
        args += ['--script-click', script]
    if guess:
        args += ['--shop-guess', guess]
    with (out / (name + '.log')).open('w', encoding='utf-8') as log:
        subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=45)
    text = (out / (name + '.log')).read_text(encoding='utf-8')
    strip = int(re.search(r'HUD strip (\d+) px', text)[1])
    assert strip == 63, text
    image = Image.open(out / (name + '.ppm')).convert('RGB')
    image.save(out / (name + '.png'))
    scene = image.crop((0, strip, 800, strip + 496))
    runs += 1
    return text, scene


def spots(text):
    return {m[1]: tuple(map(int, m.groups()[1:])) for m in re.finditer(
        r'shop spot (\S+)\s+(-?\d+),(-?\d+) (\d+)x(\d+)', text)}


def point(rect, avoid=None):
    x, y, w, h = rect
    for fx, fy in ((.5, .5), (.2, .2), (.8, .2), (.2, .8), (.8, .8)):
        px, py = int(x + w * fx), int(y + h * fy)
        if avoid:
            bx, by, bw, bh = avoid
            if bx <= px < bx + bw and by <= py < by + bh:
                continue
        return f'{px},{py}'
    raise AssertionError('No item point outside checkbox')


def selection(text):
    states = re.findall(r'shop state: selected ((?:-?\d+ ){4}-?\d+); counts (\w+)', text)
    assert states, text
    return list(map(int, states[-1][0].split())), states[-1][1]


def differs(a, b, rect):
    return ImageChops.difference(a.crop(rect), b.crop(rect)).getbbox() is not None


for dpi in ('no-hidpi', 'hidpi'):
    for difficulty, count, begin, end in (('Beginner', 3, 1, 3),
                                         ('Intermediate', 4, 0, 3), ('Advanced', 5, 0, 4)):
        prefix = difficulty.lower() + '-' + dpi
        text, closed = run(prefix + '-closed', dpi, difficulty, delay=4250)
        assert 'curtain closed for guessing, frame 0' in text
        assert text.count('shop state:') == 2, 'No per-frame room rebuild during intro'
        hidden = list(map(int, re.search(r'hidden ((?:-?\d+ ){4}-?\d+)', text)[1].split()))
        geometry = spots(text)
        item_names = {}
        for name in geometry:
            for f in range(begin, end + 1):
                for c in range(count):
                    if re.fullmatch(colors[c] + features[f] + r'[123]', name):
                        item_names[c, f] = name
        assert len(item_names) == count * count

        def click(c, f):
            name = item_names[c, f]
            return point(geometry[name], geometry.get(name + '/CheckBox'))

        guess_click = point(geometry['GuessButton'])
        text, _ = run(prefix + '-incomplete', dpi, difficulty, guess_click)
        assert 'guess refused (not every shelf is set)' in text
        assert selection(text)[1] == 'hidden'

        # Fast replacement on every shelf, then remove just the eyes.
        script = [click(c, f) for f in range(begin, end + 1) for c in range(count)]
        script.append(click(count - 1, 1))
        text, selected_image = run(prefix + '-replace', dpi, difficulty, ';'.join(script))
        expected = [-1] * 5
        for f in range(begin, end + 1):
            expected[f] = count - 1
        expected[1] = -1
        assert selection(text) == (expected, 'hidden')
        # The previous colour returns to full opacity; the new part is ghosted.
        for c in (0, count - 1):
            x, y, w, h = geometry[item_names[c, 2]]
            changed = differs(closed, selected_image, (x, y, x + w, y + h))
            assert changed == (c == count - 1), 'Selected cabinet item must fade, old item restored'

        wrong = [-1] * 5
        for f in range(begin, end + 1):
            wrong[f] = (hidden[f] + 1) % count
        wrong_spec = ','.join(map(str, wrong))
        text, judged = run(prefix + '-judged', dpi, difficulty, guess=wrong_spec, delay=4250)
        assert 'guess 1 ->' in text and selection(text)[1] == 'shown'
        text, changed = run(prefix + '-changed', dpi, difficulty,
                            click(hidden[1], 1), wrong_spec, delay=4250)
        assert selection(text)[1] == 'hidden'
        panel = (252, 85, 415, 170)
        assert differs(closed, judged, panel), 'Judged feedback should be visible'
        assert not differs(closed, changed, panel), 'Old counts must disappear when editing'
        text, _ = run(prefix + '-duplicate', dpi, difficulty, guess_click, wrong_spec)
        assert 'the same guess was already made' in text

        # Normal clicks assemble the correct face; answer stays on screen after opening.
        win_script = ';'.join([click(hidden[f], f) for f in range(begin, end + 1)] +
                              [guess_click, 'wait:800'])
        text, answer = run(prefix + '-win', dpi, difficulty, win_script)
        assert 'won in 1 guess(es)' in text
        assert 'curtain answer revealed, frame 7' in text
        assert 'answer shown' in text and 'result panel:' not in text
        assert differs(closed, answer, (65, 75, 190, 180)), 'Arch must show revealed answer'
        text, _ = run(prefix + '-continue', dpi, difficulty, win_script + ';400,360')
        assert 'result panel: WON' in text
        # Restart must cancel the revealed answer and old selections/counts.
        text, _ = run(prefix + '-restart', dpi, difficulty, win_script + ';key:n')
        assert text.count('shop: new ') == 2
        assert selection(text) == ([-1] * 5, 'hidden') and 'answer hidden' in text.split('shop: new ')[-1]

        if difficulty != 'Beginner':
            # Multiple checkbox notes must never add additional selected eyes.
            eye_name = item_names[1, 1]
            second_name = item_names[2, 1]
            script = ';'.join([click(0, 1), point(geometry[eye_name + '/CheckBox']),
                               point(geometry[second_name + '/CheckBox'])])
            text, _ = run(prefix + '-notes', dpi, difficulty, script)
            assert selection(text) == ([-1, 0, -1, -1, -1], 'hidden')
            # Loss also reveals the full hidden face. Unique wrong guesses exhaust the limit.
            limit = 7 if difficulty == 'Intermediate' else 13
            guesses = []
            for combo in itertools.product(range(count), repeat=count):
                values = [-1] * 5
                values[begin:end + 1] = combo
                if values == hidden:
                    continue
                guesses.append(','.join(map(str, values)))
                if len(guesses) == limit:
                    break
            text, lost = run(prefix + '-lose', dpi, difficulty, guess=';'.join(guesses))
            assert f'lost after {limit} guess(es)' in text
            assert 'curtain answer revealed, frame 7' in text
            assert differs(closed, lost, (65, 75, 190, 180))

print(f'PASS: {runs} Shop viewer runs; single-slot selection/ghosting, notes, feedback, intro, win/loss reveal, continue and restart; all difficulties, both DPI modes')
