# Logo and images

| File | Use |
|---|---|
| `rocket-fc-logo.svg` | The logo card (text converted to outlines: no fonts needed). Scales to any size. |
| `rocket-fc-logo.png` | The same, 2001 × 684, transparent corners. Used by `compose.py`. |
| `rocket-fc-mark.svg` / `.png` | The icon alone (square), e.g. for an avatar. |
| `social-preview.png` | 1280 × 640, for GitHub: Settings → General → Social preview. |
| `rocket-fc-banner.png` | The logo on the CAD render, full size. |
| `background-render.png` | The CAD render used as the background. |

## Logo on another background

`compose.py` puts the logo in the centre of any image, with a frosted-glass
card and a soft shadow. It needs Python and Pillow (`pip install pillow`).

```
python assets/brand/compose.py photo.jpg                                   # same size as photo.jpg
python assets/brand/compose.py render.png --size 1280x640 --fit contain    # social preview, whole render
python assets/brand/compose.py photo.jpg --size 1280x640 --focus 0.5,0.3   # fill and crop, keep the upper part
python assets/brand/compose.py photo.jpg --logo-width 0.35 --offset 0,150  # smaller, lower
```

Options: `-o` output file, `--size WxH`, `--fit cover|contain`, `--focus x,y`
(crop centre as fractions), `--logo-width` (fraction of the image width,
default 0.46), `--offset dx,dy` (pixels), `--blur` (0 = plain glass),
`--no-shadow`.

Colours: white `#ffffff`, orange `#ff8a3d`, grey `#a9b0bb`, trail gradient
`#ff6a2b` → `#ffd23d` → `#3d6bff`, card `#161618` at 72 %. Fonts (outlined in
the SVG): Inter Display ExtraBold, DejaVu Sans Mono.
