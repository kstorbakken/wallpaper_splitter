# Slideshow

[← User guide](usage.md) · [README](../README.md)

## Set up rotation

Open **Library → Slideshow…** to build a playlist from saved sets:

1. Check the sets to include. Drag them into order, or use **Move up** and **Move down**.
2. Choose an interval from **1 to 10080 minutes** (one week). The default is 30 minutes.
3. Optionally enable **Shuffle** to choose randomly, avoiding consecutive repeats
   when more than one usable set is available.
4. Check **Rotate automatically while logged in** and click **Save**. You can then
   close Wallpaper Splitter. The first scheduled change happens after the interval.

**Next now** applies the next saved playlist entry immediately, even with automatic
rotation off. Save playlist edits before using it. To stop rotation, uncheck
**Rotate automatically while logged in** and click **Save**; the current wallpaper
stays in place. Do this before uninstalling the app too.

## Monitor layouts and deleted sets

Each switch reapplies a complete set to whichever **Plasma activity is active at
that moment**. Other activities keep their wallpapers. Saved monitor positions and
resolutions must match the connected layout. Missing, damaged, deleted, and
incompatible sets are skipped; if none are usable, the wallpaper stays unchanged.

Deleting sets through the Library also removes their playlist entries. Removing
the last selected set turns automatic rotation off. The chooser shows only sets
in the current library; click **Save** to discard stale entries retained by earlier
versions. Original source images are not needed.

## Scheduling and AppImages

Automatic rotation requires a **systemd user session integrated with the graphical
desktop**. The app installs a user timer and a short-lived service, without root
access or an always-running Wallpaper Splitter process. The timer starts with the
graphical session, stops at logout, and does not wake the computer or replay missed
slides after sleep. It waits the selected interval between scheduled runs; a manual
**Next now** does not reset that timer. Systems without systemd can still save a
playlist and use **Next now**.

For **AppImage** installations, keep the AppImage in a permanent location. After
moving it or switching to a new AppImage filename, launch that AppImage and click
**Save** in the Slideshow dialog to update the scheduled command.
If you launched it with `APPIMAGE_EXTRACT_AND_RUN=1` to work around missing FUSE,
the saved service preserves that setting.

## Troubleshooting

The dialog shows the last switch attempt and any saved error. **Refresh status**
checks the timer again; clicking **Save** repairs or restarts its configuration.
For scheduler or launch failures, inspect the user service:

```sh
systemctl --user status wallpaper-splitter-slideshow.timer wallpaper-splitter-slideshow.service
journalctl --user -u wallpaper-splitter-slideshow.service -n 30
```

Playlist settings and playback state are stored in
`~/.config/wallpaper-splitter/slideshow.json`. Generated timer/service files live in
`~/.config/systemd/user/wallpaper-splitter-slideshow.{timer,service}`. These paths
respect `XDG_CONFIG_HOME`. Turning automatic rotation off and clicking **Save**
stops the timer and removes its startup link and both generated unit files.
Your playlist and playback state are kept for manual use or enabling rotation again.
