"""Real-clock conveyor/indicator/multiple-paper regressions with local shipped assets.
Run: cakes_clock_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR
"""
from pathlib import Path
import re
import subprocess
import sys
from PIL import Image

build, sample, output = [Path(p).resolve() for p in sys.argv[1:4]]
output.mkdir(parents=True, exist_ok=True)
runs = 0
assets = [str(sample / n) for n in ('PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll')]

def run(name, dpi, difficulty='Intermediate', script='', delay=900):
    global runs
    name += '-' + dpi
    args = [str(build / 'pple-view.exe'), *assets, '--cakes', difficulty, '--mute', '--no-tips',
            '--' + dpi, '--width', '800', '--height', '576', '--shot-delay', str(delay),
            '--shot', str(output / (name + '.ppm'))]
    if script:
        args += ['--script-click', script]
    with (output / (name + '.log')).open('w', encoding='utf-8') as log:
        subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=90)
    text = (output / (name + '.log')).read_text(encoding='utf-8')
    last = text[text.rfind('  draw Root/Background'):]
    draws = [(n, float(x), float(y), float(w), float(h)) for n, x, y, w, h in
             re.findall(r'  draw (\S+)\s+([\d.-]+),([\d.-]+) ([\d.-]+)x([\d.-]+)', last)]
    Image.open(output / (name + '.ppm')).save(output / (name + '.png'))
    runs += 1
    return text, draws

def timer(draws):
    return next(d[1] for d in draws if d[0] == 'TimerBarSprite')

def papers(draws):
    return [d for d in draws if d[0] == 'WaxPaper']

right, left = '260,440', '200,440'
juggle6 = ';'.join([right, left] * 3)
for dpi in ('no-hidpi', 'hidpi'):
    idle, still = run('idle-locked', dpi, delay=5700)
    assert 'automatic conveyor step' not in idle and len(papers(still)) == 1
    idle_x = timer(still)
    paused, busy = run('machine-pauses-clock', dpi, script='100,315', delay=180)
    assert timer(busy) == idle_x and 'automatic conveyor step' not in paused
    midway, mid = run('indicator-moving', dpi, script='100,315', delay=3000)
    assert idle_x + 5 < timer(mid) < idle_x + 30
    assert 'automatic conveyor step' not in midway
    automatic, moved = run('automatic-intermediate', dpi, script='100,315', delay=6600)
    assert automatic.count('automatic conveyor step') == 1
    assert abs(papers(moved)[0][1] - papers(still)[0][1] - 98) < 1
    assert timer(moved) < timer(mid), 'indicator resets after automatic slide'
    expert, advanced = run('automatic-advanced', dpi, 'Advanced', '100,315', 5600)
    assert expert.count('automatic conveyor step') == 1
    beginner, beginner_draws = run('beginner-manual', dpi, 'Beginner', '200,315', 6600)
    assert 'automatic conveyor step' not in beginner and len(papers(beginner_draws)) == 1
    assert not any(d[0] == 'TimerBarSprite' for d in beginner_draws)
    manual, reset = run('manual-resets-clock', dpi, script='100,315;wait:3000;' + right, delay=3200)
    assert 'automatic conveyor step' not in manual
    restarted, restart = run('restart-resets-clock', dpi, script='100,315;wait:2400;key:n', delay=5700)
    assert 'automatic conveyor step' not in restarted and timer(restart) == idle_x
    two, two_draws = run('multiple-manual', dpi, script='100,315;' + juggle6 + ';' + right)
    assert 'new wax paper on the chute, 2 cake(s)' in two
    assert len(papers(two_draws)) == 2 and len({p[1] for p in papers(two_draws)}) == 2
    carried, carried_draws = run('multiple-carried', dpi, script='100,315;' + juggle6 + ';' + right + ';' + right)
    assert len(papers(carried_draws)) == 2
    assert abs(papers(carried_draws)[-1][1] - papers(two_draws)[-1][1] - 98) < 1
    assert papers(carried_draws)[0][2] == papers(carried_draws)[1][2], 'new paper lands on main belt'
    assert abs(papers(carried_draws)[1][1] - papers(carried_draws)[0][1] - 3 * 98) < 1, 'separate papers retain their slot spacing'
    auto_two, auto_draws = run('multiple-automatic', dpi, script='100,315;' + juggle6, delay=6400)
    assert auto_two.count('automatic conveyor step') == 1
    assert 'new wax paper on the chute, 2 cake(s)' in auto_two and len(papers(auto_draws)) == 2
    independent, independent_draws = run('independent-machine', dpi,
        script='100,315;' + juggle6 + ';' + right + ';' + left + ';100,350')
    assert 'station 1 (pan), LAYERS 4 -> added, 1 layer(s), 2 move(s)' in independent
    assert len(papers(independent_draws)) == 2
    difficulty, changed = run('difficulty-cancels-clock', dpi,
        script='100,315;wait:2000;key:1', delay=5600)
    assert 'automatic conveyor step' not in difficulty and len(papers(changed)) == 1
    assert not any(d[0] == 'TimerBarSprite' for d in changed)
print(f'PASS: {runs} viewer runs; clock/indicator/automatic steps/multiple paper/station ownership/reset, both DPI modes')
