<div align="center">

<img src="https://raw.githubusercontent.com/blueshockz47/ounceshell-pro/development/switch2/resources/icon.jpg" width="112" alt="NS2xZilla icon">

# NS2xZilla

**A modern file manager and FTP server for Nintendo Switch 2 homebrew.**

Browse. Organize. Transfer. Stay in control.

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](https://github.com/blueshockz47/ounceshell-pro/blob/development/LICENSE)
[![Status](https://img.shields.io/badge/Status-Stable-green)](https://github.com/blueshockz47/ounceshell-pro/actions)

[Download](#download) · [Features](#features) · [Quick start](#quick-start) · [Controls](#controls) · [Credits](#credits)

</div>

## Overview

NS2xZilla is an independent evolution of OunceShell, designed for convenient SD card management directly on your Switch 2 and reliable local file transfers from a computer or phone. It combines a touch-friendly interface, flexible screen orientation and an integrated FTP server in one homebrew application.

## Download

**NS2xZilla v1.0.0-Stable**

[Download from GitHub Releases](https://github.com/blueshockz47/ounceshell-pro/releases/latest)

The stable release is based on RC33, which passed compilation and all 12 hardware checks reported by the maintainer. The release asset will be available once published.

## Features

| File management | Interface |
| --- | --- |
| Browse SD card files and folders | Grid and list views |
| Copy, move, paste, rename and delete | Touchscreen and controller navigation |
| Multi-selection and select all | Eight NS2xZilla themes |
| New folders and filename search (jump to match) | Manual portrait and landscape rotation |
| File properties and sorting | Persistent theme, view and orientation preferences |
| Conservative overwrite protection | On-screen shortcuts and transfer status |

### NS2xZilla Themes

Choose Ocean Blue, Midnight Purple, Emerald Green, Sunset Orange, Crimson Red, Arctic Cyan, Royal Gold or Titanium Silver by tapping the theme name. The interface features color-coded folders, selection outlines, rounded controls, touch selection and responsive portrait and landscape layouts.

### Integrated FTP server

- Start and stop FTP directly from the file manager.
- Display the local IP address, port, connected clients and upload/download rates.
- Standard passive-mode replies and REST-based resume support for compatible clients.
- Connection recovery with backoff, socket cleanup and bounded concurrent sessions (up to four).
- Local diagnostic logging and detailed filesystem error reporting.
- Compatible with standard FTP clients and designed to work with SwitchNest.

Resume and retry behavior also depends on the client. Verify important transfers independently; no throughput or zero-data-loss guarantee is implied.

### Built-in tools

Inherited from OunceShell: archive browsing, text and hex editing, image viewing and audio playback. Format compatibility and individual viewer behavior may vary on Switch 2; these inherited tools have not all been independently regression-tested in this fork.

## Screenshots

<table>
  <tr><td><img src="https://raw.githubusercontent.com/blueshockz47/ounceshell-pro/master/IMG_8081.jpeg" width="100%" alt="NS2xZilla screenshot 1"></td><td><img src="https://raw.githubusercontent.com/blueshockz47/ounceshell-pro/master/IMG_8082.jpeg" width="100%" alt="NS2xZilla screenshot 2"></td></tr>
  <tr><td><img src="https://raw.githubusercontent.com/blueshockz47/ounceshell-pro/master/IMG_8083.jpeg" width="100%" alt="NS2xZilla screenshot 3"></td><td><img src="https://raw.githubusercontent.com/blueshockz47/ounceshell-pro/master/IMG_8084.jpeg" width="100%" alt="NS2xZilla screenshot 4"></td></tr>
  <tr><td><img src="https://raw.githubusercontent.com/blueshockz47/ounceshell-pro/master/IMG_8085.jpeg" width="100%" alt="NS2xZilla screenshot 5"></td><td><img src="https://raw.githubusercontent.com/blueshockz47/ounceshell-pro/master/IMG_8086.jpeg" width="100%" alt="NS2xZilla screenshot 6"></td></tr>
  <tr><td colspan="2" align="center"><img src="https://raw.githubusercontent.com/blueshockz47/ounceshell-pro/master/IMG_8087.jpeg" width="50%" alt="NS2xZilla screenshot 7"></td></tr>
</table>

## Quick start

1. Download and extract the ZIP.
2. Copy `NS2xZilla.nro` into the `switch` directory on your SD card.
3. Launch it through your existing compatible Switch 2 homebrew environment.
4. Browse files using touch, D-pad or either joystick.
5. For FTP, press Minus and enter the displayed IP address and port in your FTP client.

NS2xZilla does not install a homebrew environment or provide an exploit. Back up important files before making changes.

## Controls

| Button | Action |
| --- | --- |
| A / B | Open or confirm / Back or cancel |
| D-pad / either joystick | Navigate |
| R | Select / mark files |
| L | More actions |
| X / Y | Copy / Paste |
| + | Toggle grid and list |
| − | Start or stop FTP |
| R3 | Settings |
| L3 | Return to SD card root (`sdmc:/`) |
| ZL / ZR | Rotate screen |

Touch the on-screen controls to perform the corresponding actions, except Grid/List and Rotate, which are controller-only. Drag the scrollbar or swipe to scroll with momentum. Portrait mode includes a custom staggered QWERTY keyboard. Tap a file to focus it, double-tap to open, or long-press for more actions.

## SwitchNest integration

Use NS2xZilla as the console-side FTP server with a computer or phone on the same trusted local network. SwitchNest can provide client-side file management and homebrew transfers. See the [integration notes](https://github.com/blueshockz47/ounceshell-pro/blob/development/docs/SWITCHNEST-INTEGRATION.md).

**Security:** FTP is unencrypted. Use a trusted isolated network; do not expose the server to the public internet. NS2xZilla itself does not block Nintendo services or guarantee protection from network-related risks.

## Build from source

Use [ounce-devkit](https://github.com/ouncebrew/ounce-devkit) and the development branch:

```sh
git clone -b development https://github.com/blueshockz47/ounceshell-pro.git
cd ounceshell-pro
make -f switch2/Makefile
```

Output: `build/switch2/NS2xZilla.nro`.

[Port documentation](https://github.com/blueshockz47/ounceshell-pro/blob/development/switch2/PORT.md) · [RC testing notes](https://github.com/blueshockz47/ounceshell-pro/blob/development/docs/RC-TESTS.md) · [Report an issue](https://github.com/blueshockz47/ounceshell-pro/issues)

## Credits

- **TheFloW** — original [VitaShell](https://github.com/TheOfficialFloW/VitaShell).
- **kurumuz** — [OunceShell](https://github.com/kurumuz/ounceshell), the original Switch 2 port.
- **mtheall** — bundled ftpd server.
- **blueshockz47** — NS2xZilla fork, interface and FTP enhancements.
- Team Molecule, xerpi, wololo, sakya and the VitaSDK and ouncebrew contributors.

NS2xZilla is an independent, unofficial project, distributed under GPL-3.0. Original copyright notices and third-party licenses remain applicable. Not affiliated with Nintendo.

