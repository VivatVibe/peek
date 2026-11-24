---
name: peek
description: >
  Preview images, export print-ready PDFs, and print images via CUPS using
  the peek desktop app and headless CLI.
---

# Peek

`peek` is an Omarchy image viewer and print utility with headless CLI support.

## Commands

- `peek [open] <image ...>` — Open image(s) in the desktop viewer.
- `peek printers --json` — List configured CUPS printers and the default destination.
- `peek export <image> -o <out.pdf> [options] --json` — Render an image to a print-ready PDF at 300 DPI.
- `peek print <image> [--printer <name>] [--copies <n>] [options] --json` — Render and send an image directly to CUPS.

## Layout options

- `--paper a4|letter|4x6|5x7|a5|a3|legal` (default: `a4`)
- `--orientation auto|portrait|landscape` (default: `auto`)
- `--fit fit|fill|scale` (default: `fit`)
- `--scale <10-200>` (sets `--fit scale` at the given percentage)
- `--margins standard|narrow|borderless` (default: `standard`, 10 mm)
- `--grayscale` (render in black & white)
- `--rotate 0|90|180|270` (clockwise degrees)
