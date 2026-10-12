"""Normal-click machine/belt/boxing captures against local assets, in both DPI modes.
Run: cakes_animation_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR
The timings are sampled with tolerance; these checks do not claim original-game pixel parity.
"""
from pathlib import Path
import re
import subprocess
import sys
from PIL import Image

build, sample, output = map(lambda p: Path(p).resolve(), sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
assets = [str(sample / n) for n in ('PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll')]
runs = 0

def run(name, dpi, script='', delay=0, difficulty='Beginner'):
    global runs
    args = [str(build / 'pple-view.exe'), *assets, '--cakes', difficulty, '--mute', '--no-tips',
            '--' + dpi, '--width', '800', '--height', '576', '--shot', str(output / (name + '.ppm'))]
    if script:
        args += ['--script-click', script]
    if delay:
        args += ['--shot-delay', str(delay)]
    with (output / (name + '.log')).open('w', encoding='utf-8') as stream:
        subprocess.run(args, stdout=stream, stderr=subprocess.STDOUT, timeout=90, check=True)
    text = (output / (name + '.log')).read_text(encoding='utf-8')
    assert 'node has no <Animation>' not in text and 'not found' not in text, name
    captured = text[text.rfind('  draw Root/Background'):]
    names = re.findall(r'  draw (\S+)', captured)
    tv = names.index('CakeScreen/BackgroundSprite')
    assert tv < names.index('ScreenSprite') < names.index('GlassSprite'), (name, 'target TV stack')
    assert all(i < tv for i, n in enumerate(names) if n == 'WaxPaper'), (name, 'paper behind TV')
    right_tv = names.index('ShippingSignBackground')
    assert right_tv < names.index('ShippingSign'), (name, 'shipping TV stack')
    assert all(i < right_tv for i, n in enumerate(names) if n in ('CakeBox', 'DroppingBox')), (name, 'gift behind TV')
    Image.open(output / (name + '.ppm')).save(output / (name + '.png'))
    runs += 1
    return text

def draws(text):
    # Each dump begins with the background. Keep only the last, captured draw list.
    text = text[text.rfind('  draw Root/Background'):]
    geometry = {name: tuple(map(float, (x, y, w, h)))
                for name, x, y, w, h in re.findall(r'  draw (\S+)\s+([\d.-]+),([\d.-]+) ([\d.-]+)x([\d.-]+)', text)}
    sources = {name: (float(x), float(y), int(alpha))
               for name, x, y, alpha in re.findall(r'  source (\S+)\s+([\d.-]+),([\d.-]+) alpha (\d+)', text)}
    return {name: value + sources[name] for name, value in geometry.items()}

def has(items, word):
    return any(word in key for key in items)

# Default Beginner order: rectangle, strawberry batter, lemon icing, heart decoration.
ship = '200,350;260,440;310,350;260,440;410,385;260,440;530,335;260,440;260,440'
wrong = ';'.join(['260,440'] * 5)
for dpi in ('no-hidpi', 'hidpi'):
    idle = draws(run('idle-' + dpi, dpi))
    assert 'WaxPaper' in idle and 'CakeBox' not in idle and 'DroppingBox' not in idle
    early = draws(run('pan-early-' + dpi, dpi, '200,315', 30))
    assert not has(early, 'CakePanSquish'), 'pan must wait for action frame 4'
    falling = draws(run('pan-fall-' + dpi, dpi, '200,315', 240))
    assert has(falling, 'CakePanSquish'), 'pan must become visible at the drop cue'
    belt = draws(run('belt-mid-' + dpi, dpi, '260,440', 180))
    settled = draws(run('belt-end-' + dpi, dpi, '260,440', 800))
    assert idle['WaxPaper'][0] < belt['WaxPaper'][0] < settled['WaxPaper'][0]
    assert abs(settled['WaxPaper'][0] - idle['WaxPaper'][0] - 98) < 1, 'cake must reach the next 98px slot without drift'
    assert belt['MainBeltSprite'][4:6] != idle['MainBeltSprite'][4:6], 'roller frame must advance'
    batter = draws(run('batter-shape-' + dpi, dpi, '200,350;260,440;310,350', 280))
    assert has(batter, 'RectangleBatterFill') and not has(batter, 'RoundBatterFill') and not has(batter, 'HeartBatterFill')
    assert batter['BatterTool/UI\\ComfyCakes\\Machines\\BatterMachineSprite.xml'][4:6] == idle['BatterTool/UI\\ComfyCakes\\Machines\\BatterMachineSprite.xml'][4:6], 'one-frame machine must clamp, not sample unrelated sheet art'
    icing_script = '200,315;260,440;310,315;260,440;410,315'
    icing_early = draws(run('icing-early-' + dpi, dpi, icing_script, 80))
    icing_stream = draws(run('icing-stream-' + dpi, dpi, icing_script, 360))
    icing_splat = draws(run('icing-splat-' + dpi, dpi, icing_script, 620))
    assert not has(icing_early, 'IcingStream') and not has(icing_early, 'IcingSplat')
    assert has(icing_stream, 'IcingStream') and has(icing_splat, 'IcingSplat')
    decoration_script = icing_script + ';260,440;490,335'
    deco_early = draws(run('deco-early-' + dpi, dpi, decoration_script, 50))
    deco_fall = draws(run('deco-fall-' + dpi, dpi, decoration_script, 280))
    assert not has(deco_early, 'FallingDecorations') and has(deco_fall, 'FallingDecorations')
    filling_script = '100,315;260,440;200,315;260,440;300,315'
    filling_early = draws(run('filling-early-' + dpi, dpi, filling_script, 50, 'Intermediate'))
    filling_splat = draws(run('filling-splat-' + dpi, dpi, filling_script, 330, 'Intermediate'))
    assert not has(filling_early, 'FillingSplat') and has(filling_splat, 'FillingSplat')
    final_script = '100,315;260,440;200,315;260,440;260,440;410,315;260,440;260,440;625,380'
    final_rotate_text = run('final-rotate-' + dpi, dpi, final_script, 260, 'Advanced')
    final_action_text = run('final-action-' + dpi, dpi, final_script, 720, 'Advanced')
    final_rotate, final_action = draws(final_rotate_text), draws(final_action_text)
    assert 'station 4 (final touch), LAYERS 14 ->' in final_rotate_text
    assert has(final_rotate, 'FinalTouchMachineBase') and has(final_action, 'FinalTouchMachineAction')
    assert not has(final_rotate, 'FinalTouchMachineAction'), 'action waits for the base rotation'
    drop_text = run('box-drop-' + dpi, dpi, ship, 720)
    drop = draws(drop_text)
    assert 'cake judged correct (1/5)' in drop_text and 'next target cake' in drop_text
    assert 'DroppingBox' in drop and 'CakeBox' not in drop
    gift_text = run('gift-dispatch-' + dpi, dpi, ship, 1850)
    gift = draws(gift_text)
    assert 'CakeBox' in gift and 'DroppingBox' not in gift
    assert gift['CakeBox'][0] > 687 and gift['CakeBox'][1] < 155, 'gift must move along the dispatch belt'
    gift_slots = re.findall(r'  source ScreenSlot\s+[\d.-]+,[\d.-]+ alpha (\d+)',
                            gift_text[gift_text.rfind('  draw Root/Background'):])
    assert gift_slots == ['0'] * 8, 'delivery must hide every score mark'
    done_text = run('dispatch-done-' + dpi, dpi, ship, 5100)
    done = draws(done_text)
    assert 'cakes dispatch: complete' in done_text and 'CakeBox' not in done and 'DroppingBox' not in done
    done_slots = re.findall(r'  source ScreenSlot\s+[\d.-]+,[\d.-]+ alpha (\d+)',
                            done_text[done_text.rfind('  draw Root/Background'):])
    assert done_slots == ['255'] + ['0'] * 7, 'earned score returns after delivery'
    assert gift['ShippingSign'][4:6] != idle['ShippingSign'][4:6]
    assert done['ShippingSign'][4:6] == idle['ShippingSign'][4:6]
    trash_text = run('trash-' + dpi, dpi, wrong, 850)
    trash = draws(trash_text)
    assert 'cake judged wrong (1)' in trash_text and 'next target cake' not in trash_text
    assert 'CakeBox' not in trash and 'DroppingBox' not in trash
    assert trash['TrashCan'][4:6] != idle['TrashCan'][4:6]
    reset_text = run('reset-box-' + dpi, dpi, ship + ';wait:900;key:n', 1000)
    reset = draws(reset_text)
    assert reset_text.count('cakes: new Beginner game') == 2
    assert 'CakeBox' not in reset and 'DroppingBox' not in reset
    # The real hover rebuild must preserve clocks, not recreate a stationary gift.
    hover_text = run('hover-box-' + dpi, dpi, ship + ';wait:1300;move:410,315', 650)
    hover = draws(hover_text)
    assert 'CakeBox' in hover and hover['CakeBox'][0] > 687
    print(dpi + ': cues, single shape, 98px aligned belt/roller, drop, gift, dispatch, trash, reset, hover PASS')
print(f'PASS: {runs} normal-click animation captures')
