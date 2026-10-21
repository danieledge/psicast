// castlcd.cpp - see castlcd.h. MIT licence (see LICENSE at the top of the repository).
#include "castlcd.h"
#include "cast.h"
#include "kbd.h"
#include "casthome.h"
#include "status.h"
#include "ext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace am {

static_assert(kCastHomeW == kCastPicW && kCastHomeH == kCastPicH, "casthome.h is the picture's size");

// ----- the page ------------------------------------------------------------------------

// keyboard mode: a page of its own, whoever watches. A blue band
// ("Keyboard", with a green dot once the phone is connected, amber while
// waiting for it), a keyboard drawn in cyan (the LED's colour) while it
// types and grey while it waits, then what is happening and, at the foot,
// how many watch or how to stop
static void DrawKbd(ExtPen& aPen, const CastLcdView& aV)
	{
	const int kW = ExtPen::kSize, kM = ExtPen::kMargin, kBand = ExtPen::kBandH;
	const uint16_t white = aPen.Tone(EToneWhite), grey = aPen.Tone(EToneGrey), amber = aPen.Tone(EToneAmber);
	const bool typing = aV.kbd == EKbdTyping;
	aPen.Fill(0, 0, kW, kBand, Rgb565(30, 80, 170));
	aPen.Dot(kM + 4, kBand / 2, 5, typing ? Rgb565(70, 210, 110) : amber);
	aPen.Text(EFaceBold, ExtPen::kText + 1, kM + 15, (kBand + ExtPen::kText + 1) / 2, white, "Keyboard", kW - kM - 15 - 2);
	// the keyboard: three rows of keys and a space bar
	const int kX = 14, kY = kBand + 7, kBw = kW - 2 * kX, kBh = 40;
	const uint16_t key = typing ? Rgb565(120, 220, 230) : Rgb565(150, 156, 166);
	aPen.RoundBox(kX, kY, kBw, kBh, 5, Rgb565(52, 60, 74));
	for (int r = 0; r < 3; r++)
		{
		int n = 9 - r, x0 = kX + 5 + r * 4 + (r == 2 ? 4 : 0);
		for (int i = 0; i < n; i++)
			aPen.Fill(x0 + i * 10, kY + 5 + r * 9, 8, 7, key);
		}
	aPen.Fill(kX + 25, kY + 32, kBw - 50, 5, key);
	// what is happening
	const int kRow1 = kY + kBh + 18, kRow2 = kRow1 + 15, kFoot = kW - 6;
	const bool usb = aV.kbdVia == EKbdViaUsb;
	char t[48];
	if (typing)
		{
		aPen.Text(EFaceBold, ExtPen::kText + 1, kM, kRow1, white, "Typing on", kW - 2 * kM);
		aPen.Text(EFaceSans, ExtPen::kText, kM, kRow2, white, aV.kbdName[0] ? aV.kbdName : usb ? "the phone (USB)" : "the phone", kW - 2 * kM);
		}
	else
		{
		// (the Psion's screen says how to pair: there is room for it there)
		aPen.Text(EFaceBold, ExtPen::kText + 1, kM, kRow1, amber, "Waiting", kW - 2 * kM);
		aPen.Text(EFaceSans, ExtPen::kText, kM, kRow2, grey, usb ? "for the USB cable" : "for the phone", kW - 2 * kM);
		}
	if (aV.viewers > 0)
		snprintf(t, sizeof(t), "%d watching", aV.viewers);
	else
		snprintf(t, sizeof(t), "Shift+Ctrl+B stops");
	aPen.Centre(EFaceSans, aPen.FitSize(EFaceSans, t, kW - 2 * kM, ExtPen::kText, ExtPen::kSmall), kFoot, grey, t);
	}

