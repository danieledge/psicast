// castext.h - PsiCast as an extension module of the Atom's firmware
// (firmware/atom-modem/src/ext.h): the service (cast.h) and its sockets
// (castnet.h) on the modem's hooks. Its setting, AT$CAST, is Settings2's
// ext[0], stored inverted (0, an older record's spare byte, is on): the same
// byte, at offset 442, that it was when PsiCast was in the firmware itself,
// so an Atom keeps its setting across the move. A dial to
// ATDT192.168.7.1:5961 is link A on the modem route; during a PPP call
// castnet.cpp listens for it on the PPP link's address. The viewer is /screen
// on the web pages (castext.cpp has every route). The LCD's Sharing page is
// one of the button's pages, and comes up by itself while anyone is watching
// or the Psion types into the phone (kbdext.h); the LED has a light for
// those (castlcd.h).
// MIT licence (see LICENSE at the top of the repository).
#ifndef ATOM_CASTEXT_H
#define ATOM_CASTEXT_H

#include "ext.h"
#include "cast.h"
#include "castnet.h"
#include "castlcd.h"

namespace am {

class CastExt : public Extension
	{
public:
	CastExt() : iTakePart(true), iHal(0), iCast(0) {}
	const char* Name() const override { return "PsiCast"; }
	bool Begin(Hal& aHal) override;
	void Use(Cast& aCast, Hal& aHal) { iCast = &aCast; iHal = &aHal; }   // (the tests: a service of their own)
	Cast* Service() const { return iCast; }
	const SchemaEntry* Schema(int& aCount) const override;
	void Apply(const Settings2& aS2) override;
	void Loop(const char* aPppAddr, const char* aListenAddr) override;

	bool Answers(uint16_t aPort) const override { return aPort == kCastPort; }
	bool CallRefused(uint16_t aPort, char* aWhy, size_t aMax) override;
	uint32_t CallOpen(uint16_t aPort) override { (void)aPort; return iCast->LinkOpen(Cast::ERouteModem); }
	size_t CallIn(uint32_t aId, const uint8_t* aData, size_t aLen) override { return iCast->LinkIn(aId, aData, aLen); }
	size_t CallOut(uint32_t aId, uint8_t* aOut, size_t aMax) override { return iCast->LinkOut(aId, aOut, aMax); }
	bool CallAlive(uint32_t aId) const override { return iCast->LinkAlive(aId); }
	void CallClose(uint32_t aId) override { iCast->LinkClose(aId); }   // (the screen stays for the viewers)
	bool CallHoldsEscape(uint32_t aId) const override { return iCast->LinkMidMessage(aId); }

	void StateText(char* aOut, size_t aMax) const override { iCast->StateText(aOut, aMax); }
	// AT$STAT?: CAST=OFF, WAIT (on, no Psion sharing) or SHARING; CASTV=<viewers>
	int StatCount() const override { return 2; }
	void StatLine(int aIndex, char* aOut, size_t aMax) const override;
	// the LCD's Call page: on or off with no call; how many are watching while sharing
	bool CallPageRow(bool aIdle, char* aOut, size_t aMax) const override;

	// the LCD's Sharing page and the LED's light while anyone is watching, or
	// the Psion is in keyboard mode (castlcd.h)
	bool ScreenWanted() const override { return iCast && CastLcd::Wanted(*iCast); }
	bool HasPage() const override { return iCast != 0; }   // (one of the button's pages too)
	void ScreenDraw(ExtPen& aPen, uint32_t aNowMs) override;
	uint32_t ScreenNews() const override;
	bool LedWanted(ExtLed& aOut) const override;
	CastLcd iLcd;
	uint32_t iLcdDrawnMs = 0;  // (its picture's memory goes 2 s after the page was last drawn)

	const char* WebHome() const override { return "/screen"; }
	const char* WebAction() const override { return iCast->Enabled() ? "show the screen" : 0; }
	const ExtRoute* Routes() const override;
	void Get(const char* aPath, const ExtQuery& aQuery, ExtReply& aReply) override;

	bool iTakePart;            // (hostmodem: HOSTMODEM_CAST=1; always on the Atom)
	CastNet iNet;              // the viewers' WebSocket and link A over PPP (other ports in hostmodem)

private:
	Hal* iHal;
	Cast* iCast;
	};

// the module's one instance (registered with the firmware in castext.cpp)
CastExt& TheCastExt();

} // namespace am

#endif
