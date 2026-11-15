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
#include <stdlib.h>
#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <esp_partition.h>
#endif

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
// is chosen; the 5mx's face for full screen; the High-res shader and the
// sharp text's matcher and fonts, fetched only when those settings are on
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
	{ "/screen/xbrz.js", false },
	{ "/screen/sharp.js", false },
	{ "/screen/textmatch.js", false },
	{ "/screen/eonfonts.json", false },
	{ "/screen/pictures.json", false },
	{ 0, false }
	};

const ExtRoute* CastExt::Routes() const { return kRoutes; }

// (0.22) the picture pack: known programs' pictures redrawn (PsiTris's, Chess's,
// Monopoly's), for the viewer's Replace known pictures. Not in the firmware
// (the pictures are not ours to publish, and too big): uploaded to the Atom's
// spare data partition by POST /pack (the board's web pages), "PCPK", the
// length, then the gzipped JSON (tools/mkpicpack.py); read here in place,
// mapped from the flash. The dev box's test modem reads $HOSTMODEM_PICTURES.
const uint8_t* CastPack(size_t& aLen)
	{
	aLen = 0;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
	static esp_partition_mmap_handle_t sMap;
	static bool sMapped = false;
	if (sMapped)
		{
		esp_partition_munmap(sMap);          // (the last reply has gone: map it afresh, the pack may be new)
		sMapped = false;
		}
	const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, 0);
	uint8_t head[8];
	if (!part || esp_partition_read(part, 0, head, sizeof(head)) != ESP_OK || memcmp(head, "PCPK", 4) != 0)
		return 0;
	uint32_t n = head[4] | (head[5] << 8) | (head[6] << 16) | ((uint32_t)head[7] << 24);
	if (!n || n > part->size - 8)
		return 0;
	const void* p = 0;
	if (esp_partition_mmap(part, 0, n + 8, ESP_PARTITION_MMAP_DATA, &p, &sMap) != ESP_OK)
		return 0;
	sMapped = true;
	aLen = n;
	return (const uint8_t*)p + 8;
#else
	static uint8_t* sData = 0;
	static size_t sLen = 0;
	const char* path = getenv("HOSTMODEM_PICTURES");
	if (!sData && path)
		{
		FILE* f = fopen(path, "rb");
		if (f)
			{
			fseek(f, 0, SEEK_END);
			long n = ftell(f);
			fseek(f, 0, SEEK_SET);
			sData = n > 0 ? (uint8_t*)malloc((size_t)n) : 0;
			if (sData && fread(sData, 1, (size_t)n, f) == (size_t)n)
				sLen = (size_t)n;
			fclose(f);
			}
		}
	aLen = sLen;
	return sLen ? sData : 0;
#endif
	}

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
	else if (strcmp(aPath, "/screen/xbrz.js") == 0)
		Asset(aR, "text/javascript", kCastXbrzGz, kCastXbrzGzLen);
	else if (strcmp(aPath, "/screen/sharp.js") == 0)
		Asset(aR, "text/javascript", kCastSharpGz, kCastSharpGzLen);
	else if (strcmp(aPath, "/screen/textmatch.js") == 0)
		Asset(aR, "text/javascript", kCastTextMatchGz, kCastTextMatchGzLen);
	else if (strcmp(aPath, "/screen/eonfonts.json") == 0)
		Asset(aR, "application/json", kCastFontsGz, kCastFontsGzLen);
	else if (strcmp(aPath, "/screen/pictures.json") == 0)
		{
		size_t n;
		const uint8_t* p = CastPack(n);
		if (p)
			aR.Data(200, "application/octet-stream", p, n, true, "no-cache");   // (a pack: JSON, or JSON and its images)
		else
			aR.Text(404, "application/json", "{\"error\":\"no picture pack on this Atom\"}", "no-store");
		}
	}

} // namespace am
