// kbdlinks.h - the phone keyboard's links on the board (kbd.h): Bluetooth
// LE (kbdble.cpp, NimBLE) and USB (kbdusb.cpp, TinyUSB), built only where
// module.json says (the ESP32-S3 boards: CAST_KBD_BLE, CAST_KBD_USB). Each
// gives 0 where it is not built, so the plain Atom has no keyboard at all
// (WELCOME never offers one). USB only with PSRAM (the AtomS3R): linking
// TinyUSB's device stack costs about 20 KB of internal memory for good (the
// core's library has every class's buffers), too much for the AtomS3 Lite.
// MIT licence (see LICENSE at the top of the repository).
#ifndef ATOM_KBDLINKS_H
#define ATOM_KBDLINKS_H

#if defined(CAST_KBD_USB) && !defined(BOARD_HAS_PSRAM)
#undef CAST_KBD_USB
#endif

#include <stdint.h>
#include <stddef.h>

namespace am {

class Hal;
class KbdLink;

// "PsiConnect keyboard": what the phone's Bluetooth settings and USB show
extern const char kKbdName[];

KbdLink* KbdBleLink(Hal& aHal);
KbdLink* KbdUsbLink(Hal& aHal);
// USB: whether the USB-C was made a host at start (AT$USB=1: then there is
// no USB keyboard until a restart with AT$USB=0)
void KbdUsbHostAtStart(bool aHost);

} // namespace am

#endif
