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

Meta (`M-`) is either **Alt** or the **Esc** prefix — e.g. `M-d` = Alt-D, or Esc then D.

**Files**

| Keys      | Action                                            |
|-----------|---------------------------------------------------|
| `C-x C-f` | Find/open file (inline completion — Tab to accept)|
| `C-x C-s` | Save                                              |
| `C-x C-w` | Write file (save as)                              |
| `C-x C-c` | Quit                                              |

**Movement**

| Keys          | Action                          |
|---------------|---------------------------------|
| `C-f` / `C-b` | Forward / backward char         |
| `C-n` / `C-p` | Next / previous line            |
| `C-a` / `C-e` | Start / end of line             |
| `M-f` / `M-b` | Forward / backward word         |
| `C-v` / `M-v` | Page down / up                  |
| `M-<` / `M->` | Beginning / end of buffer       |
| `C-l`         | Recenter (center → top → bottom)|
| `M-g`         | Go to line                      |

**Editing**

| Keys                  | Action                              |
|-----------------------|-------------------------------------|
| `C-d`                 | Delete char forward                 |
| `C-k`                 | Kill to end of line                 |
| `M-d` / `M-⌫`         | Kill word forward / backward        |
| `C-t`                 | Transpose characters                |
| `C-o`                 | Open line                           |
| `M-u` / `M-l` / `M-c` | Upcase / downcase / capitalize word |
| `C-/`                 | Undo                                |

**Selection & clipboard** (cut/copy/yank sync with the macOS clipboard)

| Keys          | Action                     |
|---------------|----------------------------|
| `C-Space`     | Set mark                   |
| `C-w` / `M-w` | Cut / copy region          |
| `C-y`         | Yank (paste)               |
| `C-x h`       | Select whole buffer        |
| `C-x C-x`     | Exchange point & mark      |

**Search & view**

| Keys          | Action                          |
|---------------|---------------------------------|
| `C-s` / `C-r` | Incremental search fwd / back   |
| `⌘F`          | Search with match highlights    |
| `⌘=` / `⌘-`   | Increase / decrease font size   |
| `Esc`         | Cancel / clear mark             |

Markdown headings, bold, and italic are styled live as you type.

## Layout

```
application/Kern/
  Kern.xcodeproj    the app project (open this)
  Kern/
    App/            SwiftUI shell, bridging header, asset catalog
    Editor/         the C editor: textview, buffer, editing, navigation,
                    undo, md_render, editor_types
    Platform/       SDL/GL renderer + macOS window chrome
    ThirdParty/     microui, stb_truetype, atlas
    Resources/      iA Writer fonts + OFL.txt
  Vendor/SDL2/      vendored SDL2 static lib + headers
```

## License

MIT — see [LICENSE](LICENSE). Includes microui (© rxi), SDL2 (zlib), and
stb_truetype (public domain), each under their own licenses. Typeset in the
[iA Writer](https://github.com/iaolo/iA-Fonts) Quattro & Mono typefaces
(© Information Architects), used under the SIL Open Font License 1.1 — see
[OFL.txt](application/Kern/Kern/Resources/OFL.txt).
