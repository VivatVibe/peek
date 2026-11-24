# Peek

![Peek print preview](docs/screenshot.png)

Fast image preview and effortless printing for Omarchy, inspired by macOS Preview.

- **One window, keyboard-first**: drop or open any image (PNG, JPEG, WebP, GIF, BMP, TIFF, SVG), step through sibling images in the same folder, zoom toward the pointer, rotate, flip, and undo without ever modifying originals.
- **Live print proof**: press `Space` or `Ctrl+P` to preview the exact printed paper sheet (A4, US Letter, 4×6, 5×7, A5, A3, Legal), orientation, margins, fit/fill/custom scale, and color/black-and-white before sending to CUPS or saving atomically as a 300 DPI PDF.
- **Omarchy native**: follows your active Omarchy accent color, Hyprland corner rounding, and Omarchy desktop language live.

## Keyboard Shortcuts

| Key | Action |
| --- | --- |
| `Space` / `Ctrl+P` / `P` | Open Print Sheet |
| `Enter` | Print (inside Print Sheet) |
| `Ctrl+S` | Save as PDF |
| `O` / `Ctrl+O` | Open Image |
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
peek export photo.png -o photo.pdf --paper a4 --fit fit --margins standard --json
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
