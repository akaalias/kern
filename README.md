# Kern

A minimalist text editor — a cross between Emacs and iA Writer. Emacs-style
keybindings and editing model, with a clean, distraction-free iA Writer look:
the iA Writer Quattro/Mono typefaces, live markdown styling, and a full-window
writing surface.

A macOS app: a thin SwiftUI shell launches the editor, which is written in C
with SDL2 + OpenGL for rendering. Built on
[microui](https://github.com/rxi/microui) (the immediate-mode UI library
provides the window, command list, clipping, and input plumbing) and
[stb_truetype](https://github.com/nothings/stb) for font rasterization. SDL2 is
statically linked from a vendored library, so the `.app` is self-contained
(no Homebrew dependency) and runs with Hardened Runtime + App Sandbox.

## Building

```sh
open application/Kern/Kern.xcodeproj
```

Then Build & Run (⌘R). Produces a self-contained `Kern.app` with the fonts
bundled as resources. Apple Silicon only.

## Usage

Launches into an empty buffer. Files live in the app's sandbox container:

```
~/Library/Containers/com.rondeau.Kern/Data/Documents/
```

Open/save paths resolve relative to that folder (reach it in Finder via
**Go → Go to Folder**).

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
application/Kern/
  Kern.xcodeproj         the app project (open this)
  Kern/                  Swift app shell + the C editor sources:
                                KernApp.swift, bridging header,
                                textview.c, buffer/editing/navigation/undo,
                                md_render, renderer, macos_style, microui,
                                stb_truetype, fonts
  Vendor/SDL2/                vendored SDL2 static lib + headers
```

## License

MIT — see [LICENSE](LICENSE). Includes microui (© rxi) and SDL2 (zlib), each
under their own licenses.
