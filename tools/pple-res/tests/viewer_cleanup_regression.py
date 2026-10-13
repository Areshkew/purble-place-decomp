"""Normal-play HUD, diagnostic compatibility and Windows GUI subsystem checks.

python viewer_cleanup_regression.py BUILD_DIR SAMPLE_DIR FRESH_TRACE_DIR [BASELINE_MENU_PPM]
The sample DLLs stay local. Each subprocess captures stdout/stderr, including GUI builds.
"""
import re
import struct
import subprocess
import sys
from pathlib import Path
from PIL import Image

build, sample, trace = map(Path, sys.argv[1:4])
trace.mkdir(parents=True, exist_ok=False)
viewer = build.resolve() / 'pple-view.exe'
modules = [str(sample / n) for n in ('PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll')]
pairs_args = ['--scene', r'UI\CONCENTRATION\CONCENTRATIONGAME.XML', '--pairs', 'Beginner,0,0']

exe = viewer.read_bytes()
pe = struct.unpack_from('<I', exe, 0x3c)[0]
subsystem = struct.unpack_from('<H', exe, pe + 24 + 68)[0]
assert subsystem == 2, f'Expected Windows GUI subsystem, got {subsystem}'


def run(name, args, strip, builds=None):
    shot = trace / (name + '.ppm')
    result = subprocess.run([str(viewer), *modules, '--mute', '--no-hidpi',
                             '--width', '800', '--height', '576', *args,
                             '--shot', str(shot)], capture_output=True, text=True, timeout=60)
    log = result.stdout + result.stderr
    (trace / (name + '.log')).write_text(log, encoding='utf-8')
    assert result.returncode == 0, (name, log)
    assert shot.read_bytes().startswith(b'P6\n800 576\n255\n'), name
    assert f'HUD strip {strip} px' in log, (name, log)
    if builds is not None:
        count = re.search(r'hud: (\d+) text rebuild', log)
        assert count and int(count[1]) == builds, (name, log)
    print('PASS', name)
    return shot


for name, args in [('menu', ['--menu']), ('pairs', pairs_args),
                   ('cakes', ['--cakes', 'Beginner']), ('shop', ['--shop', 'Beginner']),
                   ('chooser', ['--difficulty'])]:
    run(name + '-clean', [*args, '--no-debug'], 0, 0)

menu = run('menu-diagnostic-auto', ['--menu'], 63)
explicit = run('menu-diagnostic-explicit', ['--menu', '--debug'], 63)
def compare_static_menu(a, b):
    # Menu clouds use wall time, even on the first offscreen frame. Compare the
    # diagnostic strip and static lower half, not animation frames under host load.
    with Image.open(a) as left, Image.open(b) as right:
        for rect in [(0, 0, 800, 63), (0, 350, 800, 576)]:
            assert left.crop(rect).tobytes() == right.crop(rect).tobytes(), (a, b, rect)

compare_static_menu(menu, explicit)
if len(sys.argv) > 4:
    compare_static_menu(menu, Path(sys.argv[4]))

# One initial scene HUD + one board HUD; no repeated rasterization on idle ticks.
run('pairs-hud-cache', [*pairs_args, '--no-deal', '--shot-delay', '1200'], 80, 2)
run('pairs-hud-cache-clean', [*pairs_args, '--no-deal', '--shot-delay', '1200', '--no-debug'], 0, 0)
# Exercise actual interactive defaults, without --shot or --no-debug enabling a hook.
for name, args in [('menu', ['--menu']), ('pairs', pairs_args),
                   ('cakes', ['--cakes', 'Beginner']), ('shop', ['--shop', 'Beginner']),
                   ('chooser', ['--difficulty'])]:
    result = subprocess.run([str(viewer), *modules, '--mute', '--no-hidpi', *args,
                             '--script-click', 'key:q;key:q'], capture_output=True,
                            text=True, timeout=30)
    log = result.stdout + result.stderr
    (trace / (name + '-live.log')).write_text(log, encoding='utf-8')
    assert result.returncode == 0 and 'HUD strip 0 px' in log and 'hud: 0 text rebuild' in log, (name, log)
    print('PASS', name + '-interactive-default')
print('PASS Windows GUI subsystem, 14 viewer runs, diagnostic baseline and HUD texture reuse')
