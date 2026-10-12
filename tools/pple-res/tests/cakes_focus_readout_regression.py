"""Tool-group focus and shipping readout regressions, using only local game assets.
Run: cakes_focus_readout_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR
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


def run(name, dpi, difficulty='Beginner', script='', delay=0, solve=False):
    global runs
    shot = output / (name + '.ppm')
    args = [str(build / 'pple-view.exe'), *assets, '--cakes', difficulty,
            '--mute', '--no-tips', '--' + dpi, '--width', '800', '--height', '576',
            '--shot', str(shot)]
    if script:
        args += ['--script-click', script]
    if delay:
        args += ['--shot-delay', str(delay)]
    if solve:
        args += ['--cakes-solve']
    with shot.with_suffix('.log').open('w', encoding='utf-8') as stream:
        subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=90)
    text = shot.with_suffix('.log').read_text(encoding='utf-8')
    assert not any(s in text for s in ('not found', 'node has no <Animation>')), name
    Image.open(shot).save(shot.with_suffix('.png'))
    runs += 1
    return text


def last(text):
    return text[text.rfind('  draw Root/Background'):]


def draws(text):
    return [(n, tuple(map(float, (x, y, w, h)))) for n, x, y, w, h in re.findall(
        r'  draw (\S+)\s+([\d.-]+),([\d.-]+) ([\d.-]+)x([\d.-]+)', last(text))]


def slots(text):
    values = [(float(x), float(y), int(a)) for x, y, a in re.findall(
        r'  source ScreenSlot\s+([\d.-]+),([\d.-]+) alpha (\d+)', last(text))]
    assert len(values) == 8, values
    return values


def sources(text):
    return {n: (float(x), float(y)) for n, x, y, _ in re.findall(
        r'  source (\S+)\s+([\d.-]+),([\d.-]+) alpha (\d+)', last(text))}


def check_success(text, count):
    values = slots(text)
    assert values[:count] == [(990.0, 829.0, 255)] * count, values
    assert all(a == 0 for x, y, a in values[count:]), values


ship = '200,350;260,440;310,350;260,440;410,385;260,440;530,335;260,440;260,440'
wrong = ';'.join(['260,440'] * 5)
for dpi in ('no-hidpi', 'hidpi'):
    for difficulty in ('Beginner', 'Intermediate', 'Advanced'):
        idle = run('idle-' + difficulty + '-' + dpi, dpi, difficulty)
        check_success(idle, 0)
        assert all(n != 'HighlightSprite' for n, rect in draws(idle))
        for prefix in ('CakePan', 'Batter', 'Filling', 'Icing', 'Decoration', 'Final'):
            buttons = [(n, rect) for n, rect in draws(idle) if n.startswith(prefix) and n.endswith('Button')]
            if not buttons:
                continue
            name, rect = buttons[0]
            x, y, w, h = rect
            text = run('focus-' + difficulty + '-' + prefix + '-' + dpi, dpi, difficulty,
                       f'move:{x + w / 2},{y + h / 2}')
            # The pointer draws the button's own focus art (its second <Animation>) and nothing
            # else. The shipped *HighlightSprite glow is CCTipManager's instructional cue, so no
            # halo may appear over the hovered group or over either belt arrow.
            assert all(n != 'HighlightSprite' for n, rect in draws(text))
            assert 'SlideLeftHighlightSprite' not in last(text) and 'SlideRightHighlightSprite' not in last(text)
            idle_src, focus_src = sources(idle), sources(text)
            assert focus_src[name] != idle_src[name], (difficulty, prefix, name)
            for other, other_rect in buttons:
                if other != name:
                    assert focus_src[other] == idle_src[other], (prefix, other)
            # Focus never moves the buttons or creates extra focused buttons.
            focused_buttons = [(n, r) for n, r in draws(text) if n.startswith(prefix) and n.endswith('Button')]
            assert focused_buttons == buttons
            check_success(text, 0)

    correct = run('correct-' + dpi, dpi, script=ship, delay=720)
    assert 'cake judged correct (1/5)' in correct
    check_success(correct, 1)
    hovered = run('correct-hover-' + dpi, dpi, script=ship + ';wait:800;move:410,315', delay=80)
    check_success(hovered, 1)
    rejected = run('wrong-flash-' + dpi, dpi, script=wrong, delay=720)
    values = slots(rejected)
    assert 'cake judged wrong (1)' in rejected
    assert values[0][:2] == (990.0, 807.0) and values[0][2] > 0, values
    assert all(a == 0 for x, y, a in values[1:])
    expired = run('wrong-expired-' + dpi, dpi, script=wrong, delay=2900)
    check_success(expired, 0)
    # A rejection must start now, not when an earlier wrapped gift finishes dispatching.
    queued = run('wrong-queued-' + dpi, dpi,
                 script=ship + ';' + ';'.join(['260,440'] * 8), delay=720)
    assert 'cakes dispatch: queued' in queued and 'cake judged wrong (1)' in queued
    values = slots(queued)
    assert values[0] == (990.0, 829.0, 255), values
    assert values[1][:2] == (990.0, 807.0) and values[1][2] > 0, values
    assert all(a == 0 for x, y, a in values[2:])
    reset = run('readout-reset-' + dpi, dpi, script=ship + ';key:n', delay=720)
    assert reset.count('cakes: new Beginner game') == 2
    check_success(reset, 0)
    for difficulty, count in (('Beginner', 5), ('Intermediate', 5), ('Advanced', 6)):
        solved = run('solved-' + difficulty + '-' + dpi, dpi, difficulty, solve=True)
        assert 'cakes solve: finished, WON' in solved
        check_success(solved, count)  # includes the second row and final modal panel
    print(dpi + ': button-art-only tool focus, correct/rejected/queued/reset readouts PASS')
print(f'PASS: {runs} focus/readout regression runs')
