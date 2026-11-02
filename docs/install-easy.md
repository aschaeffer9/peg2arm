# Installing peg2arm: step by step

This puts Peggle 2 on your Android phone, built from **your own copy** of the game. No programming is needed: an
installer does the work and tells you what to do at each step. Plan for about two hours the first time, mostly
waiting.

## What you need

- **The game:**
  - Peggle 2's **Xbox 360** (Xbox Live Arcade) package, copied from your console. It's the file named
    `2452FB99DD4F3E052C919B780FEAE8F8E8281CF058`, under `Content\0000000000000000\58411436\000D0000\`.
  - Peggle 2 **Magical Masters Edition for PS4** (`CUSA00743`). The installer page uses it to make the game data, as
    for the Switch version.
- **A Windows 10 or 11 PC** with about **40 GB free** and an internet connection, for the tools.
- **An Android phone:**
  - 64-bit: most phones from 2017 on.
  - Vulkan 1.1.
  - 3 GB free.
- **A USB cable** for the phone.

## Steps

### 1. Get this repository

On the repository page, press the green **Code** button, then **Download ZIP**. Unzip it somewhere with the free
space, for example `D:\peg2arm`. Keep the folder name short and without spaces.

### 2. Make the game data in your browser

1. Open the **peggle2-nx installer page**: https://chanseyisthebest.github.io/peggle2-nx-installer/
2. Choose your Xbox 360 package and your PS4 package, then press **Create peggle2-nx.zip**. It all happens in your
   browser, and your files are not uploaded.
3. Keep the `peggle2-nx.zip` it saves. The installer asks for it at the end.

### 3. Run the installer

Double-click **`install\peg2arm-setup.bat`** in the folder from step 1. A window opens and goes through five steps:

1. **Tools.** It installs what is missing:
   - Git
   - Python
   - LLVM
   - the Visual Studio C++ Build Tools, about 7 GB
   - Java
   - the Android SDK and NDK

   Windows may ask you to allow some of these; say yes.
2. **Your game.** A file window opens; choose your Xbox 360 package.
3. **PC build.** It translates the game's code and builds it, which takes about an hour. Near the end the game opens
   in a window for 45 seconds by itself. Leave it alone; it closes on its own.
4. **Android app.** It builds the app, a few minutes.
5. **Phone.** It tells you how to turn on **USB debugging**:
   1. Open **Settings > About phone > Software information** and tap **Build number** seven times.
   2. Open **Settings > Developer options** and turn on **USB debugging**.
   3. Connect the phone, unlock it, and press **Allow** when it asks.

   The installer then installs the app and asks for your `peggle2-nx.zip`, which it copies to the phone: 2.7 GB, a few
   minutes.

When it says **Done**, open **Peggle 2** from your phone's app drawer.

If a step fails, the window says what went wrong. Fix it (often just a cable, the phone's screen lock, or free
space) and double-click the installer again. It remembers what is done and carries on from there. The full log is
`out\install_log.txt`.

To see what the installer would do without changing anything, run `install\peg2arm-setup.bat -Check`.

## Playing

| | |
|---|---|
| Aim | drag your finger; the shot goes where your finger is |
| Fire | tap |
| Speed up the ball | hold two fingers down |
| Pause | the pause button at the top left, or the back gesture |
| Sound on/off | the button under pause |

Your progress is saved on the phone, in `Android/data/com.ari.peggle2/files/peggle2/`. **Uninstalling the app deletes
it**, so copy that folder first if you want to keep it.

## Optional: the cover launcher

`peggle_launcher.apk`, attached to each release, shows the Peggle games on your phone as box covers. It finds
peg2arm, Peggle Enhanced and Peggle Blast. To add your own cover pictures, see
[launcher/README.md](../launcher/README.md).

## Updating

Download the new version of this repository, unzip it over the old folder, and run the installer again. The tools,
the PC build and your phone's game data are kept, so only the changed parts rebuild.

For the technical side, see [android.md](android.md).
