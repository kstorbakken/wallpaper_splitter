# Command line

[← README](../README.md) · [User guide](usage.md)

Run commands from a KDE Plasma Wayland session so the app can detect the connected
monitors. Use `wallpaper_splitter --help` for all options.

## Export crops

Passing an image starts the command-line exporter. Without `--destination`,
files are written to an `<image>_split` directory beside the source image.

```sh
wallpaper_splitter --destination ~/Pictures/spanned wallpaper.jpg
wallpaper_splitter --filename-template '{source}-{screen}' \
  --collision revision wallpaper.jpg
wallpaper_splitter --format png --destination ~/Pictures/lossless wallpaper.jpg
```

`--format` accepts `automatic`, `jpeg`, or `png`. Use `--jpeg-quality` with a value
from 1 to 100 to override the saved JPEG quality.

## Apply to the desktop

Use `--apply` to write a managed set and apply it to the current Plasma
activity. Export-only options cannot be combined with `--apply`.

```sh
wallpaper_splitter --apply wallpaper.jpg
```

## Automatic layouts

Add `--stretch-across-screens` to stretch the entire source image across the combined
monitor layout, including when physical sizing is enabled:

```sh
wallpaper_splitter --stretch-across-screens --destination ~/Pictures/spanned wallpaper.jpg
wallpaper_splitter --stretch-across-screens --apply wallpaper.jpg
```

Use `--fill-panorama` for a continuous, proportional panorama instead.
The two automatic layout options cannot be combined with each other or with
`--top-left` or `--bottom-right`. Without either option,
the existing command-line positioning and sizing behavior is unchanged.

## Advance a slideshow

Advance a saved Library slideshow without opening a window:

```sh
wallpaper_splitter --slideshow-next
```

This works even when automatic rotation is off. It cannot be combined with an
input image or export/apply options. The timer uses `--slideshow-tick`, which does
nothing when automatic rotation is disabled. Both commands exit after one attempt.

See the [slideshow guide](slideshow.md) for playlist setup and troubleshooting.
