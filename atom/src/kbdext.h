// kbdext.h - the phone keyboard as a module of the Atom's firmware (ext.h),
// beside PsiCast's own (castext.h), whose link A carries the Psion's KEYS:
// its settings, a row on the web status page with its own page
// (/keyboard: its state, and "Forget the paired phone"), and a line in
// AT$STAT?. Registered only where a link to the phone is built in
// (kbdlinks.h) or on the PC (the tests).
//   AT$KBD=BT|USB|OFF   (Settings2's ext[1]; 0, an older record's, is BT)
//   AT$KBDLAYOUT=UK|US  (ext[2]: the phone's keyboard layout; 0 is UK)
// MIT licence (see LICENSE at the top of the repository).
#ifndef ATOM_KBDEXT_H
#define ATOM_KBDEXT_H

#include "ext.h"
#include "kbd.h"

namespace am {

class KbdExt : public Extension
	{
public:
	KbdExt() {}
	~KbdExt() override;
	const char* Name() const override { return "Phone keyboard"; }
	bool Begin(Hal& aHal) override;
	const SchemaEntry* Schema(int& aCount) const override;
	void Apply(const Settings2& aS2) override;
	void Loop(const char* aPppAddr, const char* aListenAddr) override;
	void StateText(char* aOut, size_t aMax) const override;
	// AT$STAT?: KBD=OFF, READY, WAIT (for the phone) or TYPING
	int StatCount() const override { return 1; }
	void StatLine(int aIndex, char* aOut, size_t aMax) const override;
	const char* WebHome() const override { return "/keyboard"; }
	const char* WebAction() const override;
	const ExtRoute* Routes() const override;
	void Get(const char* aPath, const ExtQuery& aQuery, ExtReply& aReply) override;

	PhoneKbd* Keyboard() const { return iKbd; }
	// (the tests and hostmodem: links of their own, before Begin)
	void UseLinks(KbdLink* aBt, KbdLink* aUsb) { iBtLink = aBt; iUsbLink = aUsb; iOwnLinks = true; }
	bool iTakePart = true;     // (hostmodem: HOSTMODEM_CAST=1)

private:
	void Page(const char* aNote);
	Hal* iHal = 0;
	PhoneKbd* iKbd = 0;
	KbdLink* iBtLink = 0;
	KbdLink* iUsbLink = 0;
	bool iOwnLinks = false;
	bool iSawUsb = false;
	char iNonce[17] = "";      // the page's forget link carries it (so no other page can forget)
	char* iPage = 0;           // (made the first time the page is asked for)
	static const size_t kPageMax = 2048;
	};

KbdExt& TheKbdExt();
// the keyboard joined to PsiCast's link A once both have begun
void KbdJoin();

// the setting bytes (Settings2's ext[]): AT$KBD and AT$KBDLAYOUT
static const int kKbdByte = 1, kKbdLayoutByte = 2;

} // namespace am

#endif
