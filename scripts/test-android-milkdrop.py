#!/usr/bin/env python3
"""Native GLES projectM, actual orientation, audio and background suspension.

Uses the existing legal MP2K/GSF fixture APK and its real output backends.
No test control is added to production. Emulator measurements are not a device
GPU benchmark and do not certify 60 FPS on the user's hardware.
"""
import importlib.util
import json
import re
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build-android-ui/results/milkdrop"
PACKAGE = "org.agbplay.android"
ACTIVITY = f"{PACKAGE}/org.agbplay.android.AgbplayActivity"
spec = importlib.util.spec_from_file_location("background", Path(__file__).with_name("test-android-background.py"))
bg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bg)


def adb(*args):
    return subprocess.check_output(["adb", *map(str, args)], timeout=30)


bg.adb = adb


def command(value):
    before = bg.state()["sequence"]
    subprocess.run(["adb", "shell", "run-as", PACKAGE, "tee", "files/output-command.pending"],
                   input=value.encode(), stdout=subprocess.DEVNULL, check=True, timeout=30)
    adb("shell", "run-as", PACKAGE, "mv", "files/output-command.pending", "files/output-command.txt")
    if value == "remove-task":
        return None
    return bg.wait_for(lambda s: s["sequence"] > before and s["lastCommand"] == value)


def visual():
    return bg.wait_for(lambda s: s["milkdrop"]["running"] and s["milkdrop"]["frames"] > 5
                       and s["milkdrop"]["pcmFrames"] > 0 and not s["milkdrop"]["failed"])


def resources(name):
    state = bg.state()
    (bg.OUT / f"{name}-meminfo.txt").write_bytes(adb("shell", "dumpsys", "meminfo", PACKAGE))
    raw = adb("shell", "run-as", PACKAGE, "cat", f"/proc/{int(state['pid'])}/stat")
    (bg.OUT / f"{name}-process-stat.txt").write_bytes(raw)
    fields = raw.decode().rsplit(")", 1)[1].split()
    (bg.OUT / f"{name}-resources.json").write_text(json.dumps({
        "hostMonotonicSeconds": time.monotonic(), "userTicks": int(fields[11]),
        "systemTicks": int(fields[12]), "threads": int(fields[17]), "rssPages": int(fields[21])}, indent=2))


def record(name):
    state = bg.snapshot(name)
    d = state["milkdrop"]
    assert abs(d["width"] - round(d["logicalWidth"] * d["dpr"])) <= 1, state
    assert abs(d["height"] - round(d["logicalHeight"] * d["dpr"])) <= 1, state
    assert d["viewportWidth"] == d["width"] and d["viewportHeight"] == d["height"], state
    assert d["eglSurfaceSize"], "Actual Android EGL surface size was not measured"
    assert d["presetErrors"] == 0 and not d["failed"], state
    (bg.OUT / f"{name}.png").write_bytes(adb("exec-out", "screencap", "-p"))
    resources(name)
    return state


def remove_fullscreen_task():
    adb("shell", "am", "start", "-W", "-n", ACTIVITY)
    bg.wait_for(lambda s: s["applicationState"] == 4)
    command("milkdrop")
    point = bg.state()["milkdrop"]["fullscreenButton"]
    assert point["visible"]
    adb("shell", "input", "tap", point["x"], point["y"])
    bg.wait_for(lambda s: s["milkdrop"]["fullscreen"] and s["requestedOrientation"] == 0)
    visual()
    command("dirty-profile")
    previous = str(int(bg.state()["pid"]))
    command("remove-task")
    deadline = time.monotonic() + 25
    while time.monotonic() < deadline:
        try:
            process = adb("shell", "pidof", PACKAGE).decode().strip()
        except subprocess.CalledProcessError:
            process = ""
        if not process:
            break
        time.sleep(0.3)
    assert not process, "Fullscreen window prevented graceful process shutdown"
    history = adb("shell", "dumpsys", "activity", "exit-info", PACKAGE).decode()
    (bg.OUT / "fullscreen-task-exit.txt").write_text(history)
    entry = re.search(rf"\bpid={previous}\b(.*?)(?=\bApplicationExitInfo\b|\Z)", history, re.S)
    assert entry and re.search(r"\breason=1\b", entry[0]) and re.search(r"\bstatus=0\b", entry[0]), history
    log = adb("logcat", "-d", "--pid", previous).decode(errors="replace")
    (bg.OUT / "fullscreen-task-removal.log").write_text(log)
    assert "Fatal signal" not in log and "FATAL EXCEPTION" not in log, log[-5000:]
    assert "PlaybackService" not in adb("shell", "dumpsys", "activity", "services", PACKAGE).decode()
    adb("shell", "run-as", PACKAGE, "rm", "-f", "files/background-probe.json")
    adb("shell", "am", "start", "-W", "-n", ACTIVITY)
    bg.startup_ready()
    assert str(int(bg.state()["pid"])) != previous
    command("milkdrop")
    visual()
    record("fullscreen-task-relaunched")
    print("PASS fullscreen task removal with dirty profile: clean exit and fresh GL/PCM after relaunch", flush=True)