void CastLcdDraw(ExtPen& aPen, const CastLcdView& aV)
	{
	if (aV.enabled && (aV.kbd == EKbdWaiting || aV.kbd == EKbdTyping))
		{
		DrawKbd(aPen, aV);
		return;
		}
	const int kW = ExtPen::kSize, kM = ExtPen::kMargin, kBand = ExtPen::kBandH;
	const uint16_t white = aPen.Tone(EToneWhite), grey = aPen.Tone(EToneGrey), amber = aPen.Tone(EToneAmber);
	const uint16_t kBlack = 0;
	// the band: a red dot (live; grey while waiting) and "Sharing"
	aPen.Fill(0, 0, kW, kBand, Rgb565(55, 65, 85));
	aPen.Dot(kM + 4, kBand / 2, 5, aV.live ? Rgb565(250, 60, 50) : grey);
	aPen.Text(EFaceBold, ExtPen::kText + 1, kM + 15, (kBand + ExtPen::kText + 1) / 2, white, "Sharing",
		kW - kM - 15 - 2);
	// the System screen under the band (always the same picture)
	const int kPicY = kBand;
	if (aV.pic)
		aPen.Picture(0, kPicY, kCastPicW, kCastPicH, aV.pic, 1);
	else
		aPen.Fill(0, kPicY, kW, kCastPicH, Rgb565(32, 32, 36));
	const int kRow1 = kPicY + kCastPicH + 16, kRow2 = kRow1 + 16;
	const int kCtlY = kRow2 + 5, kCtlH = 20, kFoot = kCtlY + (kCtlH + ExtPen::kText) / 2;
	char t[48];
	if (!aV.enabled)
		{
		aPen.Text(EFaceBold, ExtPen::kText + 1, kM, kRow1, amber, "PsiCast is off", kW - 2 * kM);
		aPen.Text(EFaceSans, ExtPen::kText, kM, kRow2, grey, "On with AT$CAST=1", kW - 2 * kM);
		return;
		}
	if (!aV.live)
		{
		// not sharing: where to watch, once the Psion shares; and the keyboard
		aPen.Text(EFaceBold, ExtPen::kText + 1, kM, kRow1, white, "Not sharing", kW - 2 * kM);
		const char* at = aV.address[0] ? aV.address : "No address yet";
		int as = aPen.FitSize(EFaceSans, at, kW - 2 * kM, ExtPen::kText, ExtPen::kSmall);
		aPen.Text(EFaceSans, as, kM, kRow2, grey, at, kW - 2 * kM);
		if (aV.kbdVia >= 0)
			{
			snprintf(t, sizeof(t), "Keyboard: %s", aV.kbdVia == EKbdViaUsb ? "USB" : aV.kbdVia == EKbdViaBt ? "Bluetooth" : "off");
			aPen.Centre(EFaceSans, aPen.FitSize(EFaceSans, t, kW - 2 * kM, ExtPen::kText, ExtPen::kSmall), kFoot, grey, t);
			}
		return;
		}
	// how many are watching, then the picture delay as the pages' rows are
	// (a grey label, the value at the right), or that the Psion has paused;
	snprintf(t, sizeof(t), "%d watching", aV.viewers);
	aPen.Text(EFaceBold, ExtPen::kText + 1, kM, kRow1, white, t, kW - 2 * kM);
	if (aV.paused)
		aPen.Text(EFaceSans, ExtPen::kText, kM, kRow2, amber, "Paused", kW - 2 * kM);
	else if (aV.haveDelay)
		{
		if (aV.delayMs < 10000)
			snprintf(t, sizeof(t), "%lu ms", (unsigned long)aV.delayMs);
		else
			snprintf(t, sizeof(t), "%lu s", (unsigned long)(aV.delayMs / 1000));
		aPen.Text(EFaceSans, ExtPen::kText, kM, kRow2, grey, "Delay");
		aPen.Right(EFaceSans, ExtPen::kText, kW - kM, kRow2, white, t);
		}
	// control: a clear amber line while someone can tap and type on the Psion
	if (aV.control)
		{
		aPen.RoundBox(3, kCtlY, kW - 6, kCtlH, 5, amber);
		aPen.Centre(EFaceBold, ExtPen::kText + 1, kCtlY + (kCtlH + ExtPen::kText + 1) / 2, kBlack, "Control: on");
		}
	else if (aV.allowed)
		aPen.Centre(EFaceSans, ExtPen::kText, kFoot, white, "Control: allowed");
	else
		aPen.Centre(EFaceSans, ExtPen::kText, kFoot, grey, "Control: off");
	}

