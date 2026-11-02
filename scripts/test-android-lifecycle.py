#!/usr/bin/env python3
"""Real Android pause/expiry, Qt menus, SAF cancellation and task removal.

Expiry is accelerated by executing the actual timeout callback in the test APK.
Use --real-expiry to wait the real five-minute deadline instead. No test control
or reflection helper is packaged in the production APK.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "org.agbplay.android"
ACTIVITY = f"{PACKAGE}/org.agbplay.android.AgbplayActivity"
OUT = ROOT / "build-android-ui/results/lifecycle"
spec = importlib.util.spec_from_file_location("background", Path(__file__).with_name("test-android-background.py"))
bg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bg)
bg.MODE = "audiotrack"
bg.OUT = OUT


def adb(*args):
    return subprocess.check_output(["adb", *map(str, args)], timeout=30)


bg.adb = adb


def wait_until(predicate, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.3)
    raise AssertionError(f"Lifecycle timeout; state={bg.state()}")


def service_present():
    return "PlaybackService" in adb("shell", "dumpsys", "activity", "services", PACKAGE).decode()


def pid():
    try:
        return adb("shell", "pidof", PACKAGE).decode().strip()
    except subprocess.CalledProcessError:
        return ""


def command(value):
    before = bg.state()["sequence"]
    subprocess.run(["adb", "shell", "run-as", PACKAGE, "tee", "files/output-command.pending"],
                   input=value.encode(), stdout=subprocess.DEVNULL, check=True, timeout=30)
    adb("shell", "run-as", PACKAGE, "mv", "files/output-command.pending", "files/output-command.txt")
    if value != "remove-task":
        bg.wait_for(lambda s: s["sequence"] > before and s["lastCommand"] == value)


def foreground():
    return bg.wait_for(lambda s: s["applicationState"] == 4 and s["updatesEnabled"]
                       and s["exposed"] and s["inputFocus"] and not s["dialog"])


def tap(point):
    adb("shell", "input", "tap", point["x"], point["y"])


def action(text):
    state = bg.wait_for(lambda s: any(a["text"] == text for a in s["menuActions"]))
    tap(next(a for a in state["menuActions"] if a["text"] == text))


def capture(name):
    (OUT / f"{name}.png").write_bytes(adb("exec-out", "screencap", "-p"))
    (OUT / f"{name}.json").write_text(json.dumps(bg.state(), indent=2))


def menus_and_saf(name):
    tap(foreground()["menu"])
    action("Edit")
    action("Global Preferences")
    settings = bg.wait_for(lambda s: "Preferences" in s["dialog"] and "dialogCancel" in s)
    capture(name + "-preferences")
    tap(settings["dialogCancel"])
    tap(foreground()["open"])
    # Observe the actual external Android picker, not a QFileDialog stub.
    wait_until(lambda: any("documentsui" in line.lower() for line in
        adb("shell", "dumpsys", "activity", "activities").decode().splitlines()
        if "ResumedActivity" in line))
    capture(name + "-saf")
    adb("shell", "input", "keyevent", "KEYCODE_BACK")
    foreground()
    capture(name + "-returned")
    print(f"PASS {name}: actual menu/preferences and SAF open/cancel, responsive foreground", flush=True)


def launch():
    adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb("shell", "wm", "dismiss-keyguard")
    adb("shell", "run-as", PACKAGE, "rm", "-f", "files/background-probe.json")
    adb("shell", "am", "start", "-W", "-n", ACTIVITY, "--es", "outputMode", "audiotrack")
    foreground()
    bg.continuous("lifecycle-foreground", 4)


def expire(real):
    bg.media("pause")
    bg.paused("paused-before-expiry")
    if real:
        deadline = time.monotonic() + 310
        while time.monotonic() < deadline:
            time.sleep(1)
            assert not bg.state()["playing"], bg.state()
    else:
        command("expire-service")
    wait_until(lambda: not service_present())
    command("stale-service-callbacks")
    bg.paused("retired-service-callbacks")
    assert not service_present()
    print("PASS paused service expiry and retired callbacks: engine remains paused, no resurrected service", flush=True)


def remove_and_relaunch(name):
    command("dirty-profile")  # never wait for an invisible Save/Cancel dialog
    previous = pid()
    adb("shell", "input", "keyevent", "KEYCODE_HOME")
    bg.wait_for(lambda s: s["applicationState"] != 4)
    command("remove-task")  # AppTask.finishAndRemoveTask on the Android main thread
    wait_until(lambda: not pid(), timeout=20)
    assert not service_present()
    assert "agbplay:playback" not in bg.active_wake_locks()
    print(f"PASS {name}: task removed, process/service/audio wake lock released", flush=True)
    launch()
    assert pid() != previous
    menus_and_saf(name + "-relaunched")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--screen-off-seconds", type=int, default=30)
    parser.add_argument("--real-expiry", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    adb("shell", "am", "force-stop", PACKAGE)
    adb("install", "-r", ROOT / "build-android-ui/artifacts/background-probe-x86_64.apk")
    try:
        adb("shell", "wm", "size", "1600x1200")
        adb("shell", "wm", "density", "160")
        launch()
        adb("shell", "input", "keyevent", "KEYCODE_SLEEP")
        bg.continuous("lifecycle-screen-off", args.screen_off_seconds)
        expire(args.real_expiry)
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "dismiss-keyguard")
        adb("shell", "am", "start", "-W", "-n", ACTIVITY)
        for cycle in range(3):
            menus_and_saf(f"pause-expired-{cycle}")
        state = foreground()
        adb("shell", "input", "tap", state["playX"], state["playY"])
        bg.continuous("restart-after-service-expiry", 4)
        command("stale-service-callbacks")
        bg.continuous("stale-callbacks-new-session", 4)
        remove_and_relaunch("playing-dirty-profile")
        adb("shell", "input", "keyevent", "KEYCODE_HOME")
        expire(False)
        remove_and_relaunch("paused-expired-dirty-profile")
    finally:
        (OUT / "logcat.txt").write_bytes(adb("logcat", "-d"))
        (OUT / "crash.txt").write_bytes(adb("logcat", "-d", "-b", "crash"))
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "size", "reset")
        adb("shell", "wm", "density", "reset")
        adb("shell", "am", "force-stop", PACKAGE)


if __name__ == "__main__":
    main()
