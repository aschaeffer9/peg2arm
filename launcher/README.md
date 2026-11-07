# Peggle Launcher (optional)

A small Android app that shows the Peggle games installed on the phone as a row of box covers. Swipe to pick one
and tap the cover in front to start it. The launcher then closes.

It shows only what it finds installed:

| Cover | Shown when installed | Starts |
|---|---|---|
| Peggle, Peggle Extreme, Peggle Nights, Peggle Dual Shot, Peggle: World of Warcraft Edition | [Peggle Enhanced](https://github.com/aidrum/peggle-enhanced) (`com.aidrum.peggle.hd` or `com.aidrum.peggle`) | Peggle Enhanced |
| Peggle 2 | peg2arm, this repository's port (`com.ari.peggle2`) | the port |
| Peggle Blast | EA's Peggle Blast (`com.ea.game.pegglex_row`) | Peggle Blast |

Each Peggle Enhanced cover also tells Peggle Enhanced which game it is for (an intent extra, `peggle_target`) so it can go straight to the
game the cover names.

No cover art comes with the launcher. Until you add your own images, each game is a plain coloured card with its
title and year.

## Build

Needs the Android SDK (build-tools and the `android-36` platform) and a JDK 17 or 21; no Gradle.

```
python launcher/build_launcher.py --out out/launcher/peggle_launcher.apk
adb install -r out/launcher/peggle_launcher.apk
```

`--sdk`, `--build-tools`, `--platform`, `--jdk` and `--keystore` choose other locations; by default the SDK is
`%LOCALAPPDATA%\Android\Sdk`, the newest build-tools and the newest JDK in `~/.jdks`. A debug signing key is created
in `out/` the first time.

## Adding your own cover art

Start the launcher once, so Android creates its folder. Then copy your images into:

```
Android/data/com.ari.peggle.launcher/files/covers/
```

You can do this over USB with a file manager, or with adb:

```
adb push peggle.jpg /sdcard/Android/data/com.ari.peggle.launcher/files/covers/
```

Name each image after its game:

| Game | File name |
|---|---|
| Peggle | `peggle` |
| Peggle Extreme | `extreme` |
| Peggle Nights | `nights` |
| Peggle Dual Shot | `dualshot` |
| Peggle: World of Warcraft Edition | `wow` |
| Peggle 2 | `peggle2` |
| Peggle Blast | `blast` |
| the background behind the covers | `background` |

Each name takes one of the extensions `.png`, `.jpg`, `.jpeg` or `.webp`, for example `peggle2.png`. When the same
name exists with several extensions, the first in that order is used. Close and reopen the launcher to pick up new
images.

### What images work

| | Limit |
|---|---|
| Formats | PNG (transparency kept), JPEG, WebP |
| File size | at most 25 MB |
| Width and height | each from 256 to 8192 pixels |
| Shape | the long side at most 3 times the short side (3:1 or 1:3) |

An image outside these limits is skipped, and the launcher lists what it skipped and why when it opens. That game
then keeps its plain card.

### How images are shown

- **Covers keep their shape.** Each cover is scaled to fit whole inside its place in the row, a 3:4 portrait box,
  and is never stretched or cropped. Portrait boxes such as the PC and Xbox 360 cases fill it. Wider boxes, such as
  DS cases or square art, sit on the same bottom line and are shorter.
- **Black around a box is removed.** Scans that put the box on a black canvas are trimmed: near-black pixels
  connected to the image's edges become transparent, and the image is cropped to the box. Black inside the artwork
  is kept. A PNG that is already transparent around the box needs nothing.
- **Big images are fine.** Covers are decoded no larger than the screen's height and cached after trimming, so a
  4000-pixel scan costs no more memory than a 1500-pixel one. For a sharp result on a 1440p phone, use 1500 x 2000
  pixels or more for a portrait cover.
- **The background fills the screen.** It is scaled to cover the whole screen and cropped at the sides or top and
  bottom as needed. It is drawn with 25 % less colour and partly see-through over a dark backdrop, so the covers stand
  out. A landscape image of at least the screen's size, such as 2560 x 1440 or larger, looks best.

Use art you own or are allowed to use. The launcher does not download anything.
