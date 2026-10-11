"""Cakes audio dispatch with local assets and a real SDL playback device.

Run: cakes_audio_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR
Logs verify decode/queue success, not human listening or exact original cue timing.
"""
from pathlib import Path
import subprocess
import sys

build, sample, output = (Path(p).resolve() for p in sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
assets = [str(sample / n) for n in ('PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll')]
runs = 0


def run(name, args, muted=False):
    global runs
    log = output / (name + '.log')
    command = [str(build / 'pple-view.exe'), *assets, *args,
               '--width', '800', '--height', '576', '--shot', str(output / (name + '.ppm'))]
    if muted:
        command.append('--mute')
    with log.open('w', encoding='utf-8') as stream:
        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=90)
    text = log.read_text(encoding='utf-8')
    assert 'cakes audio: failed' not in text, name
    if muted:
        assert 'cakes audio: queued' not in text and 'events decoded' not in text, name
    else:
        assert '19/19 events decoded' in text, name
        assert 'audio cakes dummy:' not in text, (name, 'requires a playback device')
    runs += 1
    return text


def queued(text, *events):
    for event in events:
        assert 'cakes audio: queued ' + event in text, event


for mode in ('Beginner', 'Intermediate', 'Advanced'):
    args = ['--cakes', mode, '--cakes-solve', '--no-hidpi', '--shot-delay', '700']
    text = run('solve-' + mode, args)
    assert 'cakes solve: finished, WON' in text
    queued(text, 'CC_CAKEONTV', 'CC_CAKEPANBUTTON', 'CC_CAKEBATTERBUTTON',
           'CC_FROSTINGBUTTON', 'CC_DECORATIONBUTTON', 'RIGHT_BUTTON',
           'CC_CONVEYOR', 'CC_CORRECTCAKE', 'CC_WONGAME')
    if mode != 'Beginner':
        queued(text, 'CC_CAKEFILLINGBUTTON')
    muted = run('muted-' + mode, args, True)
    assert 'cakes solve: finished, WON' in muted

for button_y, event in ((315, 'CC_FLAME'), (375, 'CC_SUGAR')):
    text = run(event, ['--cakes', 'Advanced', '--no-tips', '--script-click',
               f'100,315;260,440;200,315;260,440;260,440;410,315;260,440;510,315;260,440;610,{button_y}',
               '--shot-delay', '700'])
    queued(text, event)

text = run('normal-tools', ['--cakes', 'Beginner', '--no-tips', '--script-click',
           '310,315;200,315;260,440;310,315;200,440', '--shot-delay', '700'])
queued(text, 'CC_TOOLBUTTONS', 'BAD_MOVE', 'CC_CAKEPANBUTTON', 'CC_CAKEBATTERBUTTON',
       'LEFT_BUTTON', 'RIGHT_BUTTON', 'CC_CONVEYOR')
text = run('loss', ['--cakes', 'Intermediate', '--script-click',
           ';'.join(['260,440'] * 23), '--shot-delay', '700'])
assert 'cakes: LOST after 3 mistake(s)' in text
queued(text, 'CC_BADCAKE', 'CC_LOSTGAME', 'CC_CHEFENTER')
# Cakes -> menu -> Pairs -> menu -> Cakes: both preload sets share the same device.
text = run('navigation', ['--menu', '--no-intro', '--no-tips', '--script-click',
           '330,250;400,170;50,465;100,180;400,175;760,470;330,250;400,170', '--shot-delay', '700'])
assert '14/14 events decoded' in text
assert text.count('19/19 events decoded') == 1
queued(text, 'CC_CAKEONTV', 'CC_CHEFENTER')
print(f'PASS: {runs} Cakes audio runs; 19 cached events; dispatch, mute and navigation')
