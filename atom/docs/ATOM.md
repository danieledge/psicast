# PsiCast on the Atom

The Atom side of PsiCast: an extension module of the PsiConnect firmware
(`firmware/atom-modem` in the public PsiTerm repository; its hooks are in
`firmware/atom-modem/src/ext.h`). This directory is the module:

- `module.json`: its sources, include directories, defines and libraries
  (per chip: NimBLE-Arduino and the core's USB library on the ESP32-S3), for
  the firmware's `tools/ext.py`.
- `src/`: the service (`cast.cpp`), its sockets (`castnet.cpp`), the
  WebSocket (`websock.cpp`), the module on the firmware's hooks
  (`castext.cpp`: the `AT$CAST` setting, the call to `192.168.7.1:5961`,
  `ATI`/`AT$STAT?` lines, the LCD and web status rows, every `/screen`
  route), the LCD's Sharing page and the LED (`castlcd.cpp`, with its
  picture `casthome.h`), the viewer page (`viewer.html` -> `castpage.h`) and
  the icon packs (`casticons.h`, `casticons_local.h`); the phone keyboard
  (`kbd.cpp`, `kbdmap.cpp`, `kbdext.cpp`, `kbdble.cpp`, `kbdusb.cpp`).
- `tools/`: `mkcastpage.py`, `mkcasticons.py`, `modicons.py` (and
  `modicons/`), `s7icons.py`, `mkhomepic.py` (the LCD's picture, from
  `system5mx.png`).
- `hosttest/`: the tests, on the firmware's own harness.

## Building it in

The firmware builds it in when the module is linked into its `ext/`
directory (git-ignored there):

    ln -s ../../../cast/atom firmware/atom-modem/ext/cast
    cd firmware/atom-modem && pio run -e atoms3r     # "Extension module cast: ..." in the log

`AM_EXT=0 pio run ...` builds the plain firmware with the link in place, and
`AM_EXT_DIRS=/path/to/cast/atom` works without the link. The setting
`AT$CAST` is `Settings2::ext[0]` (offset 442, stored inverted), the same
byte it was when PsiCast was part of the firmware, so an Atom keeps its
setting. Without the module the firmware has no PsiCast at all: no setting,
no `CAST`/`CASTV` lines in `AT$STAT?`, no `/screen` pages, and a dial to
`192.168.7.1:5961` is an ordinary call.

## What it does

PsiCast on the Psion sends its screen to the Atom, and the Atom shows it,
live, in a web browser: an iPhone in Safari first, any modern browser in
fact. If the Psion allows it, the viewer can also tap and type on the
Psion. The protocol is in `cast/docs/PROTOCOL.md`; the code is `cast/atom/src/`.

- **The Psion's side** connects to `192.168.7.1:5961`, the Atom's own
  address: over the PPP link (an ordinary socket; other apps keep sharing the
  link), or on the modem route by dialling `ATDT192.168.7.1:5961`, which the
  Atom answers itself with no uplink needed (the line is then PsiCast's).
  Hang-up is `+++` and `ATH` as for any call. With PsiCast off the dial gives
  `NO CARRIER`; any other address or port is an ordinary call, as before.
- **The viewer** is the web page `http://<atom>/screen` (the *Screen* link on
  the pages, behind the web password). It asks `/screen/token` for a token
  (single use, for 60 s) and opens a WebSocket on port **5962** with it. Up
  to four browsers can watch at once; each is sent only what changed, at its
  own pace, so a slow phone never slows the Psion or the other viewers. The
  page has nine looks (Paper, 5mx LCD, Amber, Green screen, Dark, Series 7,
  Modern, Warm and Mint; the Series 7 and Modern ones colour the Psion's
  controls and icons),
  screenshots at the exact 640 x 240 or three times that, video recording
  (MP4 on an iPhone) with the taps and keys drawn on it if wanted, a full
  screen view, and a status line: updates a second, data, latency, the
  Psion's battery and free memory. It reconnects by itself. **Add to Home
  Screen** makes it an app.
- **Control** (the hand button): taps on the picture are pen taps on the
  Psion, the phone's keyboard types (the Psion finds the keys for each
  character), and a key bar has Menu, Esc, Tab, Ctrl, Shift and Fn (a tap
  holds one for the next key, a second locks it), Del, Enter, the arrows,
  Home/End/PgUp/PgDn (as Fn with an arrow) and the 5mx's silkscreen icons.
  Input is passed on only when the viewer asked for control *and* the Psion
  has "Allow control from the viewer" on. The silkscreen positions (System,
  Word, Sheet, Contacts, Agenda, Email, Calc, Jotter, Extras below the
  screen; Clipboard, Infrared, Zoom in and out to its left) are placed in
  the digitiser's margins as the Psion reports them (DEVINFO: 695 x 280 on
  the 5mx), along the 5mx's layout; they are still to be checked on a real one.
