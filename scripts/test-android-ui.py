#!/usr/bin/env python3
"""Send actual Android input, not QTest events that bypass native overlays."""
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build-android-ui" / "results"
PACKAGE = "org.agbplay.android"


def adb(*args):
    return subprocess.check_output(["adb", *map(str, args)])


def state():
    try:
        return json.loads(adb("exec-out", "run-as", PACKAGE, "cat", "files/ui-probe.json"))
    except (subprocess.CalledProcessError, ValueError):
        return None


def wait_for(predicate, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = state()
        if value and predicate(value):
            return value
        time.sleep(0.2)
    raise AssertionError(f"Android UI timeout, last state: {state()}")


def capture(name):
    (OUT / f"{name}.png").write_bytes(adb("exec-out", "screencap", "-p"))
    (OUT / f"{name}.json").write_text(json.dumps(state(), indent=2))


def test(name, width, height, density, font_scale):
    adb("shell", "am", "force-stop", PACKAGE)
    adb("shell", "wm", "size", f"{width}x{height}")
    adb("shell", "wm", "density", density)
    adb("shell", "settings", "put", "system", "font_scale", font_scale)
    adb("shell", "run-as", PACKAGE, "rm", "-f", "files/ui-probe.json")
    adb("shell", "am", "start", "-W", "-n", f"{PACKAGE}/org.qtproject.qt.android.bindings.QtActivity")
    current = wait_for(lambda s: s["windowWidth"] > 100 and s["windowHeight"] > 100)
    time.sleep(1)  # Allow native bars/insets to settle before real input.
    current = state()
    capture(name)
    assert current["qt"] == "6.11.2", current
    wait_for(lambda s: s["actionBarChecked"])
    assert not state()["actionBarPresent"], state()
    assert not state()["nativeMenuBar"], state()
    for control, count in (("open", "openRequests"), ("menu", "menuRequests")):
        current = state()
        button = current[control]
        assert button["visible"] and button["contained"], current
        assert button["width"] >= 48 and button["height"] >= 48, current
        before = current[count]
        adb("shell", "input", "tap", button["x"], button["y"])
        wait_for(lambda s: s[count] > before, timeout=5)
        capture(f"{name}-{control}-clicked")
    adb("shell", "input", "keyevent", "KEYCODE_BACK")
    print(f"PASS {name}: actual Android taps reached Open ROM/GSF and Menu", flush=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    adb("install", "-r", ROOT / "build-android-ui/artifacts/ui-probe-x86_64.apk")
    try:
        test("phone", 576, 1280, 240, 1.0)
        test("phone-large-font", 576, 1280, 240, 1.3)
        test("phone-landscape", 1280, 576, 240, 1.0)
        test("tablet", 1600, 1200, 160, 1.0)
    finally:
        (OUT / "logcat.txt").write_bytes(adb("logcat", "-d"))
        adb("shell", "wm", "size", "reset")
        adb("shell", "wm", "density", "reset")
        adb("shell", "settings", "put", "system", "font_scale", "1.0")


if __name__ == "__main__":
    main()
