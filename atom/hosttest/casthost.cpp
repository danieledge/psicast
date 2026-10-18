// casthost.cpp - PsiCast in hostmodem (the firmware's hostmodem.cpp, built
// here with the module in): HOSTMODEM_CAST=1 makes it take part (it stays
// out otherwise, as if not built in), HOSTMODEM_CASTPORT and
// HOSTMODEM_CASTVIEW move link A's TCP end (127.0.0.1:5961, standing in for
// the PPP route's) and the viewers' WebSocket (5962). hostmodem's own:
// HOSTMODEM_HTTP (the pages, 5980) and HOSTMODEM_ANY=1 (every address, for a
// phone on the LAN). The phone keyboard has a pretend phone over
// "Bluetooth": it connects 200 ms after the keyboard starts, and logs each
// report it takes ("kbd report <mods> <key>"). MIT licence (see LICENSE at
// the top of the repository).
#include "../src/castext.h"
#include "../src/kbdext.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

namespace {

uint32_t NowMs()
	{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
	}

struct HostPhone : am::KbdLink
	{
	bool started = false;
	uint32_t startedMs = 0;
	bool Usable(char*, size_t) override { return true; }
	void Start() override { started = true; startedMs = NowMs(); fprintf(stderr, "\n[kbd phone: advertising]\n"); }
	void Stop() override { started = false; fprintf(stderr, "\n[kbd phone: stopped]\n"); }
	bool Ready() const override { return started && NowMs() - startedMs >= 200; }
	bool Send(const uint8_t aR[8]) override
		{
		if (!Ready())
			return false;
		fprintf(stderr, "\n[kbd report %02x %02x]\n", aR[0], aR[2]);
		return true;
		}
	void PhoneName(char* aOut, size_t aMax) const override { snprintf(aOut, aMax, "Test phone"); }
	};

HostPhone gPhone;

struct CastHost
	{
	CastHost()
		{
		am::CastExt& x = am::TheCastExt();
		const char* on = getenv("HOSTMODEM_CAST");
		x.iTakePart = on && atoi(on);
		am::TheKbdExt().iTakePart = x.iTakePart;
		am::TheKbdExt().UseLinks(&gPhone, 0);
		if (getenv("HOSTMODEM_CASTVIEW"))
			x.iNet.iViewPort = (uint16_t)atoi(getenv("HOSTMODEM_CASTVIEW"));
		if (getenv("HOSTMODEM_CASTPORT"))
			x.iNet.iLinkPort = (uint16_t)atoi(getenv("HOSTMODEM_CASTPORT"));
		}
	};
CastHost gCastHost;
}
