# Physical monitor alignment

[← User guide](usage.md) · [README](../README.md)

Open **Settings → Monitors** and enable **Use physical monitor sizes** when displays
have different pixel densities, uneven heights, or bezel gaps. Enter each panel’s
diagonal in inches, or its visible width and height in millimeters.
Reported dimensions are starting values;
verify them against your displays. If dimensions are unavailable, the dialog
starts with a 24-inch estimate.

Adjust **Left** and **Top** in millimeters to match the panels' real positions,
including bezel gaps. Positive top offsets move a panel down. Initial positions
follow the desktop arrangement; **Arrange left to right, top aligned** gives a
simple side-by-side starting point. The dialog previews the physical arrangement.

## Example: same size, different resolutions

For two **27-inch, 16:9** monitors, one **1920 × 1080** and one **3840 × 2160**:

1. Enable **Use physical monitor sizes** and enter **27** in both Diagonal fields.
   Both panels should have the same width and height. Equal diagonals only imply
   equal dimensions when the aspect ratios match; use measured visible dimensions
   if needed, excluding the frame.
2. Click **Arrange left to right, top aligned** for a side-by-side starting point.
3. Adjust **Left** and **Top** to match the actual positions, as described below.
4. Click **OK**, review the crop overlay, and apply the wallpaper.

Both monitors now cover equally sized areas of the wallpaper. An object crossing
the seam keeps the same physical size even though one display has more pixels.
You do not need to change either monitor's resolution or desktop scaling.

## Example: bezel gaps and height differences

Positions share a common origin; use **Left = 0, Top = 0** for the left panel.
If its visible width is **600 mm** and the distance from its visible right edge to
the next panel's visible left edge is **12 mm**, set the next panel's **Left = 612**.
Include both bezels and any space between them in that gap. The matching strip of
wallpaper is hidden so lines continue naturally across it.

If the right panel sits **10 mm higher**, set its **Top = -10**; if it sits
**10 mm lower**, use **Top = 10**. This works for monitors with identical sizes
and resolutions too. A wallpaper with a clear line crossing the screens can help
you check alignment and fine-tune the measurements.

These instructions are also available through **How to line up monitors** in
the Monitors dialog.

## Saved measurements

Measurements are remembered per monitor, including disconnected displays. A
reported serial number identifies the monitor across ports; displays without a
unique serial use their connector. Saved dimensions rotate when a display moves
between landscape and portrait. Applying monitor settings or changing the display
layout refits and recenters the crop overlay; review the preview before applying.

Physical sizing affects both the GUI and command-line crops when enabled. Plasma's
display configuration is unchanged. Turn the option off to return to the normal
desktop-coordinate layout. Saved library sets retain their original crop mappings.
