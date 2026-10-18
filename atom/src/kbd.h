// kbd.h - the phone keyboard (PROTOCOL.md, "The phone keyboard"): the
// Psion's keyboard types into a phone, through the Atom. The Psion sends
// KEYS on link A (cast.cpp passes them here); this turns them into HID
// keyboard reports (8 bytes: modifiers, 0, six keys) and gives them to a
// link to the phone: Bluetooth LE (kbdble.cpp) or USB (kbdusb.cpp), as the
// settings say (AT$KBD, AT$KBDLAYOUT; kbdext.cpp). The Atom is a keyboard
// to the phone only while the Psion is in its keyboard mode (between MODE 1
// and MODE 0): a phone with a keyboard connected hides its own on-screen
// one. Everything is let go when link A closes, when the phone goes, on a
// HOLD of 0 and on MODE 0.
//   - CHAR: the character as the phone's layout makes it (kbdmap.h), the
//     held modifiers and the event's Ctrl, Alt and Cmd added; Shift is
//     exactly what the character needs. Each CHAR and KEY is a press and a
//     release (two reports), so nothing is ever left down.
//   - Reports wait in a queue while the link is busy (BLE's buffers fill);
//     an event that does not fit whole is dropped. Nothing is queued while
//     no phone is there: typing then goes nowhere.
// Plain C++, no Arduino calls (the links are behind KbdLink).
// MIT licence (see LICENSE at the top of the repository).
#ifndef ATOM_KBD_H
#define ATOM_KBD_H

#include <stdint.h>
#include <stddef.h>
#include "kbdmap.h"

namespace am {

class Hal;

// KBD's state and transport (PROTOCOL.md)
enum TKbdState { EKbdOff = 0, EKbdReady = 1, EKbdWaiting = 2, EKbdTyping = 3 };
enum TKbdVia { EKbdViaNone = 0, EKbdViaBt = 1, EKbdViaUsb = 2 };
// KEYS's kinds
enum TKbdKind { EKbdChar = 1, EKbdKey = 2, EKbdHold = 3, EKbdMode = 4 };

// a way to the phone: the board's Bluetooth or USB, or a test's
class KbdLink
	{
public:
	virtual ~KbdLink() {}
	// it can be used now (built in, memory for it, the port free); else
	// aWhy says why, in a few words
	virtual bool Usable(char* aWhy, size_t aMax) = 0;
	// be a keyboard (advertise, or reconnect to the phone it is paired
	// with; appear on USB), and stop being one (disconnect, stop
	// advertising, leave USB)
	virtual void Start() = 0;
	virtual void Stop() = 0;
	// a phone is there and takes reports (BLE: connected, encrypted and
	// subscribed; USB: configured by the phone)
	virtual bool Ready() const = 0;
	// one report; false: not now (try again later)
	virtual bool Send(const uint8_t aReport[8]) = 0;
	// every pass of the loop, started or not
	virtual void Loop(uint32_t aNowMs) { (void)aNowMs; }
	// the phone's name, if known ("" if not)
	virtual void PhoneName(char* aOut, size_t aMax) const { if (aMax) aOut[0] = 0; }
	// a phone is paired (BLE), and forgetting it
	virtual bool Paired() const { return false; }
	virtual bool Forget() { return false; }
	};

class PhoneKbd
	{
public:
	explicit PhoneKbd(Hal& aHal) : iHal(aHal) { iWhy[0] = 0; }
	void SetLinks(KbdLink* aBt, KbdLink* aUsb) { iBt = aBt; iUsb = aUsb; }
	// a way to the phone is built in (WELCOME's capability bit2)
	bool Built() const { return iBt || iUsb; }
	// the settings: on, which link (TKbdVia), the phone's layout (TKbdLayout)
	void Configure(bool aOn, int aVia, int aLayout);
	bool On() const { return iOn; }
	int ViaSetting() const { return iVia; }
	int Layout() const { return iLayout; }

	// ----- link A (cast.cpp)
	// a KEYS payload. False: not a valid message (a bad length, or more than
	// 64 events): nothing in it was taken
	bool Keys(const uint8_t* aP, uint32_t aN);
	// link A has closed: everything let go, out of keyboard mode
	void LinkGone();
	// KBD's fields now
	int State() const;
	int Via() const;                // (EKbdViaNone while off or not available)
	void Name(char* aOut, size_t aMax) const;
	bool InMode() const { return iWantMode; }

	// the main loop: the link's turn, the queue, the phone coming and going
	void Loop();

	// a few words on why it is not available ("" if it is)
	const char* Why() const { return iWhy; }
	bool Paired() const;
	bool Forget();
	// (the tests, and the log)
	uint32_t Events() const { return iEvents; }
	uint32_t Dropped() const { return iDropped; }
	uint32_t BadMessages() const { return iBad; }
	size_t Queued() const { return iQN; }

	static const int kMaxEvents = 64;
	static const size_t kQueue = 160;           // reports (two an event, four for a dead key)
	static const int kPerPass = 16;             // reports offered to the link in a pass

private:
	KbdLink* Chosen() const { return iVia == EKbdViaUsb ? iUsb : iBt; }
	void Reconcile();                           // the link started or stopped as the mode and settings say
	void CheckUsable();                         // (once a second: whether the chosen link can be used)
	void Event(int aKind, int aMods, unsigned aCode);
	void ReleaseAll();
	bool Push(uint8_t aMods, uint8_t aKey);
	bool Room(size_t aN) const { return kQueue - iQN >= aN; }
	void Flush();

	Hal& iHal;
	KbdLink* iBt = 0;
	KbdLink* iUsb = 0;
	KbdLink* iCur = 0;              // the link started now (0: none)
	bool iConfigured = false;
	bool iOn = false;
	bool iUsable = false;           // the chosen link can be used (checked once a second)
	uint32_t iUsableMs = 0;
	int iVia = EKbdViaBt;
	int iLayout = EKbdBritish;
	bool iWantMode = false;         // the Psion is in keyboard mode (MODE 1)
	bool iReady = false;            // the phone was ready at the last look
	uint8_t iHeld = 0;              // HOLD's modifiers
	uint8_t iQ[kQueue][8];
	size_t iQHead = 0, iQN = 0;
	char iWhy[48];
	uint32_t iEvents = 0, iDropped = 0, iBad = 0;
	};

} // namespace am

#endif