def exercise(mode, size, width, height, density):
    bg.MODE = mode
    bg.OUT = OUT / f"{size}-{mode}"
    bg.OUT.mkdir(parents=True, exist_ok=True)
    adb("shell", "am", "force-stop", PACKAGE)
    adb("shell", "wm", "size", f"{width}x{height}")
    adb("shell", "wm", "density", density)
    adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb("shell", "wm", "dismiss-keyguard")
    adb("shell", "run-as", PACKAGE, "rm", "-f", "files/background-probe.json")
    adb("shell", "am", "start", "-W", "-n", ACTIVITY, "--es", "outputMode", mode)
    bg.startup_ready()
    if bg.state()["backend"] != mode:
        command(mode)
    bg.wait_for(lambda s: s["backend"] == mode and s["playing"] and s["rms"] > 0.00001)
    resources("baseline-start")
    bg.continuous("baseline-hidden-visualizer", 4)
    resources("baseline-hidden-visualizer")
    original = bg.state()
    command("milkdrop")
    visual()
    for preset in range(3):
        bg.continuous(f"preset-{preset}", 3)
        record(f"preset-{preset}")
        command("milkdrop-next")
    point = bg.state()["milkdrop"]["fullscreenButton"]
    assert point["visible"]
    adb("shell", "input", "tap", point["x"], point["y"])
    bg.wait_for(lambda s: s["milkdrop"]["fullscreen"] and s["requestedOrientation"] == 0
                and s["deviceOrientation"] == 2 and s["milkdrop"]["width"] > s["milkdrop"]["height"])
    visual()
    bg.continuous("fullscreen-landscape", 6)
    record("fullscreen-landscape")
    adb("shell", "input", "keyevent", "KEYCODE_SLEEP")
    bg.wait_for(lambda s: not s["milkdrop"]["running"])
    first = bg.state()["milkdrop"]["frames"]
    bg.continuous("screen-off-audio-only", 5)
    assert bg.state()["milkdrop"]["frames"] == first
    adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb("shell", "wm", "dismiss-keyguard")
    visual()
    assert not bg.state()["milkdrop"]["exitButton"]["visible"]
    adb("shell", "input", "keyevent", "KEYCODE_BACK")
    bg.wait_for(lambda s: not s["milkdrop"]["fullscreen"]
                and s["requestedOrientation"] == original["requestedOrientation"]
                and s["deviceOrientation"] == original["deviceOrientation"])
    visual()
    bg.continuous("restored-inline", 3)
    record("restored-inline")
    command("gsf")
    command("milkdrop")
    visual()
    bg.continuous("visualizer-gsf", 3)
    adb("shell", "input", "keyevent", "KEYCODE_HOME")
    bg.wait_for(lambda s: not s["milkdrop"]["running"])
    first = bg.state()["milkdrop"]["frames"]
    bg.continuous("home-audio-only", 3)
    assert bg.state()["milkdrop"]["frames"] == first
    if size == "tablet" and mode == "audiotrack":
        remove_fullscreen_task()
    (bg.OUT / "logcat.txt").write_bytes(adb("logcat", "-d"))
    print(f"PASS MilkDrop {size}/{mode}: native framebuffer, three presets, forced landscape/restoration, GSF, screen off/Home audio", flush=True)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    # Dismiss Android's one-time immersive-mode education in this emulator.
    # Otherwise it covers the renderer and intercepts the exit-button tap.
    adb("shell", "settings", "put", "secure", "immersive_mode_confirmations", "confirmed")
    adb("install", "-r", ROOT / "build-android-ui/artifacts/background-probe-x86_64.apk")
    try:
        for size, width, height, density in (("phone", 576, 1280, 240), ("tablet", 2560, 1600, 240)):
            for mode in ("qt", "audiotrack"):
                exercise(mode, size, width, height, density)
    finally:
        (OUT / "final-state.json").write_text(json.dumps(bg.state(), indent=2))
        (OUT / "logcat.txt").write_bytes(adb("logcat", "-d"))
        (OUT / "crash.txt").write_bytes(adb("logcat", "-d", "-b", "crash"))
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "am", "force-stop", PACKAGE)
        adb("shell", "wm", "size", "reset")
        adb("shell", "wm", "density", "reset")


if __name__ == "__main__":
    main()
