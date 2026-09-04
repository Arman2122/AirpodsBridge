# Qt

AirpodsBridge links Qt 5.15.2 dynamically and redistributes the Qt DLLs alongside the
application. Qt is used under the **GNU Lesser General Public License version 3**.

- [`LICENSE.LGPLv3`](LICENSE.LGPLv3) — the licence Qt is used under.
- [`LICENSE.GPLv3`](LICENSE.GPLv3) — LGPLv3 is written as a set of additional permissions
  on top of GPLv3 and incorporates it by reference, so both texts are needed to read it.

AirpodsBridge itself is [MIT](/LICENSE). The LGPL applies to Qt, not to this project.

## How this distribution complies

- **Dynamic linking.** Qt is linked as shared libraries and the DLLs ship next to the
  executable. A user may replace them with their own build of the same Qt version, which
  is what LGPLv3 section 4(d) asks for.
- **Notice.** The licence texts above are installed into `Licenses/` beside the
  application, and the Settings window's About pane names Qt and links to its licence.
- **Source.** Qt 5.15.2 sources are published by the Qt Company at
  <https://download.qt.io/archive/qt/5.15/5.15.2/single/>. No Qt source is modified here.

If you change how Qt is linked — in particular if you ever link it statically — this
arrangement no longer holds and the obligations change substantially.
