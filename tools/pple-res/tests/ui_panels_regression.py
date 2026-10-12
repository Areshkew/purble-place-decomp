"""Shared dialog/panel atlas seams and button focus, using offline game assets.
Run: ui_panels_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR
"""
from pathlib import Path
import re
import subprocess
import sys
from PIL import Image, ImageChops

build, sample, output = (Path(p).resolve() for p in sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
assets = [str(sample / n) for n in ('PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll')]
runs = 0


def run(name, dpi, args, size=(800, 496), tips=False):
    global runs
    shot = output / (name + '.ppm')
    command = [str(build / 'pple-view.exe'), *assets, '--mute', '--' + dpi,
               '--mui', str(sample / 'en-US' / 'PurblePlace.exe.mui'),
               '--width', str(size[0]), '--height', str(size[1]), '--shot', str(shot)]
    if not tips:
        command.append('--no-tips')
    with (output / (name + '.log')).open('w', encoding='utf-8') as log:
        subprocess.run([*command, *args], stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=90)
    text = (output / (name + '.log')).read_text(encoding='utf-8')
    assert 'not found' not in text and 'node has no <Animation>' not in text, name
    image = Image.open(shot).convert('RGB')
    image.save(shot.with_suffix('.png'))
    runs += 1
    return text, image


def hud_height(image):
    # Navigation can rebuild the HUD after the one-time DPI log was printed.
    # The diagnostic strip is black; scene letterboxing is (24, 24, 32).
    return next(y for y in range(image.height) if image.getpixel((0, y)) != (0, 0, 0))


def white_join(image, origin, hud=0):
    # The upper-left corner ends 12 scene pixels from the left. The old UVs
    # put a dark filter-gutter stripe at this join for the first 13 pixels.
    # Probe above the labels and below the border: all channels must be white.
    scale = min(image.width / 800, (image.height - hud) / 496)
    ox = (image.width - 800 * scale) / 2
    oy = hud + (image.height - hud - 496 * scale) / 2
    x = round(ox + (origin[0] + 12.5) * scale)
    values = [min(image.getpixel((x, round(oy + (origin[1] + dy) * scale))))
              for dy in (4, 5, 6, 7, 8)]
    assert min(values) >= 240, ('filter gutter at panel join', origin, values)


for dpi in ('no-hidpi', 'hidpi'):
    for game, click in (('pairs', '140,250'), ('cakes', '330,250'), ('shop', '600,250')):
        idle = None
        for size in ((800, 496), (1000, 700), (1400, 900)):
            name = f'chooser-{game}-{dpi}-{size[0]}'
            text, image = run(name, dpi, ['--menu', '--script-click', click], size)
            assert 'difficulty: Select Difficulty, 3 option(s)' in text, name
            hud = hud_height(image)
            white_join(image, (180, 36), hud=hud)
            for y in (148, 216, 284):
                white_join(image, (274, y), hud=hud)
            if size[0] == 800:
                idle = image
                idle_hud = hud
        _, focused = run(f'focus-{game}-{dpi}', dpi,
                         ['--menu', '--script-click', click + ';move:400,170'])
        bounds = ImageChops.difference(idle, focused).getbbox()
        assert bounds is not None, 'button must visibly focus'
        scale = min(idle.width / 800, (idle.height - idle_hud) / 496)
        ox = (idle.width - 800 * scale) / 2
        oy = idle_hud + (idle.height - idle_hud - 496 * scale) / 2
        expected = (ox + 274 * scale, oy + 148 * scale, ox + 526 * scale, oy + 204 * scale)
        assert bounds[0] >= expected[0] - 1 and bounds[1] >= expected[1] - 1, bounds
        assert bounds[2] <= expected[2] + 1 and bounds[3] <= expected[3] + 1, bounds
        # Same unchanged hit target starts the selected game, not a panel behind it.
        selected, _ = run(f'choose-{game}-{dpi}', dpi,
                          ['--menu', '--script-click', click + ';400,170'], (800, 576))
        assert 'difficulty: Beginner chosen' in selected
        marker = {'pairs': r'scene UI\CONCENTRATION\CONCENTRATIONGAME.XML:', 'cakes': 'cakes: new Beginner game',
                  'shop': 'shop: new Beginner round'}[game]
        assert marker in selected, (game, marker)

    # All three end-of-round panels use the same nine-slice and action buttons.
    text, image = run('result-pairs-' + dpi, dpi,
                      ['--scene', r'UI\CONCENTRATION\CONCENTRATIONGAME.XML',
                       '--pairs', 'Beginner,0', '--pairs-solve', '--shot-delay', '2500'], (800, 576))
    assert 'result panel: WON' in text
    white_join(image, (200, 80), hud=80)
    for y in (208, 274, 340):
        white_join(image, (274, y), hud=80)
    text, image = run('result-cakes-' + dpi, dpi,
                      ['--cakes', 'Beginner', '--cakes-solve'], (800, 576))
    assert 'result panel: WON, score 1650' in text
    white_join(image, (200, 80), hud=80)
    for y in (208, 274, 340):
        white_join(image, (274, y), hud=80)
    fixture, _ = run('shop-fixture-' + dpi, dpi,
                     ['--shop', 'Beginner', '--shop-seed', '0'], (800, 576))
    hidden = re.search(r'hidden ((?:-?\d+ ){4}-?\d+)', fixture).group(1)
    text, image = run('result-shop-' + dpi, dpi,
                      ['--shop', 'Beginner', '--shop-seed', '0', '--shop-guess', hidden.replace(' ', ',')],
                      (800, 576))
    assert 'shop: won in 1 guess(es)' in text and 'result panel: WON' in text
    hud = hud_height(image)
    white_join(image, (200, 80), hud=hud)
    for y in (208, 274, 340):
        white_join(image, (274, y), hud=hud)
    # Intro (white) and Error (red) styles share the loader, including close buttons.
    for kind, tip_id, script, extra in (
        ('white', 'SneakPeak', 'move:20,470', []),
        ('red', 'BlankCard', 'wait:1400;move:200,50', ['--pairs-click', '0,1']),
    ):
        text, image = run(kind + '-' + dpi, dpi,
                          ['--scene', r'UI\CONCENTRATION\CONCENTRATIONGAME.XML',
                           '--pairs', 'Beginner,0', *extra, '--script-click', script],
                          (800, 576), tips=True)
        assert f'tip {tip_id}:' in text
        assert ('Intro body' if kind == 'white' else 'Error body') in text
        white_join(image, (615, 125), hud=80)
    print(dpi + ': three-game chooser seams, fractional scaling, focus, clicks, results and tip styles PASS')
print(f'PASS: {runs} shared UI regression runs')
