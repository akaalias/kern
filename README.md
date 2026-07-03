# Kern

A minimalist text editor — a cross between Emacs and iA Writer. Emacs-style
keybindings and editing model, with a clean, distraction-free iA Writer look:
the iA Writer Quattro/Mono typefaces, live markdown styling, and a full-window
writing surface.

A macOS app: a thin SwiftUI shell launches the editor, which is written in C
with SDL2 + OpenGL for rendering and
[stb_truetype](https://github.com/nothings/stb) for font rasterization. There's
no UI toolkit — the editor draws everything immediately through a small
12-function renderer interface (`renderer.h`) over a ~30-line geometry/color
header (`gfx.h`). SDL2 is statically linked from a vendored library, so the
`.app` is self-contained (no Homebrew dependency) and runs with Hardened
Runtime + App Sandbox.

## Building

```sh
open application/Kern/Kern.xcodeproj
```

Then Build & Run (⌘R). Produces a self-contained `Kern.app` with the fonts
bundled as resources. Apple Silicon only.

## Usage

On launch it opens today's daily note (`YYYY-MM-DD.md`, seeded with a date
heading). The buffer auto-saves every few seconds while modified. Files live in
the app's sandbox container:

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
| `C-x b`   | Switch buffer (recent files — Tab to list/cycle)  |
| `C-x C-c` | Quit                                              |

**Movement**

| Keys          | Action                          |
|---------------|---------------------------------|
| `C-f` / `C-b` | Forward / backward char         |
| `C-n` / `C-p` | Next / previous line (by visual row) |
| `C-a` / `C-e` | Start / end of line             |
| `M-f` / `M-b` | Forward / backward word         |
| `C-v` / `M-v` | Page down / up                  |
| `M-<` / `M->` | Beginning / end of buffer       |
| `C-l`         | Recenter (center → top → bottom)|
| `C-x t`       | Typewriter mode (pin active line at golden ratio) |
| `C-x y`       | Syntax highlight (parts of speech, by brightness) |
| `C-x s`       | Style check (strike cuttable fillers/redundancies) |
| `C-x l`       | Symbols on/off (draw `->` `--` `!=` `(c)` quotes `lambda` `forall` as → — ≠ © “ ” λ ∀; on by default) |
| `C-x p`       | Page borders on/off (hide/show the page rails, gutters & note margins) |
| `M-g`         | Go to line                      |

**Editing**

| Keys                  | Action                              |
|-----------------------|-------------------------------------|
| `C-d`                 | Delete char forward                 |
| `C-k`                 | Kill to end of line                 |
| `M-d` / `M-⌫`         | Kill word forward / backward        |
| `C-t`                 | Transpose characters                |
| `C-o`                 | Open line                           |
| `Tab` / `Shift-Tab`   | Indent / outdent list item          |
| `M-u` / `M-l` / `M-c` | Upcase / downcase / capitalize word |
| `C-/`                 | Undo                                |

**Selection & clipboard** (cut/copy/yank sync with the macOS clipboard)

