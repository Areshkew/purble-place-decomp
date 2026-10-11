"""Check fixed slot coordinates after slides, reversals and replacement chute entry.
Run: cakes_belt_alignment_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR
Checks reconstruction alignment, not original-game animation pixel parity.
"""
from pathlib import Path
import re
import subprocess
import sys
from PIL import Image

build, sample, output = [Path(p).resolve() for p in sys.argv[1:4]]
output.mkdir(parents=True, exist_ok=True)
assets = [str(sample / n) for n in ('PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll')]
runs = 0


def run(name, dpi, difficulty, script, slot, delay=800, between=None):
    global runs
    args = [str(build / 'pple-view.exe'), *assets, '--cakes', difficulty, '--mute', '--no-tips',
            '--' + dpi, '--width', '800', '--height', '576', '--shot-delay', str(delay),
            '--shot', str(output / (name + '.ppm'))]
    if script:
        args += ['--script-click', script]
    with (output / (name + '.log')).open('w', encoding='utf-8') as stream:
        subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT, timeout=90, check=True)
    text = (output / (name + '.log')).read_text(encoding='utf-8')
    last = text[text.rfind('  draw Root/Background'):]
    paper = re.search(r'  draw WaxPaper\s+([\d.-]+),([\d.-]+) ([\d.-]+)x([\d.-]+)', last)
    assert paper, name
    x, y, w, h = map(float, paper.groups())
    # WaxPaper registration is (53,23); container origin is (0,220).
    if between is None:
        assert abs(x - (98 * slot - 66 - 53)) < 0.1, (name, x, slot)
    else:
        assert between[0] < x < between[1], (name, x, between)
    assert abs(y - 226) < 0.1 and (w, h) == (106, 46), (name, y, w, h)
    Image.open(output / (name + '.ppm')).save(output / (name + '.png'))
    runs += 1


right, left = '260,440', '200,440'
for dpi in ('no-hidpi', 'hidpi'):
    for difficulty, start in (('Beginner', 3), ('Intermediate', 2), ('Advanced', 2)):
        prefix = difficulty + '-' + dpi + '-'
        run(prefix + 'initial', dpi, difficulty, '', start)
        forward = ';'.join([right] * 3)
        run(prefix + 'three-right', dpi, difficulty, forward, start + 3)
        run(prefix + 'reverse', dpi, difficulty, forward + ';' + ';'.join([left] * 2), start + 1)
        zigzag = ';'.join([right, left] * 5)
        run(prefix + 'zigzag', dpi, difficulty, zigzag, start)
        x0 = 98 * start - 119
        run(prefix + 'moving-hover', dpi, difficulty, right + ';move:410,315', start + 1,
            delay=180, between=(x0, x0 + 98))
        # Empty first cake is rejected; replacement enters from the chute, then travels
        # to slot 3. Its arrival must match an initial cake's slot, including its y.
        replacement = ';'.join([right] * (8 - start)) + ';wait:900;' + ';'.join([right] * 3)
        run(prefix + 'replacement', dpi, difficulty, replacement, 3)
    print(dpi + ': fixed slots, reversals, repeated slides, moving hover and chute entry PASS')
print(f'PASS: {runs} belt alignment captures')
