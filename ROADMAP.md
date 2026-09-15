# Roadmap

This page collects ideas for this independent fork. It is not a commitment,
release schedule, or statement of direction on behalf of the original project.

## Requests from the original project

The original Wallpaper Splitter repository still contains useful feature
requests. Their status below describes this independent fork only, not the state
shown on the original issues.

| Status | Request | Notes |
| --- | --- | --- |
| ✅ Supported | [Inherit original filenames][upstream-13] | Generated crops use the source image's base name. More control over generated names is included in **Output settings** below. |
| ✅ Supported | [Scene actions][upstream-2] | Drag and drop, <kbd>Ctrl</kbd> + <kbd>Mouse wheel</kbd> zoom, and middle-button dragging to pan the zoomed preview are supported. |
| ✅ Supported | [Configure default input and output folders][upstream-12] | The application remembers the last successfully used input and export folders. |
| 📋 Proposed | [Account for physical screen sizes][upstream-11] | Scale monitor rectangles using their real-world dimensions as well as their pixel resolutions. |
| ✅ Supported | [Apply split wallpapers from the command line][upstream-10] | The `--apply` option creates a managed set and applies it to the current Plasma activity. |
| 📋 Proposed | [Add an "as large as possible" layout mode][upstream-8] | Offer an additional automatic image/layout scaling mode. |

Completed requests and bug reports from the original project are not duplicated
here. See its [full issue history][upstream-issues] for that archival context.

## Output management

### Output settings

Persistent settings are available for:

- Default input and export folders.
- An export filename template, with fields such as source name, screen name,
  screen number, and revision.
- Collision handling: replace an existing set, create a new revision, or ask.

Exports use readable names by default and support `{source}`, `{screen}`,
`{number}`, `{revision}`, and `{digest}` template fields. Users can replace an
existing set, create a new set-wide revision, or be asked each time. Managed
wallpaper crops retain content digests internally so Plasma notices changes.

### Wallpaper set library

The **Library…** viewer lists managed wallpaper sets with a preview, source image
path, creation time, monitor layout, crop mappings, and generated files. Sets can
be reapplied to a matching monitor layout, renamed, and deleted.

Applying and exporting are separate workflows:

- **Apply** writes crops and a small manifest to an application-managed data
  directory. The application owns these files and can present them as a tidy
  library instead of placing them beside the source image.
- **Export** writes user-owned files to the configured folder using the chosen
  filename template. Exported files should not be removed automatically.

The library reads the versioned JSON manifests introduced in Wallpaper Splitter
1.3. Before deletion, it checks live and saved Plasma wallpaper references,
including other activities and disconnected monitors. If references cannot be
checked or still use the set, deletion is blocked. Source images and exports
remain user-owned.

### Source image plus crop mappings

Investigate a future mode that stores only the original image and per-monitor
crop mappings. The standard Plasma image wallpaper accepts a persistent image
file URL for each desktop; it does not accept an in-memory image or an external
crop rectangle. Avoiding generated crop files would therefore require a custom
Plasma wallpaper plugin that reads the source image and mapping data.

Until that plugin exists, managed on-disk crops are the compatible approach.
They can be treated as an internal cache/library so users do not have to manage
them directly.

## Upstream inclusion in Plasma

The long-term goal is to make spanning one wallpaper across multiple screens a
built-in Plasma feature. KDE already tracks this as [confirmed wishlist item
393781][kde-393781], and a Plasma maintainer has invited contributors to submit
a new merge request after an earlier attempt stalled.

The likely upstream contribution is not the standalone application in its
current form. It is a focused spanning mode in Plasma's existing Image wallpaper
plugin, with Wallpaper Splitter serving as a reference implementation and a
place to validate the interaction design and crop calculations.

A practical path toward upstreaming is:

1. Stabilize and test the crop/layout model here, including mixed resolutions,
   monitor positions, disconnected screens, and layout changes.
2. Discuss the intended behavior on KDE's existing wishlist item before making
   assumptions that would be difficult to change during review.
3. Prototype a **Span across screens** mode directly in Plasma Workspace's
   Image wallpaper plugin. It should render the relevant portion of one source
   image for each screen without generating intermediate files.
4. Add automated coverage for image positioning and multi-screen geometry, then
   submit a focused merge request through [KDE Invent][plasma-workspace] linked
   to the wishlist item.
5. Keep this application useful for advanced editing, exporting, saved sets,
   and compatibility with Plasma versions that do not yet include the feature.

Upstream acceptance and timing are controlled by KDE's maintainers, so this is
a direction rather than a promised release. KDE accepts external changes through
reviewed merge requests, and a KDE developer account is not required to submit
one; see KDE's [development contribution guide][kde-contributing].

[kde-393781]: https://bugs.kde.org/show_bug.cgi?id=393781
[kde-contributing]: https://community.kde.org/Infrastructure/GitLab
[plasma-workspace]: https://invent.kde.org/plasma/plasma-workspace
[upstream-issues]: https://github.com/l0drex/wallpaper_splitter/issues?q=is%3Aissue
[upstream-2]: https://github.com/l0drex/wallpaper_splitter/issues/2
[upstream-8]: https://github.com/l0drex/wallpaper_splitter/issues/8
[upstream-10]: https://github.com/l0drex/wallpaper_splitter/issues/10
[upstream-11]: https://github.com/l0drex/wallpaper_splitter/issues/11
[upstream-12]: https://github.com/l0drex/wallpaper_splitter/issues/12
[upstream-13]: https://github.com/l0drex/wallpaper_splitter/issues/13
