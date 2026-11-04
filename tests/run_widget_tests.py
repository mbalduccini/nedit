"""Run widget and full-editor checks on an isolated Xvfb display."""
import os
from pathlib import Path
import selectors
import subprocess
import tempfile


def check_editor(env):
    with tempfile.TemporaryDirectory(prefix="utf8-editor-", dir=tests_dir) as temp:
        task_dir = Path(temp)
        prefs = task_dir / "prefs"
        prefs.mkdir()
        editor_env = dict(env, NEDIT_HOME=str(prefs))
        original = "Latin: café; Greek: αβ; CJK: 中文\n".encode("utf-8")
        original += b"Invalid: \x80\xe2\x82; NUL: \0\n"
        original += "尾𐐀".encode("utf-8")
        path = task_dir / "roundtrip.txt"
        path.write_bytes(original)
        command = 'end_of_file()\nbackward_character()\ndelete_next_character()\ninsert_string("𐐀")\nsave()\nexit()\n'
        subprocess.run([str(tests_dir.parent / "source" / "nedit"),
                        "-utf8", "-nowrap", "-utf8font", "monospace:size=12",
                        "-xrm", "nedit.appendLF: False",
                        "-do", command, str(path)],
                       cwd=task_dir, env=editor_env, check=True, timeout=30)
        assert path.read_bytes() == original, "UTF-8/NUL/malformed bytes changed on save"
        # Reopen and repeat to cover real application loading after a save.
        subprocess.run([str(tests_dir.parent / "source" / "nedit"),
                        "-utf8", "-wrap", "-lm", "C", "-xrm", "nedit.appendLF: False",
                        "-do", command, str(path)],
                       cwd=task_dir, env=editor_env, check=True, timeout=30)
        assert path.read_bytes() == original, "Reopened UTF-8 bytes changed"
        print("Full editor UTF-8 activation, save/reopen, NUL and malformed-byte round trip passed.")


tests_dir = Path(__file__).resolve().parent
with tempfile.TemporaryFile(mode="w+") as log:
    server = subprocess.Popen(
        ["Xvfb", "-displayfd", "1", "-nolisten", "tcp",
         "-screen", "0", "1024x768x24"],
        stdout=subprocess.PIPE, stderr=log, text=True,
    )
    try:
        with selectors.DefaultSelector() as ready:
            ready.register(server.stdout, selectors.EVENT_READ)
            if not ready.select(timeout=15):
                raise RuntimeError("Xvfb did not start within 15 seconds")
        display_number = server.stdout.readline().strip()
        if not display_number.isdecimal():
            log.seek(0)
            raise RuntimeError(f"Xvfb failed to start: {log.read()}")
        env = dict(os.environ, DISPLAY=f":{display_number}")
        subprocess.run([str(tests_dir / "test_utf8_widget")],
                       cwd=tests_dir, env=env, check=True, timeout=30)
        check_editor(env)
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        server.stdout.close()
