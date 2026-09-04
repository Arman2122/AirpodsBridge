# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- The battery indicator is now a ring rather than a horizontal cell, matching how iOS
  shows the charge of a connected accessory: a full-circle track with an arc over it
  sweeping clockwise from twelve o'clock. Colours are Apple's system green and red on a
  systemGray5 track, and every measurement derives from the diameter, so the shape holds
  at any size.
- The charging bolt sits in the hollow of the ring, where iOS puts it.
- The close button's cross is inset proportionally and stroked thin with round caps,
  rather than a fixed 3px square-capped cross.

- Interface type is now Inter, bundled under the SIL Open Font License, with SF Pro
  preferred when the machine already has it installed. Sizes, weights and tracking come
  from one type scale rather than being pinned per-widget in the `.ui` forms.
- The action button draws a continuous (superellipse) corner rather than a circular one.
  A stylesheet `border-radius` can only produce a circular corner, so the button paints
  itself; see `Source/Gui/Painting.h`.

- The popup's corners are now rounded by the desktop compositor instead of by clipping the
  window to a region. The old approach used a one-bit mask, which cannot antialias, so
  every curve came out stair-stepped and the window also lost its native drop shadow.
  Windows 10, which has no such attribute, now leaves the window square rather than
  falling back to a mask.
- Window animation length is derived from the display's refresh rate and rounded to a whole
  number of frames, rather than a fixed 500 ms that matched no common refresh rate. It
  collapses to zero when "Show animations in Windows" is turned off.
- Colours and metrics moved out of the widgets and the `.ui` forms into a single token
  header, `Source/Presentation/Theme.h`. The palette stays deliberately light, because the
  bundled device animations are composited onto white.

### Fixed

- Hover and press states on the close button repainted synchronously on every mouse event,
  which caused flicker. They now coalesce into the next paint.
- The close button's circle was drawn to the integer widget rect, so its antialiased edge
  was clipped by the widget boundary and left a hard fringe.

## [1.0.0] - 2026-09-04

First public release.

### Added

- Battery levels for each bud and the charging case, shown in a tray popup and optionally
  on the tray icon or the taskbar.
- Automatic ear detection: media pauses when a bud is removed and resumes when it is
  returned.
- Quick connect, so one click on the tray icon connects the bound device.
- Per-model animations for AirPods 1 through 4, AirPods Pro 1 through 3, AirPods Max and
  Beats Fit Pro.
- In-app update checking against GitHub Releases.
- An NSIS installer and a portable ZIP, published with SHA-256 checksums, and optional
  Authenticode signing when repository secrets are configured.

[Unreleased]: https://github.com/Arman2122/AirpodsBridge/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Arman2122/AirpodsBridge/releases/tag/v1.0.0