// ----- the light -----------------------------------------------------------------------

bool CastLedFor(bool aEnabled, int aViewers, bool aControl, int aKbd, ExtLed& aOut)
	{
	if (!aEnabled || (aViewers <= 0 && aKbd != EKbdWaiting && aKbd != EKbdTyping))
		return false;
	// over the call's blue (sharing always comes with a call to the Atom),
	// never over no Wi-Fi, the access point or dialling
	if (aControl && aViewers > 0)
		aOut = ExtLed{ 0, 28, 28, EExtLedBlink, 400, EExtLedOverCall };    // quick: someone can type
	else if (aKbd == EKbdWaiting)
		aOut = ExtLed{ 0, 28, 28, EExtLedBlink, 1600, EExtLedOverCall };   // slow: pair or connect the phone
	else if (aKbd == EKbdTyping)
		aOut = ExtLed{ 0, 28, 28, EExtLedSteady, 0, EExtLedOverCall };     // steady: typing into the phone
	else
		aOut = ExtLed{ 0, 28, 28, EExtLedPulse, 2400, EExtLedOverCall };   // slow breathing: being watched
	return true;
	}

// ----- the state -----------------------------------------------------------------------

bool CastLcd::Wanted(const Cast& aCast)
	{
	int k = aCast.KbdState();
	// (a viewer waiting with the Psion not sharing, such as a phone left on the
	// viewer, does not take the screen: only a share that is up does)
	return aCast.Enabled() && ((aCast.Viewers() > 0 && aCast.LinkUp()) || k == EKbdWaiting || k == EKbdTyping);
	}

void CastLcd::Free()
	{
	free(iPic);
	iPic = 0;
	}

void CastLcd::View(const Cast& aCast, uint32_t aNowMs, CastLcdView& aOut)
	{
	memset(&aOut, 0, sizeof(aOut));
	aOut.enabled = aCast.Enabled();
	aOut.live = aCast.LinkUp();
	aOut.paused = aCast.Paused();
	aOut.viewers = aCast.Viewers();
	aOut.control = aCast.Controlled();
	aOut.allowed = !aOut.control && aCast.LinkUp() && aCast.ControlAllowed();
	// the delay: a new figure at most every kDelayMs (each change redraws)
	uint32_t ms, most;
	if (!aCast.LinkDelay(ms, most))
		iHaveDelay = false;
	else if (!iHaveDelay || aNowMs - iDelayAtMs >= kDelayMs)
		{
		iDelay = ms;
		iDelayAtMs = aNowMs;
		iHaveDelay = true;
		}
	aOut.haveDelay = iHaveDelay;
	aOut.delayMs = iDelay;
	aOut.kbd = aCast.KbdState();
	PhoneKbd* k = aCast.Keyboard();
	aOut.kbdVia = !k ? -1 : !k->On() ? EKbdViaNone : k->ViaSetting();
	if (k && aOut.kbd == EKbdTyping)
		k->Name(aOut.kbdName, sizeof(aOut.kbdName));
	char url[64];
	aCast.ViewerUrl(url, sizeof(url));
	snprintf(aOut.address, sizeof(aOut.address), "%s", strncmp(url, "http://", 7) == 0 ? url + 7 : url);
	// the picture: unpacked once (4 bits a pixel to 0..255), kept while the page is up
	if (!iPic)
		{
		iPic = (uint8_t*)malloc((size_t)kCastPicW * kCastPicH);
		if (iPic)
			for (int i = 0; i < kCastPicW * kCastPicH; i++)
				iPic[i] = (uint8_t)(((kCastHome[i / 2] >> ((i & 1) * 4)) & 15) * 17);
		}
	aOut.pic = iPic;                            // (no memory: the page without it)
	}

} // namespace am
