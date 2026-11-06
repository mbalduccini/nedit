"""Run widget and full-editor checks on an isolated Xvfb display."""
import os
import json
import time
import shlex
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
                        "-svrname", "", "-utf8", "-nowrap", "-utf8font", "monospace:size=12",
                        "-xrm", "nedit.appendLF: False",
                        "-do", command, str(path)],
                       cwd=task_dir, env=editor_env, check=True, timeout=30)
        assert path.read_bytes() == original, "UTF-8/NUL/malformed bytes changed on save"
        # Reopen and repeat to cover real application loading after a save.
        subprocess.run([str(tests_dir.parent / "source" / "nedit"),
                        "-svrname", "", "-utf8", "-wrap", "-lm", "C", "-xrm", "nedit.appendLF: False",
                        "-do", command, str(path)],
                       cwd=task_dir, env=editor_env, check=True, timeout=30)
        assert path.read_bytes() == original, "Reopened UTF-8 bytes changed"
        print("Full editor UTF-8 activation, save/reopen, NUL and malformed-byte round trip passed.")


def check_application_controls(env):
    with tempfile.TemporaryDirectory(prefix="utf8-controls-", dir=tests_dir) as temp:
        prefs = Path(temp) / "prefs"
        prefs.mkdir()
        subprocess.run([str(tests_dir / "test_utf8_application"), "-svrname", "", "-utf8"],
                       cwd=temp, env=dict(env, NEDIT_HOME=str(prefs)),
                       check=True, timeout=30)
        (tests_dir / "utf8-search.ppm").write_bytes((Path(temp) / "utf8-search.ppm").read_bytes())
        data = (prefs / "nedit.rc").read_text()
        assert "nedit.utf8: True" in data and "monospace:size=16" in data


def check_phase_one_files(env):
    with tempfile.TemporaryDirectory(prefix="utf8-phase1-", dir=tests_dir) as temp:
        task_dir = Path(temp)
        prefs = task_dir / "prefs"
        prefs.mkdir()
        editor_env = dict(env, NEDIT_HOME=str(prefs), LC_ALL="C.UTF-8")
        binary = str(tests_dir.parent / "source" / "nedit")

        def run(path, command, options=()):
            subprocess.run([binary, "-svrname", "", "-utf8", "-nowrap",
                            "-xrm", "nedit.appendLF: False", *options,
                            "-do", command + "\nsave()\nexit()\n", str(path)],
                           cwd=task_dir, env=editor_env, check=True, timeout=45)

        original = "café é 中 𐐀\r\n".encode() + b"Invalid: \x80; NUL: \0\r\n"
        path = task_dir / "crlf.txt"
        path.write_bytes(original)
        run(path, 'end_of_file()\ninsert_string("é")\ndelete_previous_character()')
        assert path.read_bytes() == original, "CRLF/UTF-8/binary round trip changed"
        included = task_dir / "include.txt"
        included.write_bytes("é 中文\r\n".encode() + b"\x80\0\r\n")
        run(path, 'end_of_file()\ninclude_file(' + json.dumps(str(included)) + ')')
        assert path.read_bytes() == original + included.read_bytes(), "Include-file bytes changed"
        path = task_dir / "editing.txt"
        text = "AéZ\nB中Q".encode()
        path.write_bytes(text)
        run(path, 'select_rectangle(0, $text_length, 1, 2)\nshift_right()\nundo()')
        assert path.read_bytes() == text, "Rectangle shift/undo changed bytes"
        path.write_bytes("éé 中中 aa bb".encode())
        run(path, 'select_all()\nfill_paragraph()', ["-xrm", "nedit.wrapMargin: 5"])
        assert path.read_bytes() == "éé\n中中\naa bb".encode(), "Fill ignored Unicode columns"
        path.write_bytes("aa \u0301 bb cc".encode())
        run(path, 'select_all()\nfill_paragraph()', ["-xrm", "nedit.wrapMargin: 3"])
        assert path.read_bytes() == "aa \u0301\nbb\ncc".encode(), "Fill split a combining sequence"
        path.write_bytes("int main() {\n  // café 中\n}".encode())
        run(path, 'set_auto_indent("smart")\nend_of_file()\nnewline()\nundo()', ["-lm", "C"])
        assert path.read_bytes() == "int main() {\n  // café 中\n}".encode(), "Smart-indent round trip failed"
        path.write_bytes("Aé中".encode())
        run(path, 'set_encoding("8bit")\nset_encoding("utf8")\nset_utf8_font("DejaVu Sans:size=14")\nsplit_pane()\nend_of_file()\ndelete_previous_character()\nundo()')
        assert path.read_bytes() == "Aé中".encode(), "Encoding/font/split-view switch changed bytes"

        # Autosave uses Unix line endings and adds a newline by established
        # NEdit policy. Read it from inside the macro before save removes it.
        path = task_dir / "autosave.txt"
        path.write_bytes("é 中\n".encode() + b"\x80\0\n")
        copied = task_dir / "backup-copy.txt"
        backup = path.with_name("~" + path.name)
        command = 'set_incremental_backup(1)\nend_of_file()\n'
        command += 'insert_string("𐐀")\n' * 12
        command += 'shell_command(' + json.dumps('cp ' + shlex.quote(str(backup)) + ' ' + shlex.quote(str(copied))) + ', "")\n'
        run(path, command)
        restored = copied.read_bytes()
        assert restored.startswith("é 中\n".encode() + b"\x80\0\n"), "Backup lost original bytes"
        assert restored.endswith(b"\n") and "𐐀".encode() in restored, "Backup lost edit"

        path = task_dir / "large.txt"
        data = ("café é 中文 𐐀\n".encode() * 100000)
        path.write_bytes(data)
        began = time.monotonic()
        run(path, 'end_of_file()\nbackward_character()\nforward_character()\ninsert_string("é")\ndelete_previous_character()')
        assert path.read_bytes() == data, "Large-file round trip failed"
        print(f"{len(data)/1000000:.1f} MB UTF-8 open/edit/save: {time.monotonic()-began:.2f} seconds.")
        print("Phase 1 CRLF/include, rectangle shift, fill, smart-indent, encoding/font/split-view and autosave checks passed.")


