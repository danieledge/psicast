// kbdusb.cpp - the phone keyboard over USB (kbdlinks.h): the ESP32-S3's
// USB-C as a USB HID keyboard (TinyUSB, the Arduino core's USBHIDKeyboard
// for its descriptors), for a phone that is a USB host (a USB-C iPhone on a
// plain cable; an older one through a camera adapter).
//   - The firmware's console is the S3's USB Serial/JTAG (ARDUINO_USB_MODE=1),
//     which shares the one USB PHY with the OTG controller that TinyUSB
//     needs. At the first Start the PHY moves to TinyUSB and the console is
//     gone until a restart (the core cannot give it back; the log is still
//     on the web pages and in AT$LOG?). Flashing over USB then needs a
//     restart, or the button held through a reset (the ROM's download mode
//     always works); the web update is not affected.
//   - Not with AT$USB=1: the USB-C was made a host at start, for tethering.
//   - Only in keyboard mode: after the first Start, Stop detaches from the
//     bus (the phone sees the keyboard unplugged, and its own keyboard comes
//     back) and Start attaches again.
// MIT licence (see LICENSE at the top of the repository).
#include "kbdlinks.h"
#include "kbd.h"
#include "modem.h"

namespace am {
static bool gUsbHost = false;                // (AT$USB=1 at start)
void KbdUsbHostAtStart(bool aHost) { gUsbHost = aHost; }
} // namespace am

#if defined(CAST_KBD_USB) && CAST_KBD_USB

#include <Arduino.h>
#include <USB.h>
#include <USBHIDKeyboard.h>
#include "tusb.h"
#include <stdio.h>

namespace am {

namespace {

class UsbLink : public KbdLink
	{
public:
	explicit UsbLink(Hal& aHal) : iHal(aHal) {}

	bool Usable(char* aWhy, size_t aMax) override
		{
		if (gUsbHost)
			{
			snprintf(aWhy, aMax, "the USB-C is a host (AT$USB=1)");
			return false;
			}
		if (iFailed)
			{
			snprintf(aWhy, aMax, "USB did not start");
			return false;
			}
		return true;
		}

	void Start() override
		{
		if (!iBegun)
			{
			// (the descriptors first: TinyUSB reads them once, as it starts)
			iKbd = new USBHIDKeyboard();
			iKbd->begin();
			USB.productName(kKbdName);
			USB.manufacturerName("PsiConnect");
			if (!USB.begin())
				{
				iFailed = true;
				iHal.Log("Phone keyboard: USB did not start");
				return;
				}
			iBegun = true;
			iHal.Log("Phone keyboard: the USB-C is a keyboard now (the USB console is off until a restart)");
			}
		else
			tud_connect();
		iAttached = true;
		}

	void Stop() override
		{
		if (iBegun && iAttached)
			tud_disconnect();                     // (unplugged, as far as the phone can tell)
		iAttached = false;
		}

	bool Ready() const override
		{
		return iBegun && iAttached && tud_mounted() && !tud_suspended();
		}

	bool Send(const uint8_t aReport[8]) override
		{
		if (!Ready() || !tud_hid_n_ready(0))
			return false;                         // (the last report is still going: the next pass)
		// (in boot protocol the report goes without its ID, as the core does it)
		uint8_t id = tud_hid_n_get_protocol(0) == HID_PROTOCOL_BOOT ? 0 : HID_REPORT_ID_KEYBOARD;
		hid_keyboard_report_t r;
		r.modifier = aReport[0];
		r.reserved = 0;
		memcpy(r.keycode, aReport + 2, 6);
		return tud_hid_n_report(0, id, &r, sizeof(r));
		}

private:
	Hal& iHal;
	USBHIDKeyboard* iKbd = 0;
	bool iBegun = false, iAttached = false, iFailed = false;
	};

} // namespace

KbdLink* KbdUsbLink(Hal& aHal)
	{
	static UsbLink* sLink = 0;
	if (!sLink)
		sLink = new UsbLink(aHal);
	return sLink;
	}

} // namespace am

#else

namespace am {
KbdLink* KbdUsbLink(Hal& aHal) { (void)aHal; return 0; }
} // namespace am

#endif
