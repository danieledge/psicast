// kbdext.cpp - see kbdext.h. MIT licence (see LICENSE at the top of the repository).
#include "kbdext.h"
#include "kbdlinks.h"
#include "castext.h"
#include "schema.h"
#include "modem.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <new>

namespace am {

const char kKbdName[] = "PsiConnect keyboard";

KbdExt& TheKbdExt()
	{
	static KbdExt sExt;
	return sExt;
	}

// (only where there is a way to the phone: none on the plain Atom)
#if !defined(AM_FIRMWARE) || defined(CAST_KBD_BLE) || defined(CAST_KBD_USB)
static ExtRegister gRegister(TheKbdExt());
#endif

static const char* const kKbdNames[] = { "BT", "USB", "OFF", 0 };
static const char* const kLayoutNames[] = { "UK", "US", 0 };

static const SchemaEntry kKbdSchema[] =
	{
	{ "KBD", STEnum, SR2, (uint16_t)(offsetof(Settings2, ext) + kKbdByte), 1, 0, 2, kKbdNames, SANone, SGExt, "Typing into the phone",
	  "BT: in PsiCast's keyboard mode the Psion types into a phone through the Atom, a Bluetooth keyboard (pair \"PsiConnect keyboard\" "
	  "once, in the phone's Bluetooth settings). USB: a USB keyboard on the USB-C instead (with AT$USB=0; the USB console is off until a restart). OFF" },
	{ "KBDLAYOUT", STEnum, SR2, (uint16_t)(offsetof(Settings2, ext) + kKbdLayoutByte), 1, 0, 1, kLayoutNames, SANone, SGExt, "Phone's keyboard layout",
	  "The layout chosen for a hardware keyboard on the phone (on an iPhone: Settings, General, Keyboard, Hardware Keyboard): UK (British) or US" },
	};

const SchemaEntry* KbdExt::Schema(int& aCount) const
	{
	aCount = (int)(sizeof(kKbdSchema) / sizeof(kKbdSchema[0]));
	return kKbdSchema;
	}

KbdExt::~KbdExt()
	{
	delete iKbd;
	delete[] iPage;
	}

void KbdJoin()
	{
	Cast* c = TheCastExt().Service();
	PhoneKbd* k = TheKbdExt().Keyboard();
	if (c && k)
		c->SetKeyboard(k);
	}

bool KbdExt::Begin(Hal& aHal)
	{
	if (!iTakePart)
		return false;
	iHal = &aHal;
	if (!iOwnLinks)
		{
		iBtLink = KbdBleLink(aHal);
		iUsbLink = KbdUsbLink(aHal);
		}
	if (!iBtLink && !iUsbLink)
		return false;
	if (!iKbd)
		iKbd = new PhoneKbd(aHal);
	iKbd->SetLinks(iBtLink, iUsbLink);
	KbdJoin();
	return true;
	}

// the settings (AT$KBD, AT$KBDLAYOUT, the pages, ATZ, AT&F)
void KbdExt::Apply(const Settings2& aS2)
	{
	if (!iSawUsb)
		{
		iSawUsb = true;                       // (the first pass: the USB-C's role from the start)
		KbdUsbHostAtStart(aS2.usbHost != 0);
		}
	int v = aS2.ext[kKbdByte];
	iKbd->Configure(v != 2, v == 1 ? EKbdViaUsb : EKbdViaBt, aS2.ext[kKbdLayoutByte] == 1 ? EKbdUs : EKbdBritish);
	}

void KbdExt::Loop(const char* aPppAddr, const char* aListenAddr)
	{
	(void)aPppAddr;
	(void)aListenAddr;
	iKbd->Loop();
	}

static const char* ViaText(int aVia) { return aVia == EKbdViaUsb ? "USB" : "Bluetooth"; }

void KbdExt::StateText(char* aOut, size_t aMax) const
	{
	const char* via = ViaText(iKbd->ViaSetting());
	switch (iKbd->State())
		{
	case EKbdReady:
		snprintf(aOut, aMax, "ready (%s, %s layout)", via, iKbd->Layout() == EKbdUs ? "US" : "British");
		break;
	case EKbdWaiting:
		snprintf(aOut, aMax, "waiting for the phone (%s)", via);
		break;
	case EKbdTyping:
		snprintf(aOut, aMax, "typing on the phone (%s)", via);
		break;
	default:
		if (!iKbd->On())
			snprintf(aOut, aMax, "off");
		else
			snprintf(aOut, aMax, "not available: %s", iKbd->Why()[0] ? iKbd->Why() : via);
		break;
		}
	}

void KbdExt::StatLine(int aIndex, char* aOut, size_t aMax) const
	{
	(void)aIndex;
	static const char* const kStates[] = { "OFF", "READY", "WAIT", "TYPING" };
	int s = iKbd->State();
	snprintf(aOut, aMax, "KBD=%s", kStates[s >= 0 && s <= 3 ? s : 0]);
	}

const char* KbdExt::WebAction() const
	{
	return iKbd->Paired() ? "forget the paired phone" : 0;
	}

static const ExtRoute kRoutes[] = { { "/keyboard", false }, { 0, false } };

const ExtRoute* KbdExt::Routes() const { return kRoutes; }

// the page: the state, the settings, and the forget button. Its link
// carries a nonce made for this page, so a link from anywhere else cannot
// forget the phone
void KbdExt::Page(const char* aNote)
	{
	uint8_t r[8];
	iHal->Random(r, sizeof(r));
	static const char kHex[] = "0123456789abcdef";
	for (int i = 0; i < 8; i++)
		{
		iNonce[i * 2] = kHex[r[i] >> 4];
		iNonce[i * 2 + 1] = kHex[r[i] & 15];
		}
	iNonce[16] = 0;
	char state[100];
	StateText(state, sizeof(state));
	bool paired = iKbd->Paired();
	char forget[96] = "";
	if (paired)
		snprintf(forget, sizeof(forget), "<p><a class=\"b\" href=\"/keyboard?forget=%s\">Forget the paired phone</a></p>", iNonce);
	snprintf(iPage, kPageMax,
		"<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
		"<title>Phone keyboard</title><style>body{font:16px -apple-system,Helvetica,sans-serif;margin:1em;max-width:34em}"
		"td{padding:.2em 1em .2em 0}.b{display:inline-block;padding:.5em 1em;border:1px solid #888;border-radius:6px;"
		"text-decoration:none;color:inherit}.n{background:#ffe9a8;padding:.5em}</style></head><body>"
		"<h1>Phone keyboard</h1>%s%s%s"
		"<table><tr><td>Now</td><td>%s</td></tr>"
		"<tr><td>Typing into the phone</td><td>%s</td></tr>"
		"<tr><td>Phone's layout</td><td>%s</td></tr>"
		"<tr><td>Paired phone</td><td>%s</td></tr></table>"
		"<p>In PsiCast's keyboard mode the Psion's keyboard types into the phone. The first time, pair "
		"&ldquo;%s&rdquo; in the phone's Bluetooth settings while the Psion is in keyboard mode. "
		"The Atom is a keyboard only while the Psion is in that mode, so the phone's own keyboard comes back after it.</p>"
		"%s<p><a href=\"/settings\">Settings</a> &middot; <a href=\"/\">Status</a></p></body></html>",
		aNote ? "<p class=\"n\">" : "", aNote ? aNote : "", aNote ? "</p>" : "",
		state, !iKbd->On() ? "Off" : ViaText(iKbd->ViaSetting()), iKbd->Layout() == EKbdUs ? "US" : "British (UK)",
		paired ? "Yes" : "None", kKbdName, forget);
	}

void KbdExt::Get(const char* aPath, const ExtQuery& aQuery, ExtReply& aR)
	{
	if (strcmp(aPath, "/keyboard") != 0)
		return;
	const char* f = aQuery.Arg("forget");
	const char* note = 0;
	if (f[0])
		{
		if (iNonce[0] && strcmp(f, iNonce) == 0)
			note = iKbd->Forget() ? "The paired phone is forgotten. Forget &ldquo;PsiConnect keyboard&rdquo; on the phone too, then pair it again."
			                      : "The paired phone could not be forgotten.";
		else
			note = "That link has expired: nothing was changed.";
		iNonce[0] = 0;
		}
	if (!iPage)
		iPage = new (std::nothrow) char[kPageMax];
	if (!iPage)
		{
		aR.Text(503, "text/plain", "Not enough memory\n", "no-store");
		return;
		}
	Page(note);
	aR.Data(200, "text/html; charset=utf-8", iPage, strlen(iPage), false, "no-store");
	}

} // namespace am
