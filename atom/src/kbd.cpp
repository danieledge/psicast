// kbd.cpp - see kbd.h. MIT licence (see LICENSE at the top of the repository).
#include "kbd.h"
#include "modem.h"
#include <stdio.h>
#include <string.h>

namespace am {

static const char* ViaName(int aVia) { return aVia == EKbdViaUsb ? "USB" : "Bluetooth"; }

void PhoneKbd::Configure(bool aOn, int aVia, int aLayout)
	{
	if (aVia != EKbdViaUsb)
		aVia = EKbdViaBt;
	if (aLayout != EKbdUs)
		aLayout = EKbdBritish;
	if (aOn == iOn && aVia == iVia && aLayout == iLayout && iConfigured)
		return;
	iConfigured = true;
	iOn = aOn;
	iVia = aVia;
	iLayout = aLayout;
	char m[80];
	if (!aOn)
		snprintf(m, sizeof(m), "Phone keyboard: off");
	else
		snprintf(m, sizeof(m), "Phone keyboard: %s, %s layout", ViaName(aVia), aLayout == EKbdUs ? "US" : "British");
	iHal.Log(m);
	CheckUsable();
	Reconcile();
	}

void PhoneKbd::CheckUsable()
	{
	KbdLink* l = Chosen();
	iWhy[0] = 0;
	if (!l)
		snprintf(iWhy, sizeof(iWhy), "not on this board");
	iUsable = l && (l == iCur || l->Usable(iWhy, sizeof(iWhy)));
	iUsableMs = iHal.Millis();
	}

int PhoneKbd::State() const
	{
	if (!iOn || !iUsable)
		return EKbdOff;
	if (!iWantMode)
		return EKbdReady;
	if (!iCur)
		return EKbdOff;
	return iReady ? EKbdTyping : EKbdWaiting;
	}

int PhoneKbd::Via() const
	{
	return State() == EKbdOff ? EKbdViaNone : iVia;
	}

void PhoneKbd::Name(char* aOut, size_t aMax) const
	{
	if (iCur && iReady)
		iCur->PhoneName(aOut, aMax);
	else if (aMax)
		aOut[0] = 0;
	}

bool PhoneKbd::Paired() const
	{
	return iBt && iBt->Paired();
	}

bool PhoneKbd::Forget()
	{
	if (!iBt)
		return false;
	bool ok = iBt->Forget();
	iHal.Log(ok ? "Phone keyboard: the paired phone is forgotten" : "Phone keyboard: could not forget the paired phone");
	return ok;
	}

// The link is started only between MODE 1 and MODE 0, with the keyboard on
// and its link usable; otherwise it is stopped (the phone's on-screen
// keyboard comes back). A change of link stops the old one first
void PhoneKbd::Reconcile()
	{
	KbdLink* want = 0;
	if (iOn && iWantMode && Chosen())
		{
		if (Chosen() == iCur)
			want = iCur;
		else
			{
			CheckUsable();
			if (iUsable)
				want = Chosen();
			else
				{
				char m[80];
				snprintf(m, sizeof(m), "Phone keyboard: not available (%s)", iWhy);
				iHal.Log(m);
				}
			}
		}
	if (want == iCur)
		return;
	if (iCur)
		{
		Flush();                                  // (what the link takes now, then let go)
		if (iReady)
			{
			static const uint8_t kNone[8] = { 0 };
			iCur->Send(kNone);
			}
		iCur->Stop();
		char m[64];
		snprintf(m, sizeof(m), "Phone keyboard: %s stopped", ViaName(iCur == iUsb ? EKbdViaUsb : EKbdViaBt));
		iHal.Log(m);
		}
	iReady = false;
	ReleaseAll();
	iCur = want;
	if (iCur)
		{
		iCur->Start();
		char m[80];
		snprintf(m, sizeof(m), "Phone keyboard: %s started, waiting for the phone", ViaName(iVia));
		iHal.Log(m);
		}
	}

// everything let go: no modifier held, nothing waiting, and (with a phone
// there) a report with no key at all
void PhoneKbd::ReleaseAll()
	{
	iHeld = 0;
	iQHead = iQN = 0;
	if (iReady)
		Push(0, 0);
	}

bool PhoneKbd::Push(uint8_t aMods, uint8_t aKey)
	{
	if (!Room(1))
		return false;
	uint8_t* r = iQ[(iQHead + iQN) % kQueue];
	memset(r, 0, 8);
	r[0] = aMods;
	r[2] = aKey;
	iQN++;
	return true;
	}

void PhoneKbd::Flush()
	{
	if (!iCur || !iReady)
		return;
	for (int k = 0; k < kPerPass && iQN; k++)
		{
		if (!iCur->Send(iQ[iQHead]))
			break;                                // (the link is full: the rest in a later pass)
		iQHead = (iQHead + 1) % kQueue;
		iQN--;
		}
	}

static uint16_t Le16(const uint8_t* aP) { return (uint16_t)(aP[0] | (aP[1] << 8)); }

bool PhoneKbd::Keys(const uint8_t* aP, uint32_t aN)
	{
	if (aN == 0 || aN % 4 || aN / 4 > (uint32_t)kMaxEvents)
		{
		if (iBad++ < 3)
			{
			char m[64];
			snprintf(m, sizeof(m), "Phone keyboard: a KEYS of %lu bytes ignored", (unsigned long)aN);
			iHal.Log(m);
			}
		return false;
		}
	for (uint32_t i = 0; i < aN; i += 4)
		Event(aP[i], aP[i + 1], Le16(aP + i + 2));
	Flush();
	return true;
	}

void PhoneKbd::Event(int aKind, int aMods, unsigned aCode)
	{
	iEvents++;
	switch (aKind)
		{
	case EKbdChar:
		{
		KbdStroke s[2];
		int n = iReady ? KbdStrokes(iLayout, aCode, s) : 0;
		if (!n || !Room((size_t)n * 2))
			{
			iDropped++;                           // (no phone, a character this layout cannot make, or no room)
			return;
			}
		// the held modifiers but Shift, the event's own Ctrl, Alt and Cmd (on
		// a single key: not on a dead key's two), then what the character needs
		uint8_t base = (uint8_t)(iHeld & ~kKbdShift);
		uint8_t extra = n == 1 ? (uint8_t)(aMods & (kKbdCtrl | kKbdAlt | kKbdGui)) : 0;
		for (int i = 0; i < n; i++)
			{
			Push((uint8_t)(base | extra | s[i].mods), s[i].usage);
			Push(iHeld, 0);
			}
		break;
		}
	case EKbdKey:
		// (a key the report map has, and not a modifier: those are mods)
		if (!iReady || aCode < 0x04 || aCode > 0x65 || !Room(2))
			{
			iDropped++;
			return;
			}
		Push((uint8_t)(iHeld | aMods), (uint8_t)aCode);
		Push(iHeld, 0);
		break;
	case EKbdHold:
		iHeld = (uint8_t)aMods;
		if (!iReady)
			break;
		if (!Room(1))
			{
			if (aMods)
				break;                            // (the next report carries it)
			iQHead = iQN = 0;                     // (a release must get there: what waits goes)
			}
		Push(iHeld, 0);
		break;
	case EKbdMode:
		if (aCode == 1)
			{
			if (!iWantMode)
				{
				iWantMode = true;
				Reconcile();
				}
			}
		else if (aCode == 0)
			{
			iWantMode = false;
			Reconcile();
			ReleaseAll();
			}
		break;
	default:
		break;                                    // (a newer Psion's kind: ignored)
		}
	}

void PhoneKbd::LinkGone()
	{
	if (!iWantMode && !iHeld && !iQN)
		return;
	iWantMode = false;
	Reconcile();
	ReleaseAll();
	}

void PhoneKbd::Loop()
	{
	uint32_t now = iHal.Millis();
	if (iBt)
		iBt->Loop(now);
	if (iUsb && iUsb != iBt)
		iUsb->Loop(now);
	bool ready = iCur && iCur->Ready();
	if (ready != iReady)
		{
		// the phone came (a clean start: nothing down) or went (let go)
		iReady = ready;
		iHeld = 0;
		iQHead = iQN = 0;
		if (ready)
			Push(0, 0);
		iHal.Log(ready ? "Phone keyboard: the phone is connected" : "Phone keyboard: the phone has gone");
		}
	Flush();
	if (now - iUsableMs >= 1000)
		CheckUsable();
	}

} // namespace am
