# MicroEdit

A minimalist text editor — a cross between Emacs and iA Writer. Emacs-style
keybindings and editing model, with a clean, distraction-free iA Writer look:
the iA Writer Quattro/Mono typefaces, live markdown styling, and a full-window
writing surface.

Written in C with SDL2 + OpenGL for rendering. Built on
[microui](https://github.com/rxi/microui) (the immediate-mode UI library
provides the window, command list, clipping, and input plumbing) and
[stb_truetype](https://github.com/nothings/stb) for font rasterization.

## Building

### Xcode

```sh
open MicroEdit.xcodeproj
```

Then Build & Run (⌘R). Produces `MicroEdit.app` with the fonts bundled as
resources.

### Command line

```sh
./build.sh
./microedit testdata/test_markdown.txt
```

Requires SDL2 (e.g. `brew install sdl2`). The script assumes Homebrew at
`/opt/homebrew`; adjust the include/library paths in `build.sh` and the
`HEADER_SEARCH_PATHS`/`LIBRARY_SEARCH_PATHS` build settings in the Xcode
project if SDL2 lives elsewhere.

## Usage

```sh
microedit [file]
```

Opens `file`, or an empty buffer if none is given.

### Keybindings (Emacs-style)

| Keys                | Action                          |
|---------------------|---------------------------------|
| `C-x C-f`           | Find/open file (minibuffer)     |
| `C-x C-s`           | Save                            |
| `C-x C-c`           | Quit                            |
| `C-s` / `C-r`       | Incremental search fwd/back     |
| `C-space`           | Set mark                        |
| `C-w` / `M-w`       | Kill / copy region              |
| `C-y`               | Yank                            |
| `M-<` / `M->`       | Beginning / end of buffer       |
| `Esc`               | Prefix / cancel, clear mark     |
| `⌘F`                | Search with match highlights    |

Markdown headings, bold, and italic are styled live as you type.

## Layout

```
src/          editor source (text view, buffer, editing, navigation,
              undo, markdown rendering, SDL/GL renderer, macOS chrome)
vendor/       microui/ and stb/ — vendored dependencies
assets/fonts  iA Writer typefaces + the microui glyph atlas
testdata/     sample documents
tools/        gen_prose.py (test-corpus generator)
```

## License

MIT — see [LICENSE](LICENSE). Includes microui (© rxi), also MIT.
