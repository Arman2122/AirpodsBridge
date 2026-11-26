# usbip-win2

AirpodsBridge's optional No-Test-Mode Radio Proxy Service integrates with `usbip-win2`,
an open-source Windows USB/IP virtual host controller driver maintained by Vadym
Hrynchyshyn and distributed under the BSD 2-Clause licence.

- Upstream repository: https://github.com/vadimgrn/usbip-win2
- Release downloads: https://github.com/vadimgrn/usbip-win2/releases
- Licence: BSD-2-Clause (https://github.com/vadimgrn/usbip-win2/blob/master/LICENSE)

`usbip-win2` is not compiled into AirpodsBridge; when a user opts into the No-Test-Mode
Radio Proxy Service, `usbip-win2` provides the Microsoft-signed virtual USB host
controller (`usbip2_ude`) that allows Windows' own Bluetooth stack (`BTHPORT`) and inbox
`usbaudio.sys` driver to attach to AirpodsBridge's user-mode proxy while Secure Boot stays
enabled.
