#!/usr/bin/env python3
"""Checks that tau++ behaves exactly like the Python edition (tau.py from the Tau repository).

usage: python3 tests/run_tests.py [path-to-tau++] [path-to-Tau-repository]
defaults: build/tau++  and  $TAU_REPO_DIR or ../Tau
"""
import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
default_cpp = os.path.join(HERE, "build", "tau++" + (".exe" if os.name == "nt" else ""))
CPP = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else default_cpp
TAU_REPO = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.environ.get("TAU_REPO_DIR", os.path.join(HERE, "..", "Tau")))
TAU_PY = os.path.join(TAU_REPO, "tau.py")
SCRIPTS = os.path.join(HERE, "ide", "scripts")
SAMPLE = os.path.join(HERE, "tests", "sample.tau")

for what, path in (("tau++ executable", CPP), ("tau.py", TAU_PY)):
    if not os.path.exists(path):
        sys.exit(f"cannot find {what} at {path}\n(build tau++ first, and check out the Tau repository next to this one)")

failures = 0


def run(cmd, stdin=b""):
    p = subprocess.run(cmd, input=stdin, capture_output=True)
    return p.returncode, p.stdout, p.stderr


def check(name, args, expect_stdout=None, stdin=b""):
    """Run both implementations; they must agree on exit code, stdout and stderr."""
    global failures
    py = run([sys.executable, "-I", TAU_PY] + args, stdin)
    cpp = run([CPP] + args, stdin)
    ok = py == cpp
    if expect_stdout is not None and py[1].decode("latin-1") != expect_stdout:
        ok = False
    print(("ok    " if ok else "FAIL  ") + name)
    if not ok:
        failures += 1
        print("   python:", py)
        print("   tau++ :", cpp)


def rel(p):
    return os.path.relpath(p, TAU_REPO).replace(os.sep, "/")


# ---- every program in the Tau repository ----------------------------------------
programs = [os.path.join(TAU_REPO, "hello.tau")]
for folder in ("examples", "programs", "tools"):
    programs += sorted(glob.glob(os.path.join(TAU_REPO, folder, "*.tau")))
text_inputs = sorted(glob.glob(os.path.join(TAU_REPO, "tests", "*.txt")))

for prog in programs:
    if os.path.basename(prog) in ("textstats.tau", "wc.tau"):
        continue  # these need a document, tested below
    check(rel(prog), [prog])

for prog in ("tools/textstats.tau", "programs/wc.tau"):
    for txt in text_inputs:
        check(f"{prog} on {rel(txt)}", [os.path.join(TAU_REPO, prog), "--text", txt])
sample = open(text_inputs[0], "rb").read() if text_inputs else b""
check("tools/textstats.tau reading stdin", [os.path.join(TAU_REPO, "tools", "textstats.tau"), "--text", "-"], stdin=sample)

# ---- the two scripts that tauIDE is made of -------------------------------------
check("highlight.tau on tests/sample.tau", [os.path.join(SCRIPTS, "highlight.tau"), "--text", SAMPLE])
for prog in programs:
    check("highlight.tau on " + rel(prog), [os.path.join(SCRIPTS, "highlight.tau"), "--text", prog])
check("status.tau on tests/sample.tau", [os.path.join(SCRIPTS, "status.tau"), "--text", SAMPLE, "--caret", "20"],
      "Ln 2, Col 14   |   8 lines, 112 chars")

# ---- errors must read the same in both ------------------------------------------
cases = {
    "undefined label": ("Jump nowhere\n", []),
    "unknown statement": ("Frobnicate r0\n", []),
    "data too long": ('Text t 1 "ab"\n', []),
    "bad binary group": ('Define Tau 1 "0101"\n', []),
    "division by zero": ("Set r0 1\nDiv r0 0\n", []),
    "step limit": ("again:\nJump again\n", ["--steps", "100"]),
    "no cell": ("Print 7\n", []),
    "ret without call": ("Ret\n", []),
    "host without a host": ("Host TextLen r0\n", []),
    "arithmetic": ("Set r0 -7\nDiv r0 2\nPrintInt r0\nNewline\nSet r1 -7\nMod r1 2\nPrintInt r1\n"
                   "Newline\nSet r2 0x7FFFFFFF\nMul r2 3\nPrintInt r2\nNewline\n", []),
}
with tempfile.TemporaryDirectory() as d:
    for name, (source, extra) in cases.items():
        path = os.path.join(d, "t.tau")
        with open(path, "w") as f:
            f.write(source)
        check("same result: " + name, [path] + extra)

print("\nall good" if not failures else f"\n{failures} failure(s)")
sys.exit(1 if failures else 0)
