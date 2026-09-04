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
