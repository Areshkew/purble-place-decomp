"""Comfy Cakes chooser, modal results and visual regressions using local assets.
Run: cakes_ui_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR
"""
from pathlib import Path
import re
import subprocess
import sys
from PIL import Image

build, sample, output = (Path(p).resolve() for p in sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
assets = [str(sample / n) for n in ('PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll')]
runs = 0


def run(name, dpi, args):
    global runs
    shot = output / (name + '.ppm')
    with (output / (name + '.log')).open('w', encoding='utf-8') as stream:
        subprocess.run([str(build / 'pple-view.exe'), *assets, '--mute', '--no-tips',
                        '--' + dpi, '--width', '800', '--height', '576', '--shot', str(shot),
                        *args], stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=90)
    text = (output / (name + '.log')).read_text(encoding='utf-8')
    assert not any(s in text for s in ('not found', 'node has no <Animation>')), name
    image = Image.open(shot).convert('RGB')
    image.save(shot.with_suffix('.png'))
    runs += 1
    return text, image


def last_draws(text):
    text = text[text.rfind('  draw Root/Background'):]
    return {n: tuple(map(float, (x, y, w, h))) for n, x, y, w, h in re.findall(
        r'  draw (\S+)\s+([\d.-]+),([\d.-]+) ([\d.-]+)x([\d.-]+)', text)}


def platform_sources(text):
    return re.findall(r'  source ConveyorPlatform\s+([\d.-]+),([\d.-]+) alpha (\d+)', text)


for dpi in ('no-hidpi', 'hidpi'):
    text, idle = run('idle-' + dpi, dpi, ['--cakes', 'Beginner'])
    draws = last_draws(text)
    assert text.count('draw CakeScreen/BackgroundSprite') == 2, 'one TV frame per dump'
    # A point above the target cake must show the shipped purple screen, not opaque black.
    r, g, b = idle.getpixel((50, 135))
    assert min(r, g, b) > 60 and b > g, ('purple TV background', r, g, b)
    order = text[text.rfind('  draw Root/Background'):]
    assert order.index('draw CakeScreen/BackgroundSprite') < order.index('draw ScreenSprite') < order.index('draw GlassSprite')

    # All three difficulties are reached from the menu chooser, without starting a round first.
    for mode, y in (('Beginner', 170), ('Intermediate', 238), ('Advanced', 306)):
        selected, selected_image = run('choose-' + mode + '-' + dpi, dpi,
                          ['--menu', '--script-click', f'330,250;400,{y}'])
        assert selected.index('difficulty: Select Difficulty') < selected.index('cakes: new ')
        assert f'difficulty: {mode} chosen' in selected
        assert f'cakes: new {"Expert" if mode == "Advanced" else mode} game' in selected
        assert 'intro:' not in selected, 'no Pairs intro on Cakes'
        r, g, b = selected_image.getpixel((15, 130))
        assert min(r, g, b) > 60 and b > g + 5, 'capture must show the game, not the previous chooser'
    chooser, chooser_image = run('chooser-' + dpi, dpi, ['--menu', '--script-click', '330,250'])
    assert 'cakes: new ' not in chooser
    assert min(chooser_image.getpixel((400, 150))) > 210, 'capture must show the chooser panel, not the menu'

    # The result is modal. Room buttons behind it cannot move the belt or tools.
    win, _ = run('win-modal-' + dpi, dpi, ['--cakes', 'Beginner', '--cakes-solve',
                 '--script-click', '200,315;260,440;move:400,235'])
    assert 'result panel: WON, score 1650' in win
    after = win[win.index('cakes solve: finished'):]
    assert 'cakes: station' not in after and 'cakes: belt' not in after
    again, _ = run('win-again-' + dpi, dpi, ['--cakes', 'Beginner', '--cakes-solve',
                   '--script-click', '400,235'])
    assert 'cakes: New Game -> a new round' in again
    assert again.count('cakes: new Beginner game') == 2
    assert again.count('result panel: WON') == 1, 'restart is playable, not auto-solved again'
    change, _ = run('win-difficulty-' + dpi, dpi, ['--cakes', 'Beginner', '--cakes-solve',
                    '--script-click', '400,300;400,238'])
    assert 'cakes: Change Difficulty -> the chooser' in change
    assert 'cakes: new Intermediate game' in change
    menu, _ = run('win-menu-' + dpi, dpi, ['--cakes', 'Beginner', '--cakes-solve',
                  '--script-click', '400,365;330,250'])
    assert 'cakes: Main Menu -> main menu' in menu
    assert 'Comfy Cakes difficulty chooser' in menu

    # Three rejected empty cakes end an Intermediate round; restart works for a loss too.
    mistakes = ';'.join(['260,440'] * 23)
    loss, _ = run('loss-' + dpi, dpi, ['--cakes', 'Intermediate', '--script-click', mistakes,
                  '--shot-delay', '800'])
    assert 'cakes: LOST after 3 mistake(s)' in loss and 'result panel: LOST' in loss
    restarted, _ = run('loss-again-' + dpi, dpi, ['--cakes', 'Intermediate',
                       '--script-click', mistakes + ';400,235'])
    assert restarted.count('cakes: new Intermediate game') == 2

    # The end platform must not run from ordinary belt/chute motion or a hover rebuild.
    slide, _ = run('slide-platform-' + dpi, dpi, ['--cakes', 'Beginner',
                   '--script-click', '260,440', '--shot-delay', '180'])
    assert len(set(platform_sources(slide))) == 1, 'dispatch platform is idle during a belt slide'
    hover, _ = run('hover-platform-' + dpi, dpi, ['--cakes', 'Beginner',
                   '--script-click', '260,440;wait:180;move:410,315', '--shot-delay', '60'])
    assert len(set(platform_sources(hover))) == 1, 'hover cannot start the dispatch platform'

    # Both arrow halos are centred on their corresponding button, and focus stays exclusive.
    for side, x in (('Left', 200), ('Right', 270)):
        focused, _ = run('arrow-' + side + '-' + dpi, dpi, ['--cakes', 'Beginner',
                         '--script-click', f'move:{x},440'])
        items = last_draws(focused)
        face = items['Slide' + side + 'Button']
        halo = items['Slide' + side + 'HighlightSprite']
        assert abs((face[0] + face[2] / 2) - (halo[0] + halo[2] / 2)) <= 1
        assert abs((face[1] + face[3] / 2) - (halo[1] + halo[3] / 2)) <= 1
        other = 'Right' if side == 'Left' else 'Left'
        assert 'Slide' + other + 'HighlightSprite' not in items
    print(dpi + ': chooser, win/loss actions, purple TV, stable platform and centred focus PASS')
print(f'PASS: {runs} UI regression runs')
