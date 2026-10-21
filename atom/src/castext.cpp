// castext.cpp - see castext.h. MIT licence (see LICENSE at the top of the repository).
#include "castext.h"
#include "kbdext.h"
#include "schema.h"
#include "modem.h"
#include "castpage.h"
#include "casticons.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

namespace am {

CastExt& TheCastExt()
	{
	static CastExt sExt;
	return sExt;
	}

static ExtRegister gRegister(TheCastExt());

static const int kCastByte = 0;            // Settings2::ext[0]: AT$CAST, inverted

static const SchemaEntry kCastSchema[] =
	{
	{ "CAST", STBoolInv, SR2, (uint16_t)(offsetof(Settings2, ext) + kCastByte), 1, 0, 1, 0, SANone, SGExt, "PsiCast",
	  "1: PsiCast on the Psion can show its screen in a browser, at /screen on these pages (port 5962), and let the viewer tap and type if the Psion allows it" },
	};

const SchemaEntry* CastExt::Schema(int& aCount) const
	{
	aCount = (int)(sizeof(kCastSchema) / sizeof(kCastSchema[0]));
	return kCastSchema;
	}

bool CastExt::Begin(Hal& aHal)
	{
	if (!iTakePart)
		return false;
	iHal = &aHal;
	if (!iCast)
		iCast = new Cast(aHal);              // (small until used: the screen and buffers come with the Psion)
	KbdJoin();                                // (the phone keyboard, if it has begun)
	return true;
	}

// PsiCast follows its setting (AT$CAST, the pages, ATZ, AT&F: whichever changed it)
void CastExt::Apply(const Settings2& aS2)
	{
	bool on = aS2.ext[kCastByte] == 0;
	if (iCast->Enabled() != on)
		iCast->SetEnabled(on);
	}

// its timers, the viewers' WebSocket (port 5962) and, during a PPP call, the
// Psion's link on 192.168.7.1:5961 (the modem route's is in Modem::Loop).
// Non-blocking, a bounded share of each pass
void CastExt::Loop(const char* aPppAddr, const char* aListenAddr)
	{
	iCast->Tick();
	if (!CastLcd::Wanted(*iCast) && iHal->Millis() - iLcdDrawnMs >= 2000)
		iLcd.Free();                          // (the page's picture: only while it is up)
	iNet.Loop(*iCast, *iHal, iCast->Enabled(), aPppAddr, strcmp(aListenAddr, "127.0.0.1") == 0);
	}

bool CastExt::CallRefused(uint16_t aPort, char* aWhy, size_t aMax)
	{
	(void)aPort;
	if (iCast->Enabled())
		return false;
	snprintf(aWhy, aMax, "PsiCast: dialled, but it is off (AT$CAST=1)");
	return true;
	}

void CastExt::StatLine(int aIndex, char* aOut, size_t aMax) const
	{
	if (aIndex == 0)
		snprintf(aOut, aMax, "CAST=%s", !iCast->Enabled() ? "OFF" : iCast->LinkUp() ? "SHARING" : "WAIT");
	else
		snprintf(aOut, aMax, "CASTV=%d", iCast->Viewers());
	}

bool CastExt::CallPageRow(bool aIdle, char* aOut, size_t aMax) const
	{
	if (aIdle)
		{
		snprintf(aOut, aMax, "%s", iCast->Enabled() ? "On, no Psion" : "Off");
		return true;
		}
	if (!iCast->LinkUp())
		return false;
	snprintf(aOut, aMax, "%d watching", iCast->Viewers());
	return true;
	}

// ----- the LCD and the LED -------------------------------------------------------------

void CastExt::ScreenDraw(ExtPen& aPen, uint32_t aNowMs)
	{
	CastLcdView v;
	iLcdDrawnMs = iHal ? iHal->Millis() : aNowMs;
	iLcd.View(*iCast, aNowMs, v);
	CastLcdDraw(aPen, v);
	}

// a viewer coming or going, the Psion arriving, control allowed, given or taken,
// and the phone keyboard's state wake a dimmed screen
uint32_t CastExt::ScreenNews() const
	{
	return (uint32_t)iCast->KbdState() << 12 | (uint32_t)iCast->Viewers() << 2 | (iCast->LinkUp() ? 2u : 0u) | (iCast->Controlled() ? 1u : 0u)
		| (iCast->LinkUp() && iCast->ControlAllowed() ? 1u << 11 : 0u);
	}

bool CastExt::LedWanted(ExtLed& aOut) const
	{
	return iCast && CastLedFor(iCast->Enabled(), iCast->Viewers(), iCast->Controlled(), iCast->KbdState(), aOut);
	}

// ----- the web pages ------------------------------------------------------------------
// The viewer (behind the password), its token, and the Home Screen files
// (not secret, and fetched without the password by iOS); the Series 7 and
// Modern themes' colour pictures (and their matcher), fetched only when one
// is chosen; the 5mx's face for full screen
static const ExtRoute kRoutes[] =
	{
	{ "/screen", false },
	{ "/screen/token", false },
	{ "/screen/manifest.json", true },
	{ "/screen/icon.png", true },
	{ "/screen/icon-512.png", true },
	{ "/screen/iconmatch.js", false },
	{ "/screen/s7icons.json", false },
	{ "/screen/modicons.json", false },
	{ "/screen/silk5mx.jpg", false },
	{ 0, false }
	};

const ExtRoute* CastExt::Routes() const { return kRoutes; }

// a gzipped file of the viewer's (a pack this build was made without: 404)
static void Asset(ExtReply& aR, const char* aType, const unsigned char* aData, unsigned aLen)
	{
	if (aLen)
		aR.Data(200, aType, aData, aLen, true, "max-age=604800");
	}

void CastExt::Get(const char* aPath, const ExtQuery& aQuery, ExtReply& aR)
	{
	if (strcmp(aPath, "/screen") == 0)
		aR.Data(200, "text/html; charset=utf-8", kCastPageGz, kCastPageGzLen, true, "no-cache");   // (viewer.html, tools/mkcastpage.py)
	else if (strcmp(aPath, "/screen/token") == 0)
		{
		// {"t":"<token>","port":5962,"control":true}: single use, for 60 s; ?control=1 asks for control
		bool control = strcmp(aQuery.Arg("control"), "1") == 0;
		// refused: 409 while the Psion is typing into a phone (keyboard mode
		// and sharing are one or the other: cast.h), 403 while PsiCast is off
		char t[32];
		if (iCast->Typing())
			{
			aR.Text(409, "application/json", "{\"error\":\"The Psion is typing into a phone\",\"why\":\"keyboard\"}", "no-store");
			return;
			}
		if (!iCast->NewToken(control, t, sizeof(t)))
			{
			aR.Text(403, "application/json", "{\"error\":\"PsiCast is off\",\"why\":\"off\"}", "no-store");
			return;
			}
		char m[96];
		snprintf(m, sizeof(m), "{\"t\":\"%s\",\"port\":%u,\"control\":%s}", t, (unsigned)iNet.iViewPort, control ? "true" : "false");
		aR.Text(200, "application/json", m, "no-store");
		}
	else if (strcmp(aPath, "/screen/manifest.json") == 0)
		aR.Data(200, "application/manifest+json", kCastManifest, sizeof(kCastManifest) - 1, false, "max-age=86400");
	else if (strcmp(aPath, "/screen/icon.png") == 0)
		aR.Data(200, "image/png", kCastIcon180, kCastIcon180Len, false, "max-age=86400");
	else if (strcmp(aPath, "/screen/icon-512.png") == 0)
		aR.Data(200, "image/png", kCastIcon512, kCastIcon512Len, false, "max-age=86400");
	else if (strcmp(aPath, "/screen/iconmatch.js") == 0)
		Asset(aR, "text/javascript", kCastIconMatchGz, kCastIconMatchGzLen);
	else if (strcmp(aPath, "/screen/s7icons.json") == 0)
		Asset(aR, "application/json", kCastS7IconsGz, kCastS7IconsGzLen);
	else if (strcmp(aPath, "/screen/modicons.json") == 0)
		Asset(aR, "application/json", kCastModIconsGz, kCastModIconsGzLen);
	else if (strcmp(aPath, "/screen/silk5mx.jpg") == 0)
		aR.Data(200, "image/jpeg", kCastSilk5mx, kCastSilk5mxLen, false, "max-age=604800");   // (not gzipped)
	}

} // namespace am
