# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-09-07

First public release.

### Added

- Battery levels for each bud and the charging case, shown in a tray popup and optionally
  on the tray icon or the taskbar.
- Automatic ear detection: media pauses when a bud is removed and resumes when it is
  returned.
- Quick connect, so one click on the tray icon connects the bound device.
- In-app update checking against GitHub Releases.
- An NSIS installer and a portable ZIP, published with SHA-256 checksums, and optional
  Authenticode signing when repository secrets are configured.
- Three options. *Check for updates automatically* (on by default) stops the hourly
  GitHub request when turned off. *Show the popup when the case opens* (on) makes the
  lid-open popup a choice. *Resume playback when both are in* (on) lets ear detection
  pause without ever resuming, for anyone who found the resume surprising.
- Four behaviours on the Features page. *Pause when AirPods disconnect* (on by default)
  stops media carrying on through the speakers. *Low battery notification* (on, at 20 %)
  shows a notification once per drop, and its level — 10, 20 or 30 % — also drives the
  tray icon's "when low" option. *One AirPod is enough* (off) makes ear detection follow
  the last bud rather than both. *Connect when the case opens* (off) connects the quick
  connect device when its case opens nearby; it keeps Bluetooth scanning on while
  disconnected and says so.
- The popup draws the device with Apple's own artwork: the render and loop an iPhone shows
  on its proximity card, at Apple's own 60 fps, transparent, in the device's own colour
  where Apple publishes it (every AirPods Max and Beats Fit Pro colour). `Tools/apple_device_assets.py` fetches the
  bundles from Apple's asset catalogue, decodes the HEVC-with-alpha loops by splitting
  their layers, and writes `Source/Resource/Device/`; `check` reports when Apple has
  published newer artwork. See Docs/Artwork.md.
- A notification once the installer has launched the application, saying that it is
  running in the system tray and what the icon does. `--after-install` shows it; the
  installer's finish page passes that. The first run shows the same one.
- AirPods Max, and the other one-piece devices, show one battery: one ring in the popup,
  one line in the tray tooltip, one figure on the taskbar, with no left, right or case.
  The advertisement carries the level on one side, and the application reads it from
  whichever side has it.
- Dark mode. *Theme* on the General page is System, Light or Dark; System follows the
  mode Windows is set to and changes with it while the application runs, without a
  restart. The popup, the settings window, the dialogs, the tray menu and the title bars
  all follow. The dark palette is designed rather than inverted, and
  `Tests/ThemeTests.cpp` holds both palettes to WCAG contrast on every surface text is
  drawn on.
- `Tests/AutomationTests.cpp` drives every one of those rules through its transitions
  without hardware, and `Tests/SettingsPreview.cpp` renders the settings window to PNG so
  it can be looked at without a tray icon or a Bluetooth adapter.
- Animations run at the display's refresh rate, on any display. The scroll glide, the
  switches, the popup sliding in and the device loop are advanced once per vertical blank
  of the display they are on — 60, 120, 144, 240 or 300 Hz — rather than from Qt's fixed
  60 Hz timer, and stop costing anything the moment nothing moves. `Gui::FrameClock`
  (`Source/Gui/FrameClock.h`) is the one clock; there is no setting, because there is
  nothing to choose.

### Changed

- Settings changes are subscriptions. `Core::Settings::Watch` replaces the `ApplyObserver`
  interface that grew a method per setting; `ApbApplication::WireSettings` is now the
  complete list of what settings drive, one line each, and watchers run after the settings
  lock is released.
- The decisions behind the Features page are policies in
  `Source/Domain/AutomationPolicies.h`, carried out by `Core::Automation::Engine` through an
  `Effects` interface. The device manager reports and no longer decides.
- The update checker is owned by the application rather than the main window.
- The application's stock widgets -- message boxes, the device chooser, the update
  download window, the tray menu -- are drawn by Qt's Fusion style in both modes, from the
  application's own palette, instead of by the Windows style. The Windows style in Qt 5
  paints buttons and menus through the system theme and cannot go dark.
- The bundled AVI animations and Qt Multimedia are gone: the popup paints animated WebP
  frames through `Widget::DeviceView`, which decodes nothing while the popup is hidden and
  shows a still when Windows' animation setting is off. The installer no longer ships the
  Qt Multimedia libraries or media plugins.
- The installer is rebuilt on a custom NSIS template: it installs for the current user
  under `%LOCALAPPDATA%\Programs` with no administrator prompt, has branded, DPI-aware
  pages, offers launch and a desktop shortcut on the finish page, closes a running copy
  before replacing files, replaces a previous version in place, and registers with Apps &
  features properly. A copy installed for all users by the 1.0.0 installer is detected and
  its uninstaller offered.
- The quick connect device is chosen once and serves both the tray click and auto-connect.
- The popup's battery rings sit under what they measure. Apple composes each model's
  picture differently, so the artwork catalogue now records where the buds and the case
  are and the rings are placed from that, instead of at one fixed position.
- The mouse wheel glides through the settings window instead of jumping a step per notch,
  and a scroll over the range slider or the device list no longer changes the setting
  under the pointer.
- The settings window is rebuilt. Pages are chosen from a sidebar instead of tabs; every
  setting is a row with its explanation under the title rather than behind a hover tip;
  on/off settings are switches, three-way choices are segmented controls, and the
  Bluetooth range slider shows the value it is set to. It uses the same type scale,
  colours and continuous corners as the popup.
- Restore defaults asks first, and keeps the bound AirPods and the quick connect device
  rather than forgetting them along with the preferences.
- The quick connect device list is greyed out until quick connect itself is on, like
  every other dependent option; it used to stay active regardless.
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

[Unreleased]: https://github.com/Arman2122/AirpodsBridge/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/Arman2122/AirpodsBridge/releases/tag/v1.0.0
