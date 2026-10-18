// kbdmap.cpp - see kbdmap.h. MIT licence (see LICENSE at the top of the repository).
#include "kbdmap.h"
#include <string.h>

namespace am {

namespace {

const uint8_t S = kKbdShift, A = kKbdAlt, AS = kKbdAlt | kKbdShift;
enum { KB = 1, KU = 2, KBoth = 3 };          // which layouts an entry is for

// the ASCII punctuation on a US keyboard (letters and digits are worked out)
const char kPunct[] = " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
const uint8_t kPunctUsage[] =
	{
	0x2c, 0x1e, 0x34, 0x20, 0x21, 0x22, 0x24, 0x34, 0x26, 0x27, 0x25, 0x2e, 0x36, 0x2d, 0x37, 0x38,
	0x33, 0x33, 0x36, 0x2e, 0x37, 0x38, 0x1f, 0x2f, 0x31, 0x30, 0x23, 0x2d, 0x35, 0x2f, 0x31, 0x30, 0x35
	};
const uint8_t kPunctShift[] =
	{
	0, S, S, S, S, S, S, 0, S, S, S, S, 0, 0, 0, 0,
	S, 0, S, 0, S, S, S, 0, 0, 0, S, S, 0, S, S, S, S
	};
static_assert(sizeof(kPunct) - 1 == sizeof(kPunctUsage) && sizeof(kPunctUsage) == sizeof(kPunctShift), "kPunct tables");

// the keys that differ between the layouts, and Option's characters. The
// first match wins, so a layout's own entry comes before a shared one
struct Extra { uint8_t code, mods, usage, layouts; };
const Extra kExtras[] =
	{
	// British: £ and # change places with US's, as do € and ™; ` and ~ are on
	// the ISO key beside the left Shift
	{ 0x23, A, 0x20, KB },     // #  Option+3
	{ 0xa3, S, 0x20, KB },     // £  Shift+3
	{ 0xa3, A, 0x20, KU },     // £  Option+3
	{ 0x60, 0, 0x64, KB },     // `
	{ 0x7e, S, 0x64, KB },     // ~
	{ 0x80, A, 0x1f, KB },     // €  Option+2
	{ 0x80, AS, 0x1f, KU },    // €  Option+Shift+2
	{ 0x99, AS, 0x1f, KB },    // ™  Option+Shift+2
	{ 0x99, A, 0x1f, KU },     // ™  Option+2
	// Option (both layouts)
	{ 0xa1, A, 0x1e, KBoth },  // ¡  1
	{ 0xa2, A, 0x21, KBoth },  // ¢  4
	{ 0xa7, A, 0x23, KBoth },  // §  6
	{ 0xb6, A, 0x24, KBoth },  // ¶  7
	{ 0x95, A, 0x25, KBoth },  // •  8
	{ 0xaa, A, 0x26, KBoth },  // ª  9
	{ 0xba, A, 0x27, KBoth },  // º  0
	{ 0x96, A, 0x2d, KBoth },  // –  -
	{ 0x9c, A, 0x14, KBoth },  // œ  q
	{ 0xae, A, 0x15, KBoth },  // ®  r
	{ 0x86, A, 0x17, KBoth },  // †  t
	{ 0xa5, A, 0x1c, KBoth },  // ¥  y
	{ 0xf8, A, 0x12, KBoth },  // ø  o
	{ 0x93, A, 0x2f, KBoth },  // “  [
	{ 0x91, A, 0x30, KBoth },  // ‘  ]
	{ 0xab, A, 0x31, KBoth },  // «  backslash
	{ 0xe5, A, 0x04, KBoth },  // å  a
	{ 0xdf, A, 0x16, KBoth },  // ß  s
	{ 0x83, A, 0x09, KBoth },  // ƒ  f
	{ 0xa9, A, 0x0a, KBoth },  // ©  g
	{ 0xac, A, 0x0f, KBoth },  // ¬  l
	{ 0x85, A, 0x33, KBoth },  // …  ;
	{ 0xe6, A, 0x34, KBoth },  // æ  '
	{ 0xe7, A, 0x06, KBoth },  // ç  c
	{ 0xb5, A, 0x10, KBoth },  // µ  m
	{ 0xf7, A, 0x38, KBoth },  // ÷  /
	{ 0xa0, A, 0x2c, KBoth },  // no-break space
	// Option+Shift (both layouts)
	{ 0x8b, AS, 0x20, KBoth }, // ‹  3
	{ 0x9b, AS, 0x21, KBoth }, // ›  4
	{ 0x87, AS, 0x24, KBoth }, // ‡  7
	{ 0xb0, AS, 0x25, KBoth }, // °  8
	{ 0xb7, AS, 0x26, KBoth }, // ·  9
	{ 0x82, AS, 0x27, KBoth }, // ‚  0
	{ 0x97, AS, 0x2d, KBoth }, // —  -
	{ 0xb1, AS, 0x2e, KBoth }, // ±  =
	{ 0x8c, AS, 0x14, KBoth }, // Œ  q
	{ 0x84, AS, 0x1a, KBoth }, // „  w
	{ 0xb4, AS, 0x08, KBoth }, // ´  e
	{ 0x89, AS, 0x15, KBoth }, // ‰  r
	{ 0xa8, AS, 0x18, KBoth }, // ¨  u
	{ 0x88, AS, 0x0c, KBoth }, // ˆ  i
	{ 0xd8, AS, 0x12, KBoth }, // Ø  o
	{ 0x94, AS, 0x2f, KBoth }, // ”  [
	{ 0x92, AS, 0x30, KBoth }, // ’  ]
	{ 0xbb, AS, 0x31, KBoth }, // »  backslash
	{ 0xc5, AS, 0x04, KBoth }, // Å  a
	{ 0xc6, AS, 0x34, KBoth }, // Æ  '
	{ 0xc7, AS, 0x06, KBoth }, // Ç  c
	{ 0xb8, AS, 0x1d, KBoth }, // ¸  z
	{ 0xaf, AS, 0x36, KBoth }, // ¯  ,
	{ 0x98, AS, 0x11, KBoth }, // ˜  n
	{ 0xbf, AS, 0x38, KBoth }, // ¿  /
	};

// the dead keys (Option and a key), then the letters each one makes
struct Dead { uint8_t usageUk, usageUs; const char* letters; const uint8_t* made; };
const uint8_t kGrave[] = { 0xe0, 0xc0, 0xe8, 0xc8, 0xec, 0xcc, 0xf2, 0xd2, 0xf9, 0xd9 };
const uint8_t kAcute[] = { 0xe1, 0xc1, 0xe9, 0xc9, 0xed, 0xcd, 0xf3, 0xd3, 0xfa, 0xda, 0xfd, 0xdd };
const uint8_t kCirc[] = { 0xe2, 0xc2, 0xea, 0xca, 0xee, 0xce, 0xf4, 0xd4, 0xfb, 0xdb };
const uint8_t kTilde[] = { 0xe3, 0xc3, 0xf1, 0xd1, 0xf5, 0xd5 };
const uint8_t kUml[] = { 0xe4, 0xc4, 0xeb, 0xcb, 0xef, 0xcf, 0xf6, 0xd6, 0xfc, 0xdc, 0xff, 0x9f };
const Dead kDead[] =
	{
	{ 0x64, 0x35, "aAeEiIoOuU", kGrave },        // Option+`  (British: the ISO key)
	{ 0x08, 0x08, "aAeEiIoOuUyY", kAcute },      // Option+E
	{ 0x0c, 0x0c, "aAeEiIoOuU", kCirc },         // Option+I
	{ 0x11, 0x11, "aAnNoO", kTilde },            // Option+N
	{ 0x18, 0x18, "aAeEiIoOuUyY", kUml },        // Option+U
	};

// ASCII, as on a US keyboard
bool Ascii(unsigned aCode, KbdStroke& aOut)
	{
	if (aCode >= 'a' && aCode <= 'z')
		aOut = KbdStroke{ 0, (uint8_t)(0x04 + aCode - 'a') };
	else if (aCode >= 'A' && aCode <= 'Z')
		aOut = KbdStroke{ S, (uint8_t)(0x04 + aCode - 'A') };
	else if (aCode >= '1' && aCode <= '9')
		aOut = KbdStroke{ 0, (uint8_t)(0x1e + aCode - '1') };
	else if (aCode == '0')
		aOut = KbdStroke{ 0, 0x27 };
	else
		{
		const char* p = aCode ? strchr(kPunct, (int)aCode) : 0;
		if (!p)
			return false;
		size_t i = (size_t)(p - kPunct);
		aOut = KbdStroke{ kPunctShift[i], kPunctUsage[i] };
		}
	return true;
	}

} // namespace

int KbdStrokes(int aLayout, unsigned aCode, KbdStroke aOut[2])
	{
	if (aCode < 0x20 || aCode > 0xff || aCode == 0x7f)
		return 0;                                // (control codes: KEY is for those)
	int mask = aLayout == EKbdUs ? KU : KB;
	for (const Extra& e : kExtras)
		if (e.code == aCode && (e.layouts & mask))
			{
			aOut[0] = KbdStroke{ e.mods, e.usage };
			return 1;
			}
	if (Ascii(aCode, aOut[0]))
		return 1;
	for (const Dead& d : kDead)
		for (size_t i = 0; d.letters[i]; i++)
			if (d.made[i] == aCode)
				{
				aOut[0] = KbdStroke{ A, aLayout == EKbdUs ? d.usageUs : d.usageUk };
				Ascii((unsigned)(unsigned char)d.letters[i], aOut[1]);
				return 2;
				}
	return 0;
	}

} // namespace am