- **Memory**: about 90 KB for the screen copy and its decoder, and 64 KB for
  link A's buffer, taken when the Psion connects (PSRAM on the AtomS3R).
  A board without PSRAM takes a smaller buffer if it must, or refuses the
  connection and logs why, rather than leave WiFi and TLS short.
- `AT$CAST=0` turns it off (the web *Settings* page too). The LCD's Call
  page says whether PsiCast is on and how many are watching.
- **The LCD** (AtomS3R): the Sharing page is one of the button's pages (a
  press on Home goes to it, then Wi-Fi and VPN), and comes up over Home by
  itself while anyone is watching or the Psion is in keyboard mode. It shows
  how many are watching, control, the delay (a new figure at most every
  5 s), the keyboard's state; when nothing is shared, the address to open on
  the phone and the keyboard's setting. Its picture is always the 5mx's
  System screen (`casthome.h`, 3 KB at 16 greys, made by
  `tools/mkhomepic.py`): a live copy at 128 x 48 could not be read, and
  would cost redraws. The page is drawn again only when its words change.

`AT$CAST=0/1`: PsiCast on or off (on as standard). `AT$STAT?` adds
`CAST=OFF/WAIT/SHARING` and `CASTV=` (viewers).

## The phone keyboard

The Psion's keyboard types into a phone, through the Atom (PROTOCOL.md, "The
phone keyboard"). In PsiCast's keyboard mode the Psion sends its keys over
link A (KEYS); the Atom is a keyboard to the phone, by Bluetooth LE or USB,
only between MODE 1 and MODE 0, because a phone with a keyboard connected
hides its own on-screen keyboard. It is a second module beside PsiCast's,
"Phone keyboard" (`kbdext.cpp`), with its own settings, web row and page.

- `AT$KBD=BT|USB|OFF` (`Settings2::ext[1]`; BT as standard) and
  `AT$KBDLAYOUT=UK|US` (`ext[2]`; UK as standard): the web *Settings* page
  has them under *Phone keyboard*. The layout must be the one chosen on the
  phone for a hardware keyboard (on an iPhone: Settings, General, Keyboard,
  Hardware Keyboard).
- `AT$STAT?` adds `KBD=OFF/READY/WAIT/TYPING`. The web status page has a
  *Phone keyboard* row; its page, `/keyboard`, shows the state and settings
  and has **Forget the paired phone** (its link carries a nonce made for
  that page, so no other page can do it).
