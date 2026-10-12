"""Normal-click eye reward, inventory, saved state and use with local assets.
Run: concentration_reward_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR
"""
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from PIL import Image

build, sample, output = [Path(p).resolve() for p in sys.argv[1:4]]
output.mkdir(parents=True, exist_ok=True)
runs = 0
assets = [str(sample / name) for name in ('PurblePlace.dll', 'PurblePlace2.dll', 'CardGames.dll')]

def run(name, dpi, state, script='', delay=300):
    global runs
    args = [str(build / 'pple-view.exe'), *assets,
            '--scene', r'UI\CONCENTRATION\CONCENTRATIONGAME.XML', '--pairs', 'Advanced,0,0',
            '--pairs-state', str(state), '--no-deal', '--no-tips', '--mute', '--' + dpi,
            '--width', '800', '--height', '576', '--shot-delay', str(delay),
            '--shot', str(output / (name + '.ppm'))]
    if script:
        args += ['--script-click', script]
    with (output / (name + '.log')).open('w', encoding='utf-8') as log:
        subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=45)
    text = (output / (name + '.log')).read_text(encoding='utf-8')
    assert 'HUD strip 80 px' in text
    Image.open(output / (name + '.ppm')).save(output / (name + '.png'))
    runs += 1
    return text

def state_xml(path):
    data = path.read_bytes()
    start = data.index('<?xml'.encode('utf-16le'))
    end_tag = '</ConcentrationSave>'.encode('utf-16le')
    end = data.index(end_tag, start) + len(end_tag)
    return ET.fromstring(data[start:end].decode('utf-16le'))

def point(index):
    # Shipped Advanced #0: 8x8, first cell 17,17, cells 49x49 + 4 spacing;
    # Board Base 150,2, choose safely inside each card. Script coordinates are scene-space.
    return f'{177 + index % 8 * 53},{29 + index // 8 * 53}'

for dpi in ('no-hidpi', 'hidpi'):
    prefix = 'eye-' + dpi
    initial = output / (prefix + '-initial.rgmh')
    assert not initial.exists(), 'Use a fresh trace directory so initial fixture starts empty'
    run(prefix + '-initial', dpi, initial, '720,460')
    root = state_xml(initial)
    assert int(root.findtext('Tokens')) == 0
    cells = root.findall('./Cells/Cell')
    eye = next(int(c.attrib['index']) for c in cells if c.attrib['bonus'] == '1')
    partner = next(int(c.attrib['index']) for c in cells
                   if c.attrib['id'] == cells[eye].attrib['id'] and int(c.attrib['index']) != eye)
    reward = output / (prefix + '-reward.rgmh')
    reward.write_bytes(initial.read_bytes())
    text = run(prefix + '-match', dpi, reward,
               point(eye) + ';wait:500;' + point(partner) + ';wait:1200;720,460')
    assert re.search(r'click\s+' + str(partner) + r'\s+-> MATCH', text), text
    root = state_xml(reward)
    assert int(root.findtext('Tokens')) == 1
    assert root.findall('./Cells/Cell')[eye].attrib['face'] == '2'
    assert root.findall('./Cells/Cell')[partner].attrib['face'] == '2'
    run(prefix + '-inventory', dpi, reward)
    image = Image.open(output / (prefix + '-inventory.png')).convert('RGB')
    gold = [sum(r > 150 and g > 80 and b < 100
                for r, g, b in list(image.crop((x, 535, x + 30, 563)).get_flattened_data()))
            for x in (8, 52, 98)]
    assert gold[0] > 100 and gold[1:] == [0, 0], 'One visible earned eye, two empty slots'
    used = output / (prefix + '-used.rgmh')
    used.write_bytes(reward.read_bytes())
    text = run(prefix + '-use', dpi, used, '20,470;wait:2200;720,460')
    assert 'sneak token 0 -> 0 left' in text, text
    root = state_xml(used)
    assert int(root.findtext('Tokens')) == 0
    assert all(c.attrib['face'] in ('0', '2') for c in root.findall('./Cells/Cell'))
    assert 'state: wrote' in text
print(f'PASS: {runs} viewer runs; normal eye match, saved token, visible inventory and earned peek use/expiry, both DPI modes')
