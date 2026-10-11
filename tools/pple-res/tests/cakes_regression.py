"""Integration checks using local game assets; no downloads or installed game writes.

Run with bundled Python:
  cakes_regression.py BUILD_DIR SAMPLE_DIR TRACE_DIR
Output captures/logs go only to TRACE_DIR. Pillow is needed only for PNG previews.
"""
from pathlib import Path
import re
import subprocess
import sys

build, sample, output = map(lambda p: Path(p).resolve(), sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
viewer = build / "pple-view.exe"
assets = [str(sample / name) for name in ("PurblePlace.dll", "PurblePlace2.dll", "CardGames.dll")]


def run(name, args):
    log = output / (name + ".log")
    with log.open("w", encoding="utf-8") as stream:
        subprocess.run([str(viewer), *assets, "--mute", *args], stdout=stream,
                       stderr=subprocess.STDOUT, check=True, timeout=90)
    return log.read_text(encoding="utf-8")


for mode, count, pan_x, batter_x, layers in (
    ("Beginner", 16, 186, 289, 3),
    ("Intermediate", 19, 86, 189, 5),
    ("Advanced", 21, 86, 189, 6),
):
    for dpi in ("no-hidpi", "hidpi"):
        name = f"cakes-{mode}-{dpi}"
        text = run(name, ["--cakes", mode, "--no-tips", "--" + dpi,
                          "--width", "800", "--height", "576", "--shot", str(output / (name + ".ppm"))])
        assert f"{count} hot-spot(s), 0 unresolved" in text, name
        assert f"target cake, {layers} layer(s)" in text, name
        assert re.search(rf"hot-spot CakePanRoundButton\s+{pan_x},298", text), name
        assert re.search(rf"hot-spot BatterChocolateButton\s+{batter_x},298", text), name
        assert "draw WaxPaper" in text, name
        target_widths = re.findall(r"draw UI\\ComfyCakes\\Layers\\\S+\s+[-\d]+,[-\d]+ (\d+)x", text)
        assert target_widths and all(int(w) <= 90 for w in target_widths), (name, "TV scale")
        for effect in ("CakePanSquish", "FallingBatter", "BatterFill", "FillingSplat", "IcingSplat", "IcingStream", "FallingDecorations"):
            assert not re.search(r"draw .*" + effect, text), (name, effect)
        assert ("hot-spot FillingChocolateButton" in text) == (mode != "Beginner"), name
        assert ("hot-spot FinalGlazeButton" in text) == (mode == "Advanced"), name
        assert ("draw TimerBarSprite" in text) == (mode != "Beginner"), name
        solve = run(name + "-solve", ["--cakes", mode, "--cakes-solve", "--" + dpi,
                    "--width", "800", "--height", "576", "--shot", str(output / (name + "-solve.ppm"))])
        assert "cakes solve: finished, WON" in solve, name
        assert "0 missed" in solve and "there is no cake under it" not in solve, name
        orders = []
        for block in re.split(r'cakes: (?:target cake|next target cake)', solve)[1:]:
            order = re.findall(r'target (?:layer|station).*?LAYERS (\d+).*?shape (-?\d+)', block)
            assert order, (name, 'order layers logged')
            orders.append(tuple(order))
        assert len(orders) == (6 if mode == 'Advanced' else 5), (name, 'new order per correct cake')
        assert all(a != b for a, b in zip(orders, orders[1:])), (name, 'adjacent TV orders differ visibly')
        assert 'node has no <Animation>' not in solve, (name, 'machine sprite state')
        # Normal mouse/script hit testing, not the solve hook. Slide before batter so both
        # machine positions and ingredient visibility are exercised on every difficulty.
        click = run(name + "-click", ["--cakes", mode, "--no-tips", "--" + dpi,
                    "--script-click", f"{pan_x+15},315;260,440;{batter_x+15},315;move:750,300",
                    "--shot-delay", "1000", "--width", "800", "--height", "576",
                    "--shot", str(output / (name + "-click.ppm"))])
        assert "station 1 (pan), LAYERS 3 -> added" in click, name
        assert "station 0 (batter), LAYERS 0 -> replaced" in click, name
        assert "station 0 (batter) finished" in click, name
        assert "Layers\\BatterChocolateRoundSprite.xml" in click, name
        assert "there is no cake under it" not in click, name
        print(name + ": layout, solve, normal clicks PASS")

for mode, score in (("Beginner", 410), ("Intermediate", 1180), ("Advanced", 4040)):
    text = run("pairs-" + mode, ["--scene", "UI\\CONCENTRATION\\CONCENTRATIONGAME.XML",
               "--pairs", mode + ",0", "--pairs-solve", "--shot", str(output / ("pairs-" + mode + ".ppm"))])
    assert f"solve: WON, score {score}" in text, mode
    print("pairs-" + mode + ": PASS")

text = run("cakes-menu", ["--menu", "--script-click", "330,250;50,465", "--shot", str(output / "cakes-menu.ppm")])
assert "ComfyCakesButton -> Comfy Cakes" in text
assert "cakes: ReturnToMainMenu -> main menu" in text
print("cakes menu roundtrip: PASS")
text = run("cakes-keys", ["--cakes", "Beginner", "--no-tips", "--no-hidpi",
           "--script-click", "200,315;key:2;key:3;key:1;key:n;key:r",
           "--shot-delay", "1000", "--width", "800", "--height", "576",
           "--shot", str(output / "cakes-keys.ppm")])
assert text.count("cakes: new Beginner game") == 4
assert "cakes: new Intermediate game" in text and "cakes: new Expert game" in text
assert "station 1 (pan) finished" not in text  # reset cancels the old in-flight animation
assert "there is no cake under it" not in text
print("cakes difficulty keys and round restart: PASS")
try:
    from PIL import Image
    for path in output.glob("*.ppm"):
        Image.open(path).save(path.with_suffix(".png"))
except ImportError:
    pass
print("PASS: 23 integration runs")