- The characters (`kbdmap.cpp`): CP1252 from the Psion to the keys the
  phone's layout needs, the iPhone's (and Mac's) own British and US layouts:
  ASCII; British £ on Shift+3, # on Option+3, € on Option+2; Option's
  characters (¬, §, °, ±, ©, «», curly quotes, dashes, … and the rest);
  accented letters as a dead key and the letter (Option+E then e is é).
  What neither can make (¤, ¦, ×, the superscripts, the fractions, Þ ð, Š Ž)
  is dropped. Ctrl+letter is the letter with Ctrl.
- **Bluetooth** (`kbdble.cpp`, NimBLE-Arduino 2.5.1; the ESP32-S3 boards):
  "PsiConnect keyboard", appearance keyboard, a HID keyboard with Device
  Information (PnP ID) and Battery; bonded with LE Secure Connections ("just
  works"), the bond in NVS (NimBLE's `nimble_bond`: a factory reset of the
  modem does not clear it, *Forget* does). Reports go only once the link is
  encrypted and the phone has subscribed; after pairing it asks for a 15 to
  30 ms interval (latency 4, 4 s timeout). Bluetooth starts at the first
  MODE 1 (if 70 KB of internal memory is free) and stops 10 s after MODE 0;
  while it runs Wi-Fi keeps its modem sleep (`Hal::RadioShared`), which the
  two need to share the radio. Not on the plain Atom (classic ESP32): there
  the module has no keyboard and never offers one.
- **USB** (`kbdusb.cpp`, the AtomS3R only): see below.
- The LCD says "Typing on" the phone or "Waiting for" it; the LED (WS2812
  boards) blinks slowly cyan while waiting for the phone and is steady cyan
  while typing.

### USB: what it takes, and what it costs

The S3 has one USB PHY. The firmware's console is the USB Serial/JTAG
controller (`ARDUINO_USB_MODE=1`), and tethering (`AT$USB=1`) uses the OTG
controller as a host from start-up. A USB keyboard needs the OTG controller
as a *device* (TinyUSB). The core can start TinyUSB at any time with the
build as it is: `USB.begin()` moves the PHY to the OTG controller. So:

- `AT$KBD=USB` starts TinyUSB at the first MODE 1 (a HID keyboard,
  "PsiConnect keyboard"). From then until a restart the USB console is gone
  (the core cannot give the PHY back to the Serial/JTAG controller; the log
  is still in `AT$LOG?` and on the web pages), and flashing over USB needs a
  restart first, or the button held through a reset (the ROM's download
  mode). Web updates are not affected. MODE 0 detaches from the bus
  (`tud_disconnect`), so the phone sees the keyboard unplugged; MODE 1
  attaches again.
- Not with `AT$USB=1` (the USB-C was made a host at start): the keyboard is
  then "not available" until a restart with `AT$USB=0`.
- The cost: linking TinyUSB's device stack takes about 20 KB of internal RAM
  for good (the core's prebuilt library has the buffers of every class it
  was built with: NCM, MSC, DFU, CDC, MIDI, vendor), plus a 4 KB event task
  at start. So USB is built only for the AtomS3R (PSRAM); the AtomS3 Lite
  has Bluetooth only.
- Not tried on hardware. The open questions: whether a USB-C iPhone keeps
  powering the Atom while the keyboard is detached (if it cuts VBUS, the
  Atom restarts), and how much a Lightning camera adapter supplies with
  Wi-Fi on. Making USB the console's way back without a restart would need
  a core with a TinyUSB `end()` (it has none), or a build with
  `ARDUINO_USB_MODE=0` and the console on TinyUSB's CDC, which changes how
  the board is flashed and how tethering starts: not done.

## Testing

    make -C cast/atom/hosttest test      # test_cast, test_iconmatch.js, the Modern packs' sides
    make -C cast/atom/hosttest e2e       # test_cast_e2e.py
    make -C cast/atom/hosttest shots     # cast_page_shot.py (Playwright)

- `test_cast` (with the sanitizers) covers link A's framing (the
  CRC, resync after rubbish and a damaged message, a message too long for the
  buffer), the session against a Psion played with the real encoder (WELCOME
  with the viewer's address, an ACK for every update, KEYREQ after a damaged
  update, a missing one or one that does not decode, PING, the 20 s silence,
  BYE, a replaced connection), the tokens, the WebSocket handshake (RFC 6455's
  example) and frames, per-viewer tracking (a fast and a slow viewer through
  300 random changes and scrolls, both pixel-exact, the slow one sent far
  less), the input rules and the modem route. `make -C cast/atom/hosttest e2e`
  runs `test_cast_e2e.py`: hostmodem with `HOSTMODEM_CAST=1`, a fake Psion on
  both routes and Python WebSocket viewers, pixel for pixel.
  `make -C cast/atom/hosttest shots` (`cast_page_shot.py`) loads the real viewer page in
  headless Chromium (Playwright) at iPhone sizes against the same set-up,
  checks its canvas pixel for pixel and a tap reaching the Psion, and saves
  screenshots in `cast/atom/hosttest/screens/cast/`. `HOSTMODEM_CAST=1` also lets the
  Psion emulator's PsiCast use hostmodem (see `hosttest/casthost.cpp`).
- The keyboard's tests in `test_cast`: both layouts' tables (and every
  code: known keys only, no two characters on one stroke), KEYS (bad
  lengths, more than 64 events, unknown kinds, a busy link, a full queue
  that still lets go), the rules for letting go (link A closing, BYE, 20 s
  of silence, HOLD 0, MODE 0, the phone going), MODE's gating with a fake
  phone, KBD after WELCOME and on each change, no KEYREQ while a
  keyboard-only Psion is paused, and the settings, page, LCD and LED. The
  e2e test has a keyboard-only Psion typing into hostmodem's pretend phone.
- `hosttest/casthost.cpp` sets up PsiCast in hostmodem: `HOSTMODEM_CAST=1`
  makes it take part (and the keyboard, with a pretend phone that connects
  200 ms after MODE 1 and logs each report); `HOSTMODEM_CASTPORT` and `HOSTMODEM_CASTVIEW` move
  link A's TCP end and the WebSocket. hostmodem's own `HOSTMODEM_HTTP` (the
  pages, 5980) and `HOSTMODEM_ANY=1` (every address, for a phone on the LAN)
  apply too.
