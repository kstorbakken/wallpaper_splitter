# Slideshow

[← User guide](usage.md) · [README](../README.md)

## Set up rotation

Open **Library → Slideshow…** to build a playlist from saved sets:

1. In the **Desktop** tab, check the sets to include. Both tabs open in name
   order. Click **Name** or **Date Added** to sort both lists. The playlist in
   the tab you clicked takes that order. Drag rows or use **Move up** and
   **Move down** to fine-tune it, then click **Save**. To see a playlist's
   current sequence, right-click its header and choose **Show playback order**.
2. Choose a **Lock screen** mode.
   - **Off** leaves your current lock-screen wallpaper in place.
   - **Match desktop** uses the exact set the desktop switches to, including when
     Shuffle is on.
   - **Separate playlist** adds a **Lock Screen** tab where you can select and
     order its sets. It advances independently on the same interval. Clear the
     desktop selection if you only want to rotate the lock screen.
3. Choose an interval from **1 to 10080 minutes** (one week). The default is 30
   minutes. **Shuffle** avoids consecutive repeats when more than one usable set
   is available. Separate playlists shuffle independently.
4. Check **Rotate automatically while logged in** and click **Save**. You can
   then close Wallpaper Splitter. The first scheduled change happens after the
   interval. On first use, saving automatic lock-screen rotation can set an
   initial lock-screen wallpaper so the plugin is ready when you lock.

**Next now** advances the configured playlists immediately, even with automatic
rotation off. Save playlist edits before using it. To stop rotation, uncheck
**Rotate automatically while logged in** and click **Save**. The current
wallpapers stay in place. Do this before uninstalling the app too.

## Live lock-screen updates

Each display fades to its matching crop while the lock screen is visible. While
only the displays are powered off, the slideshow continues publishing the latest
set without requesting a display wake. When the displays turn back on, the lock
screen uses that set and skips any expired transitions.

## Monitor layouts and deleted sets

Desktop changes apply to whichever **Plasma activity is active at that moment**.
Other activities keep their wallpapers. Saved monitor positions and
resolutions must match the connected layout. Missing, damaged, deleted, and
incompatible sets are skipped; if none are usable, the wallpaper stays unchanged.

Deleting sets through the Library removes them from both playlists. Removing the
last separate lock-screen set switches its mode to **Off**. Automatic rotation
stops when there are no selected sets left to rotate. The chooser shows only sets
in the current library. Original source images are not needed.

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

The dialog shows the last switch attempt and any saved error. With separate
playlists, a failure on one wallpaper can leave the other rotation running.
**Refresh status** checks the timer again. Clicking **Save** repairs or restarts
its configuration.
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