def check_phase_two(env):
    with tempfile.TemporaryDirectory(prefix="utf8-phase2-", dir=tests_dir) as temp:
        task_dir=Path(temp)
        prefs=task_dir / "prefs"
        prefs.mkdir()
        editor_env=dict(env,NEDIT_HOME=str(prefs))
        binary=str(tests_dir.parent / "source" / "nedit")
        path=task_dir / "macros.txt"
        checks=[
            ('length("é中")',5),
            ('unicode_length("é中é𐐀")',5),
            ('unicode_grapheme_length("é中é𐐀")',4),
            ('unicode_substring("é中𐐀",1,2)',"中"),
            ('unicode_substring("é中𐐀",-1)',"𐐀"),
            ('unicode_next("é中",0)',2),
            ('unicode_next("é中",1)',2),
            ('unicode_next("é中",5)',5),
            ('unicode_previous("é中",5)',2),
            ('unicode_previous("é中",0)',0),
            ('unicode_next_grapheme("é中",0)',3),
            ('unicode_previous_grapheme("é中",3)',0),
            ('unicode_previous_grapheme("é中",0)',0),
            ('unicode_toupper("éβ𐐨")',"ÉΒ𐐀"),
            ('unicode_tolower("ÉΒ𐐀")',"éβ𐐨"),
            ('unicode_casefold("Straße Σς ﬃ")',"strasse σσ ffi"),
            ('search_string("é中",".",0,"regex")',0),
            ('search_string("é中","中",1,"case","utf8")',2),
            ('search_string("Straße","STRASSE",0,"utf8","literal")',0),
            ('search_string("中β","β",0,"utf8","caseWord")',-1),
            ('replace_in_string("é中","(.)","<\\\\1>","regex","utf8")',"<é><中>"),
            ('replace_in_string("Straße","STRASSE","yes","utf8","literal")',"yes"),
        ]
        command='report = ""\n'
        for expression,expected in checks:
            expected=json.dumps(expected,ensure_ascii=False)
            command+=f'if ({expression} != {expected}) report = report + "FAIL: " + {json.dumps(expression,ensure_ascii=False)} + "\\n"\n'
        command+='p = search_string("é中", ".", 0, "regex")\n'
        command+='if ($search_end != 1) report = report + "legacy regex offsets\\n"\n'
        command+='p = search_string("é中", ".", 0, "utf8", "regex")\n'
        command+='if ($search_end != 2) report = report + "UTF-8 regex offsets\\n"\n'
        command+='p = search(".", 0, "utf8", "regex")\n'
        command+='if (p != 0 || $search_end != 2) report = report + "document search offsets\\n"\n'
        command+='parts = split("é中β", ".", "utf8", "regex")\n'
        command+='if (parts[] != 4) report = report + "Unicode split\\n"\n'
        command+='string_to_clipboard("é中β")\n'
        command+='if (clipboard_to_string() != "é中β") report = report + "macro clipboard\\n"\n'
        command+='select_all()\nreplace_selection(report)\nsave()\nexit()\n'
        path.write_text("é中")
        subprocess.run([binary,"-svrname","","-utf8","-nowrap","-xrm","nedit.appendLF: False",
                        "-do",command,str(path)],cwd=task_dir,env=editor_env,check=True,timeout=30)
        assert path.read_bytes()==b"",path.read_text()
        # Repeated Unicode matches exercise the bulk subject maps.
        data=("é中β\n"*20000).encode()
        path.write_bytes(data)
        began=time.monotonic()
        subprocess.run([binary,"-svrname","","-utf8","-nowrap","-xrm","nedit.appendLF: False",
                        "-do",'value = replace_in_string(get_range(0,$text_length), "é", "É", "utf8", "case")\nselect_all()\nreplace_selection(value)\nsave()\nexit()\n',str(path)],
                       cwd=task_dir,env=editor_env,check=True,timeout=30)
        assert path.read_bytes()==("É中β\n"*20000).encode(),"Bulk Unicode literal replacement failed"
        print(f"20,000 Unicode replacements: {time.monotonic()-began:.2f} seconds.")
        # Shell filters exchange original document bytes; UTF-8-aware tools opt
        # in themselves. No editor-side transcoding or normalization is applied.
        path.write_bytes("é β 中\n".encode()+b"\x80\n")
        original=path.read_bytes()
        subprocess.run([binary,"-svrname","","-utf8","-nowrap","-xrm","nedit.appendLF: False",
                        "-do",'select_all()\nfilter_selection("cat")\nsave()\nexit()\n',str(path)],
                       cwd=task_dir,env=editor_env,check=True,timeout=30)
        assert path.read_bytes()==original,"Shell filter changed UTF-8/malformed bytes"
        # Include actual syntax matches throughout a large buffer, which catches
        # decoding the complete document separately for each highlighting match.
        data="αβ 中中 𐐀\n".encode()*60000
        path.write_bytes(data)
        patterns='C:1:0{Greek:"[α-ω]+":::Keyword::\nCJK:"中+":::String::\n}'
        began=time.monotonic()
        subprocess.run([binary,"-svrname","","-utf8","-nowrap","-lm","C",
                        "-xrm","nedit.appendLF: False","-xrm","nedit.highlightPatterns: "+patterns.replace("\n", "\\n"),
                        "-do",'end_of_file()\ninsert_string("β")\ndelete_previous_character()\nsave()\nexit()\n',str(path)],
                       cwd=task_dir,env=editor_env,check=True,timeout=30)
        assert path.read_bytes()==data,"Large highlighted file changed"
        print(f"{len(data)/1000000:.1f} MB Unicode syntax open/edit/save: {time.monotonic()-began:.2f} seconds.")
        print("Unicode macro APIs, legacy byte APIs, search/split flags, clipboard and shell-filter round trips passed.")


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
        env = dict(os.environ, DISPLAY=f":{display_number}", LC_ALL="C.UTF-8")
        subprocess.run([str(tests_dir / "test_utf8_widget")],
                       cwd=tests_dir, env=env, check=True, timeout=30)
        check_application_controls(env)
        check_editor(env)
        check_phase_one_files(env)
        check_phase_two(env)
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        server.stdout.close()
