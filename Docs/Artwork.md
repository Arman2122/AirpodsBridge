# Device artwork

The battery popup shows the device that is actually in range, drawn with Apple's own
renders: the picture and the animation an iPhone shows on its proximity card, with the same
transparent background, and in the device's own colour where Apple publishes it.

This document says where that artwork comes from, what ships, how to refresh it, and what
it costs.

## Where it comes from

Apple delivers the artwork to iPhones as a MobileAsset,
`com.apple.MobileAsset.SharingDeviceAssets`. Its catalogue is a signed plist on
`mesu.apple.com` with one entry per product and colour, each pointing at a zip on Apple's
CDN. A zip is a `.devicebundle`:

| File | What |
| --- | --- |
| `ProxCard_L@3x.png`, `ProxCard_R@3x.png` | each bud, transparent |
| `ProxCard_case-closed*.png`, `ProxCard_case-open@3x.png` | the case, transparent |
| `ProxCard_loop-charged.mov`, `ProxCard_loop.mov` | the card's loop: the open case with the buds, HEVC with an alpha layer |
| `ProxCard_connect-*.mov`, `ProxCard_features.mov` | the pairing flow and the feature tour |

Two fields of an entry matter. `ProductType` names the product, `AirPodsPro1,3` for AirPods
Pro 3, and `CL` is the colour. `CL` is the same number the device broadcasts as its colour
byte in every advertisement, which is what lets the popup show a blue AirPods Max as blue.

## What ships

`Tools/apple_device_assets.py` reads the catalogue and writes `Source/Resource/Device/`:

| File | What |
| --- | --- |
| `<Product>/CL_<n>/still.png` | the first frame of the loop, transparent, sized to fit twice the popup's slot (480 × 212) |
| `<Product>/CL_<n>/loop.webp` | the loop at Apple's own 60 fps, an animated WebP with alpha, 2 to 3 MB |
| `catalog.json` | what is there, by product and colour, the Apple build it came from, and each picture's `anchors`: where the buds and the case sit, as fractions of the width, measured from the still |
| `Device.qrc` | the list Qt compiles in |
| `NOTICE.txt` | whose artwork this is; installed next to the other licences |

The policy is the `PRODUCTS` table in the tool:

- every model the application decodes gets a still for every colour Apple publishes;
- each of those products' default colour also gets the loop;
- the three Beats the decoder does not produce yet get a still of their default colour only.

A loop is 2 to 3 MB and a still 30 to 200 KB, about twice what the old AVI files weighed.
The frame rate is what costs: every source frame is kept, because the loops are what an
iPhone shows and 30 fps reads as steppy next to it. `LOOP_FPS` in the tool halves them if
size ever matters more. Animating every colour would add a loop per colour; that is
`fetch --animate all`, and a size decision rather than a technical one.

Apple composes each picture differently -- the buds sit at 17 % of the width in one model
and 23 % in another, the case at 71 % to 79 % -- so the popup does not put the battery rings
at a fixed place. The tool measures where the objects are (the picture is split at the
widest clear gap; a headphone has none and is one object) and the popup centres the rings
under those anchors, which is also how it shows one ring for AirPods Max.

The application side is `Gui::DeviceArtwork` in `Source/Presentation/DeviceArtwork.h`: it
reads `catalog.json` from the qrc once and resolves a model and colour to its files, falling
back to the product's default colour. `Widget::DeviceView` shows the result, animated when
there is a loop and Windows' animation setting is on, the still otherwise. It paces the loop
by the clock rather than by the decoder: on every tick the frame that is due is decoded and
shown, and a frame that comes late is skipped, so the loop keeps Apple's exact length on any
display and on a slow machine drops frames rather than slowing down.

## How the movies are decoded

The loops are HEVC with a separate alpha layer, two layers in one track. FFmpeg refuses the
stream at the VPS, the part that describes the layers, so the tool does not ask it to
decode the movie. It parses the QuickTime file itself, splits the NAL units by layer, gives
each layer an ordinary single-layer VPS, and hands FFmpeg two plain streams: the colour
layer, and the alpha as a greyscale picture. The colour layer is premultiplied by the alpha
and is un-premultiplied before the frames are cropped to the content, scaled, and written.

The tool checks the two layers stayed in step. A frame with colour where there is no alpha
fails the run; it would show as ghosting on every loop.

## Refreshing

Apple adds bundles when it ships a product or a colour. To see whether anything changed:

```powershell
python Tools/apple_device_assets.py check
```

It compares the committed catalogue with Apple's and reports what is newer, without
downloading any bundle. To refresh:

```powershell
python Tools/apple_device_assets.py fetch
```

That needs Python 3.10 or newer, Pillow, numpy, and `ffmpeg` and `ffprobe` on `PATH`.
Downloads are cached under `Build/AppleDeviceAssets/`; a full run fetches about 350 MB
once. The conversion is deterministic: the same Apple build produces byte-identical files,
so a refresh that changes nothing leaves the working tree clean. `--product` and `--cl`
restrict a run, `--skip-existing` leaves unchanged variants alone, and `--animate` chooses
which colours get the loop.

Commit the result as it is written. `Tests/DeviceArtworkTests.cpp` checks that every model
has artwork, that every file the catalogue names exists and is listed in the qrc, and that
every loop is an animated WebP with alpha that Qt's plugin can read. `PopupPreview` renders
the popup for every model so the result can be looked at:

```powershell
Build\dev\Binary\PopupPreview.exe C:\some\folder
```

## Adding a model

1. The product ID and `Model` value, in `Source/Domain/AppleCP.cpp` and `Source/Domain/Base.h`.
2. Its `ProductType`, in `Core::AirPods::AppleProductType` (`Source/Domain/Base.h`). Run
   `python Tools/apple_device_assets.py list --all` to find it.
3. If it is one thing with one battery -- headphones, a neckband -- add it to
   `Core::AirPods::IsOnePiece` in the same file. The artwork test checks that the picture
   agrees: one object for a one-piece device, buds and a case for a pair.
4. The same product in the tool's `PRODUCTS` table, with its default colour.
5. `fetch`, then the tests.

## Licence

The artwork is Apple's. It is fetched from Apple's public asset server and converted; it is
not covered by the repository's licence, and `Source/Resource/Device/NOTICE.txt` says so
in the installed copy as well. AirPods, AirPods Pro, AirPods Max and Beats are trademarks of
Apple Inc.; this project is not affiliated with, authorised by, or endorsed by Apple Inc.
