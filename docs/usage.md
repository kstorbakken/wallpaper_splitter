# User guide

[← README](../README.md) · [Installation](installation.md) · [Slideshow](slideshow.md) ·
[Monitor alignment](monitors.md) · [Command line](command-line.md)

## Open, adjust, and apply

1. Click **Open** to select an image, or drop one into the window.
2. Adjust the image and monitor overlay using the controls below.
3. Click **Export** to save crop files, or **Apply** to save a library set and use
   it on the current Plasma activity.

| Action | Control |
| --- | --- |
| Resize or stretch the image | Drag an image corner handle |
| Keep the image’s proportions while resizing | Hold **Shift** while dragging a handle |
| Move the monitor overlay | Drag it with the left mouse button |
| Resize the monitor overlay | Drag it with the right mouse button |
| Zoom the editor | **Ctrl + mouse wheel** |
| Pan the zoomed view | Drag with the middle mouse button |

Diagonal resizing of the monitor overlay has a known issue; use horizontal or
vertical dragging instead.

## Fit the image

Click the translucent stretch icon in the preview’s upper-right corner
(**Stretch across screens**) to stretch the entire original image across the
combined monitor layout, filling its width and height. This changes the image’s
proportions when its aspect ratio differs from the layout. Monitor positions and
physical bezel gaps are preserved; image content in gaps or outside the panels of
an uneven arrangement remains hidden. You can still adjust the image and layout
afterward. Click the active icon again to restore the original image and the
monitor layout shown when it was first loaded.

The adjacent outward-arrow icon, **Fill panorama**, scales one continuous image
proportionally to fill the complete monitor arrangement. Excess content is cropped
symmetrically at the top/bottom or sides; the image is never repeated or stretched.
Monitor positions and physical bezel gaps stay intact. Both actions start from the
original image. The active icon stays highlighted; click it again to return to
the initially loaded view, or click the other icon to switch modes. Loading a
new image clears the active mode.

## Split a folder

Choose <kbd>Open → Split folder…</kbd> to process every supported image directly
inside a folder. Select **Export** or **Library (save without applying)**, then
choose centered largest fit, stretch, or panorama for each image. Physical monitor
settings, image format, JPEG quality, and the export filename template are reused.

The batch destination, layout, collision choice, and folders are remembered as
defaults for the next batch, including after restarting the app. Each image is
fitted automatically; manual crop positioning from the editor is not copied.

Subfolders are excluded. Export to a different folder to keep source images safe.
Batch exports default to creating revisions, and offer replace or skip on collisions.
Cancel stops before the next image and keeps completed output; the final summary
lists failures and skipped images.

## Try it on the desktop

Click <kbd>Preview</kbd> to temporarily apply the current crop to the desktop.
The editor minimizes while a countdown and **End Preview** button are shown.
The previous wallpaper returns after **15 seconds** by default, when you end the
preview, or when you close the preview dialog. Change **Preview duration** in
<kbd>Settings → General</kbd> (1–300 seconds).

Preview uses temporary crops and
does not add a set to the library or write to your export folder. Temporary files
are removed after Plasma confirms restoration; if restoration fails, the dialog
keeps them and offers another attempt. The application's slideshow cannot switch
wallpapers during a preview. Other activities' wallpapers are left as they were.

## Export settings

Open **Settings → General** to configure:

| Setting | Behavior |
| --- | --- |
| Input and export folders | Remember the folders used to open images and save crops |
| Filename template | Supports `{source}`, `{screen}`, `{number}`, `{revision}`, and `{digest}` |
| Image format and JPEG quality | Choose automatic, PNG, or JPEG output and its quality |
| Existing exports | Ask, replace files, or create a revision |

Automatic format keeps PNG sources lossless, uses high-quality JPEG for other
opaque sources, and always uses PNG when transparency is present. The source image
is never modified.

Uncheck **Close the app after successfully applying a wallpaper set** to keep
the window open for further adjustments after Apply. This preference is remembered;
closing after Apply is enabled by default.

## Library folder

Set **Library folder** in <kbd>Settings → General</kbd> to choose a different
location for managed sets. Apply, the Library, batch saves, and slideshow rotation
all use that location. Existing sets stay in their original folder; switch back
to access them. If you switch to another library, choose its sets in the slideshow
playlist before resuming rotation. **Restore Defaults** returns to the standard
library folder and resets the remembered batch choices and preview duration.

## Saved wallpaper sets

Use <kbd>Library</kbd> to browse saved sets, with a preview, source image path,
creation time, monitor layout, crop mappings, and generated file paths. Reapply
requires the saved monitor positions and resolutions to match the current layout;
the original source image is not needed. Export saves a set's existing crops using
the configured export folder, filename template, and collision handling, without
reopening or recropping the source. Renaming changes the library label.

Deletion removes only managed files and is blocked while Plasma references a set
on any activity, or when those references cannot be checked. Switch those activities
to another wallpaper before deleting. Exports and source images are kept.

To rotate saved sets automatically, see the [slideshow guide](slideshow.md). For
different pixel densities, height offsets, or bezel gaps, see
[physical monitor alignment](monitors.md).

## Files and preferences

Preferences follow the XDG base-directory convention and are stored in
`~/.config/wallpaper-splitter/settings.conf`. Existing preferences from older
versions are migrated automatically. Managed wallpaper sets created by
<kbd>Apply</kbd> or a library batch are stored in `~/.local/share/wallpaper-splitter/sets`
by default, or in your configured library folder.

Exports belong to you. Applied crops and their JSON manifest are kept in the
library so Plasma can keep referencing them after the app closes. Applying a set
uses [Plasma’s D-Bus scripting API](https://develop.kde.org/docs/plasma/scripting/api/).
