# Tau++

Tau++ is the **C++ implementation of the Tau programming language**, plus **tauIDE**, a small
Windows IDE that is written half in C++ and half in Tau itself. The language and its Python reference
implementation live in the **Tau** repository; this one has the same language (v0.2) running natively.

Released under the MIT License (see `LICENSE`).

## What is in here

| Path | What it is |
|---|---|
| `tauvm.hpp`, `tauvm.cpp` | The tau++ interpreter, a single class with no dependencies beyond the C++17 standard library |
| `tauxx.cpp` | The command-line program `tau++` |
| `ide/main.cpp` | tauIDE's window: editor, output pane, status bar, menus, file handling (Win32 API) |
| `ide/scripts/highlight.tau` | tauIDE's syntax highlighter, **written in Tau** |
| `ide/scripts/status.tau` | The text of tauIDE's status bar, **written in Tau** |
| `tests/` | Tests that compare tau++ with the Python implementation |

## Running programs

```
tau++ program.tau [--text file|-] [--caret N] [--steps N]
```

See the Tau repository for the language reference. `--text file` (or `-` for standard input) gives the program a
document to read through `Host` functions, `--caret N` sets the caret position, `--steps N` raises the step limit.

```
tau++ tools/textstats.tau --text notes.txt       # text analyser from the Tau repository
```

## tauIDE

A dark editor with live syntax highlighting, an output pane and a status bar.

| Key | Action |
|---|---|
| Ctrl+N / Ctrl+O / Ctrl+S | New, open, save |
| **F5** | Run the program in the editor; the output appears in the bottom pane |
| Ctrl+R | Reload the Tau scripts in `scripts\` without restarting |

**How it is split.** The C++ side owns everything the operating system is needed for: the window, files, running
programs. Everything about *what the editor shows* is a Tau program that the C++ side runs whenever the text
changes. They talk through `Host` functions:

| Host call | Meaning |
|---|---|
| `Host TextLen rD` | Number of characters in the editor |
| `Host CharAt rD <idx>` | Character code at `idx` (non-ASCII characters appear as `?`) |
| `Host CaretLine rD` / `Host CaretCol rD` | Caret position |
| `Host LineCount rD` | Number of lines |
| `Host Paint <start> <len> <colour>` | Colour a range of text |

Colours for `Paint`: 1 keyword, 2 comment, 3 string or character, 4 number, 5 label, 6 register (0 is the default
colour). They are defined in the `PALETTE` table in `ide/main.cpp`.

`highlight.tau` paints the text. `status.tau` prints the status bar text with `Print` / `PrintInt`. Edit either file
and press Ctrl+R to see the change immediately. Programs run with F5 get the same `Host` functions, so a Tau
program in the editor can analyse the editor's own text (try `programs/wc.tau` or `tools/textstats.tau` from the Tau
repository).

## Building

The scripts folder must sit next to `tauIDE.exe`; the build scripts copy it for you.

**Windows with MinGW-w64** (for example from MSYS2), from this folder:

```
build.bat
```

**Linux or macOS with mingw-w64** (cross-compiling the Windows programs):

```
sh build_cross.sh
```

**MSVC:**

```
cl /std:c++17 /EHsc /O2 ide\main.cpp tauvm.cpp user32.lib comctl32.lib comdlg32.lib gdi32.lib /Fe:build\tauIDE.exe /link /SUBSYSTEM:WINDOWS
```

**Command-line `tau++` only** (any platform with a C++17 compiler):

```
make
```

## Tests and continuous integration

`tests/run_tests.py` runs every example and tool from the Tau repository through both implementations and checks
that standard output, error messages and exit codes are identical. It also runs the two IDE scripts on sample text.
