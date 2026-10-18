// kbdmap.h - the phone keyboard's characters (PROTOCOL.md, "The phone
// keyboard"): a CP1252 character from the Psion as the keys a phone's
// hardware keyboard layout needs to make it. USB HID keyboard usages are
// places on the keyboard, not characters, so the same character takes
// different keys on different layouts; the phone's layout is a setting
// (AT$KBDLAYOUT). The tables are the iPhone's (and the Mac's) own layouts:
//   - US: ASCII as printed on a US keyboard; Option (Alt) makes the rest
//     (Option+3 is £, Option+Shift+2 is €, Option+L is ¬ and so on);
//   - British: as US, but Shift+3 is £, Option+3 is #, Option+2 is €, and
//     ` and ~ are on the key beside the left Shift (ISO's extra key).
// Accented letters are a dead key and the letter: Option+E then e is é,
// Option+U then u is ü; ç, å, æ, ø, ß and œ have keys of their own.
// A character neither layout can make (¤, ¦, ×, the superscripts, the
// fractions, the Icelandic and the caron letters) gives 0 strokes: dropped.
// Plain C++, no Arduino calls. MIT licence (see LICENSE at the top of the repository).
#ifndef ATOM_KBDMAP_H
#define ATOM_KBDMAP_H

#include <stdint.h>

namespace am {

// the USB HID modifier bits (the left-hand keys), as KEYS uses them
static const uint8_t kKbdCtrl = 0x01, kKbdShift = 0x02, kKbdAlt = 0x04, kKbdGui = 0x08;

enum TKbdLayout { EKbdBritish = 0, EKbdUs = 1 };

// one key pressed and let go, with these modifiers held
struct KbdStroke
	{
	uint8_t mods;
	uint8_t usage;              // a USB HID keyboard usage (page 7)
	};

// the strokes that type aCode (CP1252) on aLayout: 0 (it cannot be made),
// 1, or 2 (a dead key, then the letter)
int KbdStrokes(int aLayout, unsigned aCode, KbdStroke aOut[2]);

} // namespace am

#endif
