---
name: peek
description: >
  Preview images and PDFs, organize/merge PDF pages, crop/resize/color-adjust/convert
  formats, export print-ready PDFs, and print via CUPS using the peek desktop app
  and headless CLI.
---

# Peek

`peek` is an Omarchy image & PDF viewer, quick editor, and print utility with headless CLI support.

## Commands

- `peek [open] <file ...>` — Open image(s) or PDF document(s) in the desktop viewer.
- `peek printers --json` — List configured CUPS printers and the default destination.
- `peek presets --json` — List built-in and saved print presets.
- `peek inspect <file> [--json]` — Show image dimensions, 300 DPI print size, and EXIF metadata, or PDF page count, physical page size, and document info.
- `peek export <file ...> -o <out> [options] --json` — Export edited image (PNG/JPEG/WebP/TIFF/BMP) or merge/slice/export print-ready 300 DPI PDF.
- `peek print <file ...> [--printer <name>] [--preset <name>] [--copies <n>] [options] --json` — Render and send image(s) or PDF pages directly to CUPS.

## Edit & export options

- `--format png|jpg|webp|tiff|bmp|pdf` (inferred from `-o` extension if omitted)
- `--quality <10-100>` (default: `90` for JPEG/WebP)
- `--pages <range>` (1-based page selection, e.g. `1-3,5`)
- `--crop <x,y,w,h>` (crop rectangle in pixels)
- `--resize <WxH>` (resize target in pixels)
- `--rotate 0|90|180|270` (clockwise degrees)
- `--exposure <-1.0..1.0>`, `--contrast <-1.0..1.0>`, `--saturation <-1.0..1.0>`, `--brightness <-1.0..1.0>`
- `--auto-levels` (auto-balance contrast and brightness)

## Print layout options

- `--paper a4|letter|4x6|5x7|a5|a3|legal|<W>x<H>mm|<custom-name>` (default: `a4`)
- `--orientation auto|portrait|landscape` (default: `auto`)
- `--fit fit|fill|scale` (default: `fit`)
- `--scale <10-200>`
- `--margins standard|narrow|borderless` (default: `standard`)
- `--per-page 1|2|4|6|9|12|16`
- `--cut-guides`
- `--grayscale`
