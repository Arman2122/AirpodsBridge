# Releasing

GitHub Actions builds and tests every push to `main` and every pull request. A tag matching
`v*.*.*` additionally publishes a GitHub Release containing the NSIS installer, a portable
ZIP, and `SHA256SUMS.txt`.

## Versioning

AirpodsBridge follows [Semantic Versioning](https://semver.org). The version lives in two
files and the workflow refuses to build a tag when they disagree:

| File | Field |
| --- | --- |
| `CMakeLists.txt` | `project(AirpodsBridge VERSION x.y.z ...)` |
| `vcpkg.json` | `version-string` |

Release tags are always `vX.Y.Z`. The workflow rejects a tag in any other form.

## Prepare a release

1. Update the version in `CMakeLists.txt` and `vcpkg.json`.
2. Move the `Unreleased` entries in [`CHANGELOG.md`](/CHANGELOG.md) under a new heading for
   the version, dated.
3. Build and test Win32 `RelWithDebInfo` locally:

   ```powershell
   cmake --preset release
   cmake --build --preset release
   ctest --preset release
   ```

4. Merge the version change into `main`.
5. Create and push a matching annotated tag:

   ```powershell
   git switch main
   git pull --ff-only
   git tag -a v1.0.0 -m "Release v1.0.0"
   git push origin v1.0.0
   ```

Do not rename or manually upload the installer: the updater selects an `.exe` asset whose
name contains `win32`.

## Verify a release

Confirm the workflow completed, then download the installer and compare its digest with
`SHA256SUMS.txt`:

```powershell
Get-FileHash .\AirpodsBridge-1.0.0-win32.exe -Algorithm SHA256
```

The automated workflow publishes releases immediately. Keep one as a draft only when manual
acceptance testing is required.

## Installer

The installer is NSIS, produced by CPack from `CMake/NSIS.template.in` rather than CMake's
stock template. It installs for the current user, under `%LOCALAPPDATA%\Programs\AirpodsBridge`,
with no administrator prompt — which is also what lets the in-app updater run it unattended.
It closes a running copy before replacing its files, replaces a previous per-user copy in
place, and detects a copy the 1.0.0 installer put in Program Files for all users, offering
to run that (elevated) uninstaller first. Apps & features gets the icon, publisher, version,
size and links. The finish page offers to launch the application and to add a desktop
shortcut; neither happens unasked. Launching from there passes `--after-install`, so the
application announces itself from the tray once it is up.

The header and sidebar bitmaps under `Source/Resource/Installer/` are generated from the
application icon: run `python Tools/generate_installer_art.py` after changing the icon and
commit the result. Never edit the bitmaps by hand.

The portable ZIP is the installer's own staging tree,
`Build/Release/Installer/_CPack_Packages/win32/NSIS/AirpodsBridge-<version>-win32/`, zipped by
the workflow.

To exercise the installer without clicking through it:

```powershell
.\AirpodsBridge-1.0.0-win32.exe /S /D=C:\Temp\AirpodsBridge
C:\Temp\AirpodsBridge\Uninstall.exe /S
```

## Code signing

Tagged installers are signed when both repository secrets are present:

| Secret | Contents |
| --- | --- |
| `WINDOWS_SIGNING_CERTIFICATE_BASE64` | Base64-encoded PFX certificate |
| `WINDOWS_SIGNING_CERTIFICATE_PASSWORD` | Password for that PFX |

The workflow publishes an *unsigned* installer when neither secret exists, and fails when
only one is configured. The temporary certificate is deleted after `signtool` signs and
verifies the installer.

Without signing, Windows SmartScreen warns on first run of a new release until the binary
builds reputation. That is expected for an unsigned installer, and worth mentioning in
release notes.

## Forks

The updater defaults to `Arman2122/AirpodsBridge`. GitHub Actions overrides the owner and
repository with whichever repository runs the workflow, so a fork's build checks that fork
for test releases. For a local fork build, pass them explicitly:

```powershell
cmake -S . -B Build `
  -DAPB_GITHUB_OWNER=YOUR_GITHUB_OWNER `
  -DAPB_GITHUB_REPOSITORY=YOUR_REPOSITORY
```

`APB_RELEASE_TAG_PREFIX` (default `v`) states whether that repository's tags carry a `v`.
It only affects the version link in the Settings window; the updater's release lookup tries
both forms.
