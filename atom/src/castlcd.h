// castlcd.h - PsiCast on the Atom's LCD and LED (the firmware's hooks in
// ext.h). The AtomS3R's screen has a Sharing page: one of the button's pages
// (the next after Home), and shown over Home while anyone is watching or the
// Psion is in keyboard mode, going back to the modem's pages after.
//   - Sharing: a red dot while the Psion is live, how many are watching, a
//     clear line while a viewer has control (can tap and type on the
//     Psion), and the picture delay when it is known (the figure changed at
//     most every 5 s, so it does not keep redrawing the screen).
//   - Not sharing: "Not sharing", the viewer's address to open on the phone
//     once the Psion shares, and the phone keyboard's setting; or that
//     PsiCast is off.
//   - Keyboard mode (kbd.h): a page of its own while the Psion types into
//     the phone, watched or not: a blue "Keyboard" band, a keyboard drawn in
//     cyan while it types (grey while it waits for the phone), "Typing on"
//     the phone or "Waiting for the phone", and at the foot how
//     many watch or how to stop.
// The picture is always the same: the 5mx's System screen, made once from a
// real one (tools/mkhomepic.py, casthome.h). A live copy of the Psion's
// screen at this size could not be read, and would only cost redraws: the
// page is drawn again only when its words change.
// On the boards with only the WS2812, a slow cyan pulse while sharing, a
// quick cyan blink while a viewer has control, a slow blink while the
// keyboard waits for the phone and a steady cyan while it types into it
// (cyan is none of the modem's colours; the modem's no Wi-Fi, access point
// and updates win). Plain C++, no Arduino calls.
// MIT licence (see LICENSE at the top of the repository).
#ifndef ATOM_CASTLCD_H
#define ATOM_CASTLCD_H

#include <stdint.h>
#include <stddef.h>

namespace am {

class ExtPen;
class Cast;
struct ExtLed;

// the picture's box on the page
static const int kCastPicW = 128;
static const int kCastPicH = 48;

// what the page shows
struct CastLcdView
	{
	bool enabled;               // PsiCast is on
	bool live;                  // the Psion is sharing (else waiting for it)
	bool paused;                // ... but has paused
	int viewers;
	bool control;               // a viewer can tap and type on the Psion
	bool haveDelay;
	uint32_t delayMs;           // the picture delay, Psion -> Atom (held for kDelayMs)
	int kbd;                    // the phone keyboard (TKbdState: shown when 2 or 3)
	char kbdName[24];           // the phone's name, if known
	int kbdVia;                 // its setting while not in use: TKbdVia, 0 off; -1 none built in
	char address[64];           // where to watch ("192.168.1.50/screen"), if known
	const uint8_t* pic;         // the System screen, kCastPicW x kCastPicH greys (0: no memory)
	};
void CastLcdDraw(ExtPen& aPen, const CastLcdView& aView);

// the light: false while nobody is watching and the keyboard is not in use
// (aKbd: a TKbdState)
bool CastLedFor(bool aEnabled, int aViewers, bool aControl, int aKbd, ExtLed& aOut);

// the page's state: its picture (unpacked from flash while the page is up)
// and the delay it shows
class CastLcd
	{
public:
	CastLcd() {}
	~CastLcd() { Free(); }
	// the page is wanted over Home: PsiCast on, and someone watching or the
	// Psion in keyboard mode
	static bool Wanted(const Cast& aCast);
	void View(const Cast& aCast, uint32_t aNowMs, CastLcdView& aOut);
	void Free();                // (the page is gone: its picture's memory too)
	static const uint32_t kDelayMs = 5000;
private:
	uint8_t* iPic = 0;          // kCastPicW x kCastPicH bytes, while the page is up
	bool iHaveDelay = false;
	uint32_t iDelay = 0, iDelayAtMs = 0;
	};

} // namespace am

#endif
