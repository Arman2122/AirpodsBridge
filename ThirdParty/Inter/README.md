# Inter

AirpodsBridge embeds four cuts of [Inter](https://rsms.me/inter/) in its Qt resources and
uses them for all interface text:

| File | Used for |
| --- | --- |
| `Inter-Regular.ttf` | body text |
| `Inter-Medium.ttf` | secondary text |
| `Inter-SemiBold.ttf` | emphasis |
| `InterDisplay-SemiBold.ttf` | the device name |

Inter is licensed under the [SIL Open Font License 1.1](LICENSE.txt), which permits
embedding and redistribution. The licence text is installed to `Licenses/Inter` beside the
application.

## Why Inter and not SF Pro

Apple's SF Pro is the face this interface is modelled on, and the application prefers it at
runtime when the machine already has it installed. It is not bundled: Apple's font licence
covers development for Apple platforms and does not permit redistributing the font with a
Windows application.

Inter was drawn for the same job — user interface text at small sizes on screen — so it
sits at a similar optical weight and width, and substitutes without the layout shifting.

The preference order lives in `Source/Presentation/Theme.cpp`.
