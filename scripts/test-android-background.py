#!/usr/bin/env python3
"""Actual Qt/AAudio callbacks with Home, screen off, media keys and competing focus."""
import json
from pathlib import Path
import subprocess
import time
import sys
MODE = sys.argv[1] if len(sys.argv) > 1 else "qt"
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ("build-android-ui/results/background-" + MODE)
PACKAGE = "org.agbplay.android"

def adb(*args):
    return subprocess.check_output(["adb", *map(str, args)])

def state():
    try:
        return json.loads(adb("exec-out", "run-as", PACKAGE, "cat", "files/background-probe.json"))
    except (subprocess.CalledProcessError, ValueError):
        return None

def wait_for(predicate, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = state()
        if value and predicate(value):
            assert value["engine"] and not value["error"], value
            return value
        time.sleep(0.3)
    raise AssertionError(f"Background audio timeout: {state()}")

def snapshot(name):
    value = state()
    (OUT / f"{name}.json").write_text(json.dumps(value, indent=2))
    (OUT / f"{name}-services.txt").write_bytes(adb("shell", "dumpsys", "activity", "services", PACKAGE))
    (OUT / f"{name}-power.txt").write_bytes(adb("shell", "dumpsys", "power"))
    return value

def active_wake_locks():
    power = adb("shell", "dumpsys", "power").decode()
    assert "Wake Locks:" in power and "Suspend Blockers:" in power, power
    # Android 15 also prints a historical Wake Lock Log, including released
    # locks. Only the active list represents current CPU ownership.
    return power.split("Wake Locks:", 1)[1].split("Suspend Blockers:", 1)[0]

def continuous(name, seconds=12):
    first = wait_for(lambda s: s["playing"] and s["rms"] > 0.00001)
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        time.sleep(1)
        current = state()
        assert current and current["playing"] and not current["error"], current
    last = snapshot(name)
    assert last["sequence"] > first["sequence"] + seconds, (first, last)  # Qt event loop alive
    assert last["frames"] > first["frames"] + seconds * 24000, (first, last)
    assert last["rms"] > 0.00001, last
    assert "agbplay:playback" in active_wake_locks()
    print(f"PASS {name}: frames {first['frames']}..{last['frames']}, RMS={last['rms']}, underruns={last['underruns']}", flush=True)

def media(key):
    adb("shell", "cmd", "media_session", "dispatch", key)

def paused(name):
    wait_for(lambda s: not s["playing"])
    time.sleep(2)  # discard the final in-flight diagnostic period
    first = state()
    time.sleep(2)
    last = snapshot(name)
    assert last["frames"] == first["frames"], (first, last)
    locks = active_wake_locks()
    assert "agbplay:playback" not in locks, locks
    print(f"PASS {name}: paused callback count and released CPU wake lock", flush=True)

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    adb("shell", "am", "force-stop", PACKAGE)
    adb("install", "-r", ROOT / "build-android-ui/artifacts/background-probe-x86_64.apk")
    adb("install", "-r", ROOT / "build-saf-tests/saf-tests.apk")
    adb("shell", "run-as", PACKAGE, "rm", "-f", "files/background-probe.json")
    try:
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "dismiss-keyguard")
        adb("shell", "am", "start", "-W", "-n", f"{PACKAGE}/org.qtproject.qt.android.bindings.QtActivity", "--es", "outputMode", MODE)
        continuous("foreground", 4)
        assert state()["backend"] == MODE, state()
        services = adb("shell", "dumpsys", "activity", "services", PACKAGE).decode()
        assert "PlaybackService" in services and "isForeground=true" in services, services
        adb("shell", "input", "keyevent", "KEYCODE_HOME")
        wait_for(lambda s: s["applicationState"] != 4)
        continuous("home", 30 if MODE == "audiotrack" else 12)
        assert state()["uiTaskRuns"] == 0, state()
        adb("shell", "input", "keyevent", "KEYCODE_SLEEP")
        continuous("screen-off", 60 if MODE == "audiotrack" else 12)
        power = adb("shell", "dumpsys", "power").decode()
        assert "mWakefulness=Asleep" in power or "mWakefulness=Dozing" in power, power
        media("pause")
        paused("media-pause")
        media("play")
        continuous("screen-off-resume", 4)
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "wm", "dismiss-keyguard")
        adb("shell", "am", "start", "-W", "-n", "org.agbplay.saftest/.FocusStealer", "--ez", "transient", "true")
        paused("transient-focus-loss")
        adb("shell", "input", "keyevent", "KEYCODE_BACK")
        continuous("transient-focus-return", 4)
        adb("shell", "am", "start", "-W", "-n", "org.agbplay.saftest/.FocusStealer", "--ez", "transient", "false")
        paused("permanent-focus-loss")
        adb("shell", "input", "keyevent", "KEYCODE_BACK")
        time.sleep(2)
        assert not state()["playing"], state()  # permanent loss must never auto-resume
        media("play")
        continuous("explicit-focus-resume", 4)
        for _ in range(5):
            media("pause")
            media("play")
        wait_for(lambda s: s["playing"] and s["rms"] > 0.00001)
        media("stop")
        paused("media-stop")
        services = adb("shell", "dumpsys", "activity", "services", PACKAGE).decode()
        assert "PlaybackService" not in services, services
        adb("shell", "am", "start", "-W", "-n", f"{PACKAGE}/org.qtproject.qt.android.bindings.QtActivity")
        wait_for(lambda s: s["uiTaskRuns"] == 1 and s["applicationState"] == 4)
        print("PASS deferred UI: control loop runs hidden, UI completion waits for foreground", flush=True)
        time.sleep(1)
        button = state()
        adb("shell", "input", "tap", button["playX"], button["playY"])
        continuous("restart-after-stop", 4)
        def command(value):
            before = state()["sequence"]
            subprocess.run(["adb", "shell", "run-as", PACKAGE, "tee", "files/output-command.pending"], input=value.encode(), stdout=subprocess.DEVNULL, check=True)
            adb("shell", "run-as", PACKAGE, "mv", "files/output-command.pending", "files/output-command.txt")
            wait_for(lambda s: s["sequence"] > before and s["lastCommand"] == value)
        for _ in range(3):
            command("restart")
            wait_for(lambda s: s["playing"] and s["rms"] > 0.00001)
        other = "qt" if MODE == "audiotrack" else "audiotrack"
        command(other)
        wait_for(lambda s: s["backend"] == other)
        continuous("switch-to-" + other, 4)
        command(MODE)
        wait_for(lambda s: s["backend"] == MODE)
        continuous("switch-back-" + MODE, 4)
        command("mute")
        wait_for(lambda s: s["playing"] and s["rms"] < 0.000001)
        command("unmute")
        continuous("track-unmute", 4)
        command("wav")
        wait_for(lambda s: s["diagnosticWav"])
        continuous("diagnostic-wav", 4)
        command("song")
        wait_for(lambda s: not s["diagnosticWav"])
        continuous("return-to-mp2k", 4)
        command("gsf")
        wait_for(lambda s: s["gsf"])
        continuous("gsf-minigsf-song-two", 4)
        media("stop")
        paused("final-stop")
        assert "PlaybackService" not in adb("shell", "dumpsys", "activity", "services", PACKAGE).decode()
        print("PASS background lifecycle: service/notification controls, focus and safe stop", flush=True)
    finally:
        log = adb("logcat", "-d")
        (OUT / "logcat.txt").write_bytes(log)
        if sys.exc_info()[0] is not None:
            print("Android audio failure log:", flush=True)
            print("\n".join(line for line in log.decode(errors="replace").splitlines() if any(tag in line for tag in ("agbplay", "AudioTrack", "AndroidRuntime")))[-18000:], flush=True)
        adb("shell", "input", "keyevent", "KEYCODE_WAKEUP")
        adb("shell", "am", "force-stop", PACKAGE)
        adb("shell", "am", "force-stop", "org.agbplay.saftest")

if __name__ == "__main__":
    main()
