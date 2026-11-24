# Peek

![Peek print preview](docs/screenshot.png)

Fast image & PDF preview, quick editing, and effortless printing for Omarchy — inspired by macOS Preview.

- **One window, keyboard-first**: drop or open any image (PNG, JPEG, WebP, GIF, BMP, TIFF, SVG) or **PDF document**, browse thumbnails in the sidebar (`Tab`), multi-select images or pages for batch printing, zoom toward the pointer, and undo/redo without ever modifying originals.
- **PDF Companion (`poppler-qt6`)**:
  - Open single or multi-page PDFs with automatic thumbnail sidebar, crisp 150 DPI viewing, and 300 DPI print/export output.
  - **Organize & Merge**: reorder pages (`Ctrl+↑` / `Ctrl+↓`), remove pages (`Delete`), merge additional PDFs or images (`+ Merge` / `Ctrl+Shift+M`), annotate or rotate individual pages, and save the organized document via `Ctrl+S` — never modifying source files.
  - **Page Ranges**: print or export `All`, `Current`, `Selected`, or custom ranges (`1-3, 5`) with live multi-page preview.
  - **PDF Inspector (`I`)**: displays page count, physical page size (mm), and PDF document metadata (Title, Author, Creator, Producer).
- **Quick Edit & Export (non-destructive)**:
  - **Crop & Copy (`S` / `Ctrl+K` / `Ctrl+C`)**: draw a selection with optional aspect-ratio presets (`Free`, `1:1`, `4:3`, `3:2`, `16:9`, `A4`), crop cleanly, or copy image/selection to the clipboard.
  - **Adjust Size (`Ctrl+Shift+R`)**: resize by pixels or percent with proportional lock and quick presets (`50%`, `75%`, `1920`, `1280`, `Original`).
  - **Adjust Color (`Ctrl+Shift+C`)**: live floating HUD for Exposure, Contrast, Saturation, Brightness, plus one-click **Auto Levels**.
  - **Markup Toolbar (`M`)**: draw rectangles, ovals, arrows, lines, freehand sketches, text callouts placed under the pointer, pixelation mosaics, and solid redaction bars — painted by one shared C++ renderer so screen, clipboard, exported images, and printed PDFs match identically.
  - **Image & EXIF Inspector (`I`)**: floating HUD showing dimensions, megapixels, 300 DPI print size, file size, and camera EXIF metadata (camera, lens, shutter, aperture, ISO, focal length).
  - **Export & Convert (`E` / `Ctrl+Shift+S`)**: convert to JPEG, PNG, WebP, TIFF, BMP, or PDF with quality slider and live estimated file size; saves atomically and asks before discarding unexported edits on quit (`Q`).
- **macOS Preview–style print sheet (`Space` / `Ctrl+P`)**:
  - Live paper proof painted by the exact C++ renderer used for PDF/CUPS output.
  - **Presets & Custom Paper Sizes**: automatically remembers your last print settings and preferred printer across launches (resetting Copies to 1 for safety, like macOS), lets you create custom paper dimensions (`mm`, `cm`, `in`) via **Manage Custom Sizes…**, ships built-in presets, and saves your own named presets in `~/.config/peek/presets.json`.
  - **Images per sheet (`1`, `2`, `4`, `6`, `9`, `12`, `16`)** with optional dashed **cut guides** — repeat the same image across a sheet or pack multiple selected images across sheets with multi-page preview.
  - **Print Selection**: print only the drawn region without cropping the file.
  - **CUPS Printer Options**: automatically reads Media Type, Print Quality, and Two-Sided options via `lpoptions`.
- **Omarchy native**: follows your active Omarchy accent color, Hyprland corner rounding, and Omarchy desktop language live.

## Keyboard Shortcuts

| Key | Action |
| --- | --- |
| `Space` / `Ctrl+P` / `P` | Open Print Sheet |
| `Enter` | Print (inside Print Sheet) |
| `PgUp` / `PgDn` | Previous / Next Page (inside Print Sheet) |
| `Ctrl+S` | Save as PDF |
| `E` / `Ctrl+Shift+S` | Export / Convert Format |
| `O` / `Ctrl+O` | Open Image or PDF |
| `Tab` | Toggle Thumbnail Sidebar |
| `Ctrl+↑` / `Ctrl+↓` | Move Page Up / Down |
| `Ctrl+Shift+M` / `Del` | Merge PDF or Images / Remove Page |
| `S` / `Shift+Drag` | Select Region to Crop or Print |
| `M` | Toggle Markup Toolbar |
| `I` | Toggle Inspector (EXIF / PDF) |
| `Ctrl+K` | Crop to Selection |
| `Ctrl+C` | Copy Image or Selection |
| `Ctrl+Shift+R` | Adjust Size (Resize) |
| `Ctrl+Shift+C` | Adjust Color |
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
| `Q` | Quit (asks before discarding unexported edits) |

## Headless CLI

Peek can also be driven without a display:

```sh
peek printers --json
peek presets --json
peek inspect document.pdf --json
peek export photo.png -o edited.jpg --crop 100,100,800,600 --resize 640x480 --quality 85 --auto-levels --json
peek export report.pdf appendix.pdf --pages 1-3,5 -o merged.pdf --json
peek export photo.png -o sheet.pdf --preset "4-Up Sheet with Cut Guides" --json
peek print report.pdf --pages 1-2 --preset "Black & White Draft" --copies 2 --json
peek skill install
```

## Install

### Download Prebuilt Package (Omarchy / Arch Linux `x86_64`)

Download the latest `.pkg.tar.zst` from [GitHub Releases](https://github.com/VivatVibe/peek/releases/latest) and install it with `pacman`:

```sh
sudo pacman -U peek-0.6.0-1-x86_64.pkg.tar.zst
```

### Build from Source

```sh
git clone https://github.com/VivatVibe/peek.git
cd peek
./bin/build
./bin/test
./bin/install
```

## License

MIT — see [LICENSE](LICENSE).
