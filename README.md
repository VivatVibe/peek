# Peek

![Peek print preview](docs/screenshot.png)

Fast image preview and effortless printing for Omarchy, inspired by macOS Preview.

- **One window, keyboard-first**: drop or open any image (PNG, JPEG, WebP, GIF, BMP, TIFF, SVG), browse thumbnails in the sidebar (`Tab`), multi-select images for batch printing, zoom toward the pointer, rotate, flip, and undo without ever modifying originals.
- **macOS Preview–style print sheet (`Space` / `Ctrl+P`)**:
  - Live paper proof painted by the exact C++ renderer used for PDF/CUPS output.
  - **Images per sheet (`1`, `2`, `4`, `6`, `9`)** with optional dashed **cut guides** — repeat the same image across a sheet (great for ID photos/stickers) or pack multiple selected images across sheets with multi-page preview.
  - **Print Selection (`S` or `Shift+Drag`)**: draw a rectangle on the image and print only that region non-destructively.
  - **CUPS Printer Options**: automatically reads Media Type, Print Quality, and Two-Sided options from your printer via `lpoptions`.
  - Atomic 300 DPI **Save as PDF** (`Ctrl+S`).
- **Omarchy native**: follows your active Omarchy accent color, Hyprland corner rounding, and Omarchy desktop language live.

## Keyboard Shortcuts

| Key | Action |
| --- | --- |
| `Space` / `Ctrl+P` / `P` | Open Print Sheet |
| `Enter` | Print (inside Print Sheet) |
| `PgUp` / `PgDn` | Previous / Next Page (inside Print Sheet) |
| `Ctrl+S` | Save as PDF |
| `O` / `Ctrl+O` | Open Image |
| `Tab` | Toggle Thumbnail Sidebar |
| `S` / `Shift+Drag` | Select Region to Print |
| `Ctrl+A` | Select All Images |
| `←` / `→` (`H` / `L`) | Previous / Next Image in folder |
| `+` / `−` | Zoom In / Out |
| `0` / `F` | Fit to Window |
| `1` | Actual Size (100%) |
| `[` / `]` (`Ctrl+L` / `Ctrl+R`) | Rotate Left / Right |
| `Shift+H` / `Shift+V` | Flip Horizontal / Vertical |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / Redo |
| `T` | Toggle Transparency Grid |
| `?` | Toggle Shortcuts Help |
| `Q` | Quit |

## Headless CLI

Peek can also be driven without a display:

```sh
peek printers --json
peek export photo.png -o sheet.pdf --paper a4 --per-page 4 --cut-guides --json
peek export a.png b.png c.png d.png -o contact.pdf --paper a4 --per-page 4 --json
peek print photo.png --copies 2 --paper 4x6 --fit fill --margins borderless --json
peek skill install
```

## Build, Test, and Install

```sh
./bin/build
./bin/test
./bin/install
```

## License

MIT — see [LICENSE](LICENSE).
