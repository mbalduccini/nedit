"""Check client startup/routing on Xvfb without using the live editor/display."""
import ctypes
import ctypes.util
import json
import os
from pathlib import Path
import selectors
import subprocess
import tempfile
import time


repo = Path(__file__).resolve().parent.parent
binary = repo / "source/nedit"
launcher = repo / "source/nc-utf8"
x11 = ctypes.CDLL(ctypes.util.find_library("X11"))
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x11.XDefaultRootWindow.restype = ctypes.c_ulong
x11.XListProperties.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                              ctypes.POINTER(ctypes.c_int)]
x11.XListProperties.restype = ctypes.POINTER(ctypes.c_ulong)
x11.XGetAtomName.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
x11.XGetAtomName.restype = ctypes.c_void_p
x11.XFree.argtypes = [ctypes.c_void_p]
x11.XCloseDisplay.argtypes = [ctypes.c_void_p]


def properties(display):
    count = ctypes.c_int()
    atoms = x11.XListProperties(display, x11.XDefaultRootWindow(display),
                               ctypes.byref(count))
    names = set()
    for i in range(count.value):
        name = x11.XGetAtomName(display, atoms[i])
        names.add(ctypes.string_at(name).decode())
        x11.XFree(name)
    if atoms:
        x11.XFree(atoms)
    return {name for name in names if name.startswith("NEDIT_SERVER_EXISTS_")}


def wait_for(test):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        result = test()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError("Client/server test timed out")


def editor_processes(display_name):
    found = {}
    for proc in Path("/proc").iterdir():
        if not proc.name.isdecimal():
            continue
        try:
            if (proc / "exe").resolve() != binary:
                continue
            environment = (proc / "environ").read_bytes().split(b"\0")
            if ("DISPLAY=" + display_name).encode() not in environment:
                continue
            found[int(proc.name)] = (proc / "cmdline").read_bytes().split(b"\0")
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue
    return found


with tempfile.TemporaryDirectory(prefix="nc-utf8-", dir=repo / "tests") as temp:
    task = Path(temp)
    prefs = task / "prefs"
    prefs.mkdir()
    # Match the requested installation, including resolving a symlink from PATH.
    test_bin = task / "bin"
    test_bin.mkdir()
    (test_bin / "nc").symlink_to(launcher)
    # A PATH lookup for nedit must fail: startup must use the workspace binary.
    decoy = test_bin / "nedit"
    decoy.write_text("#!/bin/sh\nexit 97\n")
    decoy.chmod(0o755)
    with (task / "server.log").open("w+") as log:
        xvfb = subprocess.Popen(["Xvfb", "-displayfd", "1", "-nolisten", "tcp",
                                 "-screen", "0", "1024x768x24"],
                                stdout=subprocess.PIPE, stderr=log, text=True)
        display = None
        default = None
        display_name = None
        try:
            with selectors.DefaultSelector() as ready:
                ready.register(xvfb.stdout, selectors.EVENT_READ)
                assert ready.select(15), "Xvfb did not start"
            number = xvfb.stdout.readline().strip()
            assert number.isdecimal(), "Xvfb did not return a display number"
            display_name = ":" + number
            env = dict(os.environ, DISPLAY=display_name, NEDIT_HOME=str(prefs),
                       LC_ALL="C.UTF-8", PATH=str(test_bin) + ":" + os.environ["PATH"])
            display = x11.XOpenDisplay(display_name.encode())
            assert display
            # Exercise coexistence with a normal unnamed, legacy-mode server.
            default = subprocess.Popen([str(binary), "-server", "-svrname", "",
                                        "-8bit", "-nowrap"], cwd=task, env=env,
                                       stdout=log, stderr=log)
            default_properties = wait_for(lambda: properties(display))
            assert len(default_properties) == 1
            sample = task / "sample.txt"
            sample.write_text("é中👩‍💻")
            report = task / "report.txt"
            macro = "beginning_of_file()\nforward_character()\n"
            macro += "write_file($cursor, " + json.dumps(str(report)) + ")"
            subprocess.run(["nc", "-do", macro, str(sample)], cwd=task, env=env,
                           stdout=log, stderr=log, check=True, timeout=20)
            wait_for(report.exists)
            assert report.read_text() == "3", "Server did not enable UTF-8 grapheme editing"
            names = properties(display)
            assert default_properties <= names and len(names) == 2, names
            assert any(name.endswith("_classic-UTF-8") for name in names), names
            processes = editor_processes(display_name)
            assert default.pid in processes and len(processes) == 2, processes
            utf8_pid = next(pid for pid in processes if pid != default.pid)
            args = processes[utf8_pid]
            assert b"-utf8" in args and b"-server" in args
            assert b"classic-UTF-8" in args and b"monospace:size=12" in args
            # A second request must reuse the same server, leaving the default alone.
            report.unlink()
            subprocess.run(["nc", "-do", macro, str(sample)], cwd=task, env=env,
                           stdout=log, stderr=log, check=True, timeout=20)
            wait_for(report.exists)
            assert report.read_text() == "3"
            assert editor_processes(display_name) == processes
            assert properties(display) == names
            print("Symlink/PATH client starts the workspace UTF-8 server, isolates it "
                  "from the default server, and reuses it on subsequent requests.")
        finally:
            if display_name:
                for pid in editor_processes(display_name):
                    try:
                        os.kill(pid, 15)
                    except ProcessLookupError:
                        pass
            if default:
                default.wait(timeout=5)
            if display:
                x11.XCloseDisplay(display)
            xvfb.terminate()
            xvfb.wait(timeout=5)
            xvfb.stdout.close()