| Keys          | Action                     |
|---------------|----------------------------|
| `C-Space`         | Set mark                   |
| `Backspace` / `Delete` | Delete the selected region |
| `C-w` / `M-w`     | Cut / copy region          |
| `⌘C`              | Copy region                |
| `C-y` / `⌘V`      | Yank (paste)               |
| `C-x h`           | Select whole buffer        |
| `C-x C-x`         | Exchange point & mark      |
| `**` `*` `==` `++` `` ` `` | With a region marked, wrap it: bold / italic / highlight / underline / code |
| `Cmd-Shift-H`     | Toggle `==highlight==` around the caret's sentence |
| `Cmd-Shift-U`     | Toggle `++underline++` around the caret's sentence |

**Search & view**

| Keys          | Action                          |
|---------------|---------------------------------|
| `C-s` / `C-r` | Incremental search fwd / back   |
| `⌘=` / `⌘-`   | Increase / decrease font size   |
| `Esc`         | Cancel / clear mark             |

Markdown headings, bold, italic, highlight (`==`), and underline (`++text++`)
are styled live as you type. Each document also grows a read-only **Context**
section at the bottom listing backlinks and notes created the same day.

**Notes & wikilinks**

| Keys                          | Action                                            |
|-------------------------------|---------------------------------------------------|
| `[[`                          | Wikilink autocomplete (↑/↓ to pick, Tab/Enter to accept) |
| `Cmd-Enter`                   | Follow the `[[wikilink]]` under the cursor        |
| `Cmd-Shift-←` / `Cmd-Shift-→` | Back / forward through note history               |
| `Cmd-Shift-N`                 | Extract the selected region into a new linked note|
| `Cmd-Shift-T`                 | Open (or create) today's daily note               |

## Publishing to X (Twitter)

When an X account is connected, a **paper-plane button** appears in the top-right
of the title bar (next to the help and folder buttons). Click it to post the note
you're viewing — or the marked region, if one is active — straight to your X
timeline. The result shows up both as a macOS notification (`Posted to X ✓` / a
failure banner) and in the editor's status bar. The button is hidden until you
link an account.

**One-time setup**

1. **Create an X app** at [console.x.com](https://console.x.com) (pay-per-use;
   ~$0.015/post). In *User authentication settings*:
   - **App permissions:** Read and write
   - **Type of App:** Native App (public client — PKCE, no secret)
   - **Callback URI:** `http://127.0.0.1:8123/callback`
2. **Add your Client ID locally.** Copy the OAuth 2.0 **Client ID** from the
   portal into a gitignored config file:
   ```sh
   cp application/Kern/Config/Secrets.example.xcconfig \
      application/Kern/Config/Secrets.xcconfig
   # then paste your id into X_CLIENT_ID
   ```
   It's injected at build time via `Info.plist`, so it never enters the repo. A
   clone without this file still builds; the X feature just reports
   "not configured".
3. **Connect.** Build & run, open **Settings (⌘,) → X**, click **Connect X
   Account**, and approve in your browser. Tokens are stored in the macOS
   Keychain and auto-refreshed, so you only do this once. (The panel reads state
   when opened — use **Refresh** if it looks stale.)

**Behavior**

- ≤ 280 characters → a normal post.
- \> 280 characters → sent as one long-form post (requires X Premium). Note that
  X's API support for >280 is inconsistent even for Premium; if it's rejected
  you'll see `X: Your Tweet text is too long` and nothing is posted.

**How it works.** OAuth 2.0 with PKCE via a short-lived loopback redirect
listener (RFC 8252), all on a background thread — `editor_main` parks the main
thread in the SDL event loop, so the Swift networking layer must stay off the
main actor and report through the C status bar.

## Layout

```
application/Kern/
  Kern.xcodeproj    the app project (open this)
  Info.plist        app metadata; injects X_CLIENT_ID from the xcconfig
  Config/           Base.xcconfig + Secrets.example.xcconfig (Secrets.xcconfig
                    is local-only / gitignored)
  Kern/
    App/            SwiftUI shell + X publishing, bridging header, asset catalog
    Editor/         the C editor: textview, buffer, editing, navigation,
                    undo, md_render, commands, recent, clipboard, clock,
                    gfx, editor_types
    Platform/       SDL/GL renderer + macOS window chrome
    ThirdParty/     stb_truetype
    Resources/      iA Writer fonts + OFL.txt
  Vendor/SDL2/      vendored SDL2 static lib + headers
```

## License

MIT — see [LICENSE](LICENSE). Includes SDL2 (zlib) and stb_truetype (public
domain), each under their own licenses. Typeset in the
[iA Writer](https://github.com/iaolo/iA-Fonts) Quattro & Mono typefaces
(© Information Architects), used under the SIL Open Font License 1.1 — see
[OFL.txt](application/Kern/Kern/Resources/OFL.txt).
