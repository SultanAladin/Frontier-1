#!/usr/bin/env python3
"""Refuses a PowerShell script that Windows PowerShell 5 would fail to parse.

🔴 THE FAILURE THIS EXISTS TO CATCH, MEASURED RATHER THAN GUESSED. `Build/Construct.ps1` held 57
   em-dashes. With no UTF-8 BOM, PowerShell 5 decodes the file as the ANSI code page, so the
   em-dash's three UTF-8 bytes E2 80 94 become three cp1252 characters -- and the last of them,
   0x94, IS A CLOSING SMART QUOTE. Inside a double-quoted string it ends the string early:

       throw "$UnitName -- cl.exe rejected the translation batch"
                     becomes
       throw "$UnitName a<euro>" cl.exe rejected the translation batch"
                                 ^ string ended here; `cl.exe` is now a bare token

   which is the reported `Unexpected token 'cl.exe'`. Every later brace is then miscounted, giving
   the cascading "Missing closing '}'" at the end of the file.

⚠️ A BOM ALONE IS NOT A FIX. Any editor that resaves without one re-breaks the build silently. So
   this gate requires BOTH: a BOM, and no smart-quote-producing byte in executable code. Comments
   may hold anything -- PowerShell never parses them for quotes.
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
GOVERNED = ("Build", "Scripts")

# 🔴 UTF-8 lead/continuation bytes that land on a cp1252 QUOTE when misread. 0x93/0x94 are the
#    smart double quotes and 0x91/0x92 the smart singles; those are the ones that close a string.
CLOSERS = {0x91: "'", 0x92: "'", 0x93: '"', 0x94: '"'}


def Faults():
    Found = []

    for Folder in GOVERNED:
        Root = ROOT / Folder
        if not Root.is_dir():
            continue

        for Leaf in sorted(Root.rglob("*")):
            if Leaf.suffix.lower() not in (".ps1", ".bat"):
                continue

            Raw = Leaf.read_bytes()
            Marked = Raw.startswith(b"\xef\xbb\xbf")
            Body = Raw[3:] if Marked else Raw

            if not any(Byte > 127 for Byte in Body):
                continue

            Named = Leaf.relative_to(ROOT).as_posix()

            if not Marked:
                Found.append(f"{Named} carries non-ASCII with no UTF-8 BOM; PowerShell 5 reads it as ANSI")

            # ⚠️ Read the file the way PS5 would if the BOM were ever lost, and refuse if any
            #    executable line would then hold a stray quote.
            for Number, Line in enumerate(Body.split(b"\n"), 1):
                Code = Line.split(b"#", 1)[0]
                for Byte in Code:
                    if Byte in CLOSERS:
                        Found.append(
                            f"{Named}:{Number} has byte 0x{Byte:02X}, a cp1252 {CLOSERS[Byte]} "
                            f"that would close a string early: {Code.decode('ascii', 'replace').strip()[:60]}")
                        break

    return Found


def Main():
    Broken = Faults()

    if Broken:
        for Fault in Broken:
            print(f"[ShellEncoding] {Fault}")
        print(f"[ShellEncoding] refused: {len(Broken)} shell script fault(s)")
        return 1

    print("[ShellEncoding] every governed shell script parses under PowerShell 5")
    return 0


if __name__ == "__main__":
    sys.exit(Main())
