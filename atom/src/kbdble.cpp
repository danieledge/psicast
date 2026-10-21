// kbdble.cpp - the phone keyboard over Bluetooth LE (kbdlinks.h): a HID
// keyboard (HID over GATT) with NimBLE, "PsiConnect keyboard", appearance
// keyboard, bonded with LE Secure Connections ("just works": the Atom has no
// screen or keys for a code), the bond kept in NVS by NimBLE.
//   - What iOS needs: the HID service with its report map, HID information,
//     control point and protocol mode; Device Information with a PnP ID;
//     Battery (always 100: the Atom is on USB power); the input report
//     readable only encrypted, so the phone pairs before it subscribes; and
//     reports sent only once the link is encrypted and the phone has
//     subscribed. After pairing the Atom asks for a 15-30 ms connection
//     interval, within Apple's rules (latency 4, timeout 4 s).
//   - Advertising: flags, appearance and the HID service; the name in the
//     scan response (it does not fit beside them). A paired phone reconnects
//     by itself when it sees the advertising.
//   - Only in keyboard mode: Start advertises, Stop disconnects and stops.
//     Bluetooth is set up at the first Start (about 50 KB of internal memory,
//     checked first) and taken down again 10 s after a Stop, so it costs
//     nothing while unused; while it is up Wi-Fi keeps its modem sleep
//     (Hal::RadioShared), which the two need to share the radio.
//   - NimBLE's callbacks run on its own task: they only set flags, which
//     Loop reads (and logs) on the main loop.
// MIT licence (see LICENSE at the top of the repository).
#include "kbdlinks.h"
#include "kbd.h"
#include "modem.h"

#if defined(CAST_KBD_BLE) && CAST_KBD_BLE

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_heap_caps.h>
#include <nvs.h>
#include <stdio.h>
#include <string.h>

namespace am {

namespace {

// a boot-compatible keyboard, report ID 1: modifiers, a reserved byte, six
// keys (usages 0 to 0x65); the LEDs as an output report
const uint8_t kReportMap[] =
	{
	0x05, 0x01, 0x09, 0x06, 0xa1, 0x01, 0x85, 0x01,
	0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
	0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
	0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
	0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
	0xc0
	};

const size_t kNeedFree = 70 * 1024;          // internal memory free before Bluetooth starts
const uint32_t kTearDownMs = 10000;          // Bluetooth taken down this long after a Stop
const char kBondSpace[] = "nimble_bond";     // NimBLE's NVS namespace for bonds

class BleLink : public KbdLink, public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks
	{
public:
	explicit BleLink(Hal& aHal) : iHal(aHal) {}

	bool Usable(char* aWhy, size_t aMax, int& aCode) override
		{
		if (iUp)
			return true;
		if (iFailed)
			{
			snprintf(aWhy, aMax, "Bluetooth did not start");
			aCode = EKbdWhyFailed;
			return false;
			}
		size_t free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		if (free < kNeedFree)
			{
			snprintf(aWhy, aMax, "not enough memory for Bluetooth (%u KB)", (unsigned)(free / 1024));
			aCode = EKbdWhyMemory;
			return false;
			}
		return true;
		}

	void Start() override
		{
		iWanted = true;
		if (!iUp && !Up())
			return;
		Advertise();
		}

	void Stop() override
		{
		iWanted = false;
		iStoppedMs = iHal.Millis();
		if (!iUp)
			return;
		NimBLEDevice::getAdvertising()->stop();
		uint16_t c = iConn;
		if (c != BLE_HS_CONN_HANDLE_NONE)
			iServer->disconnect(c);
		}

	bool Ready() const override
		{
		return iUp && iConn != BLE_HS_CONN_HANDLE_NONE && iSecure && iSubscribed;
		}

	bool Send(const uint8_t aReport[8]) override
		{
		uint16_t c = iConn;
		if (!Ready() || c == BLE_HS_CONN_HANDLE_NONE)
			return false;
		return iInput->notify(aReport, 8, c);     // (false: NimBLE's buffers are full)
		}

	void Loop(uint32_t aNowMs) override
		{
		if (!iUp)
			return;
		Events();
		uint16_t c = iConn;
		if (iWanted && c == BLE_HS_CONN_HANDLE_NONE && !NimBLEDevice::getAdvertising()->isAdvertising())
			Advertise();                          // (after a disconnect: the phone comes back to it)
		if (iWanted && c != BLE_HS_CONN_HANDLE_NONE && iSecure && !iParamsAsked)
			{
			// 15 to 30 ms, latency 4, 4 s: within Apple's rules for a HID
			// device, and gentle on Wi-Fi's share of the radio
			iServer->updateConnParams(c, 12, 24, 4, 400);
			iParamsAsked = true;
			}
		if (!iWanted && c == BLE_HS_CONN_HANDLE_NONE && aNowMs - iStoppedMs >= kTearDownMs)
			Down();
		}

	bool Paired() const override
		{
		if (iUp)
			return NimBLEDevice::getNumBonds() > 0;
		// (Bluetooth is down: whether NimBLE's namespace holds anything)
		nvs_iterator_t it = 0;
		bool any = nvs_entry_find(NVS_DEFAULT_PART_NAME, kBondSpace, NVS_TYPE_ANY, &it) == ESP_OK && it;
		if (it)
			nvs_release_iterator(it);
		return any;
		}

	bool Forget() override
		{
		if (iUp)
			{
			uint16_t c = iConn;
			if (c != BLE_HS_CONN_HANDLE_NONE)
				iServer->disconnect(c);
			return NimBLEDevice::deleteAllBonds();
			}
		nvs_handle_t h;
		if (nvs_open(kBondSpace, NVS_READWRITE, &h) != ESP_OK)
			return true;                          // (no namespace: nothing paired)
		bool ok = nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK;
		nvs_close(h);
		return ok;
		}

	// ----- NimBLE's task
	void onConnect(NimBLEServer* aServer, NimBLEConnInfo& aInfo) override
		{
		if (!iWanted)
			{
			aServer->disconnect(aInfo);           // (out of keyboard mode: not now)
			return;
			}
		iSecure = aInfo.isEncrypted();
		iSubscribed = false;
		iParamsAsked = false;
		iConn = aInfo.getConnHandle();
		iEvConnect = true;
		}
	void onDisconnect(NimBLEServer* aServer, NimBLEConnInfo& aInfo, int aReason) override
		{
		(void)aServer;
		if (aInfo.getConnHandle() != iConn)
			return;
		iConn = BLE_HS_CONN_HANDLE_NONE;
		iSecure = iSubscribed = false;
		iReason = aReason;
		iEvDisconnect = true;
		}
	void onAuthenticationComplete(NimBLEConnInfo& aInfo) override
		{
		iSecure = aInfo.isEncrypted();
		iBonded = aInfo.isBonded();
		iEvAuth = true;
		}
	void onSubscribe(NimBLECharacteristic* aChr, NimBLEConnInfo& aInfo, uint16_t aSub) override
		{
		(void)aInfo;
		if (aChr == iInput)
			iSubscribed = (aSub & 1) != 0;
		}

private:
	bool Up()
		{
		iHal.RadioShared(true);                   // (before the controller starts: Wi-Fi in modem sleep)
		if (!NimBLEDevice::init(kKbdName))
			{
			iFailed = true;
			iHal.RadioShared(false);
			iHal.Log("Phone keyboard: Bluetooth did not start");
			return false;
			}
		NimBLEDevice::setSecurityAuth(true, false, true);       // bonding, no MITM (no screen), Secure Connections
		NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
		iServer = NimBLEDevice::createServer();
		iServer->setCallbacks(this, false);
		iServer->advertiseOnDisconnect(false);    // (Loop decides)
		iHid = new NimBLEHIDDevice(iServer);
		iHid->setManufacturer("PsiConnect");
		iHid->setPnp(0x02, 0x303a, 0x8170, 0x0100);   // (USB-IF vendor IDs: Espressif's)
		iHid->setHidInfo(0x00, 0x01);
		iHid->setReportMap((uint8_t*)kReportMap, sizeof(kReportMap));
		iInput = iHid->getInputReport(1);
		iInput->setCallbacks(this);
		iHid->getOutputReport(1);                 // (the LEDs: written by the phone, not used)
		iHid->setBatteryLevel(100);
		iServer->start();
		iUp = true;
		char m[96];
		snprintf(m, sizeof(m), "Phone keyboard: Bluetooth on as \"%s\", %u KB free", kKbdName,
			(unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
		iHal.Log(m);
		return true;
		}

	void Down()
		{
		NimBLEDevice::deinit(true);               // (the server and its services go too)
		delete iHid;
		iHid = 0;
		iServer = 0;
		iInput = 0;
		iUp = false;
		iConn = BLE_HS_CONN_HANDLE_NONE;
		iSecure = iSubscribed = false;
		iHal.RadioShared(false);
		iHal.Log("Phone keyboard: Bluetooth off");
		}

	void Advertise()
		{
		NimBLEAdvertising* a = NimBLEDevice::getAdvertising();
		NimBLEAdvertisementData ad, sr;
		ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
		ad.setAppearance(HID_KEYBOARD);
		ad.setCompleteServices(NimBLEUUID((uint16_t)0x1812));
		sr.setName(kKbdName);
		a->setAdvertisementData(ad);
		a->setScanResponseData(sr);
		a->enableScanResponse(true);
		a->start();
		}

	void Events()
		{
		char m[96];
		if (iEvConnect)
			{
			iEvConnect = false;
			iHal.Log("Phone keyboard: a phone connected over Bluetooth");
			}
		if (iEvAuth)
			{
			iEvAuth = false;
			snprintf(m, sizeof(m), "Phone keyboard: %s", !iSecure ? "pairing failed" : iBonded ? "paired, encrypted" : "encrypted (not bonded)");
			iHal.Log(m);
			}
		if (iEvDisconnect)
			{
			iEvDisconnect = false;
			snprintf(m, sizeof(m), "Phone keyboard: the phone disconnected (reason 0x%x)", (unsigned)iReason);
			iHal.Log(m);
			}
		}

	Hal& iHal;
	NimBLEServer* iServer = 0;
	NimBLEHIDDevice* iHid = 0;
	NimBLECharacteristic* iInput = 0;
	bool iUp = false, iFailed = false, iWanted = false, iParamsAsked = false;
	uint32_t iStoppedMs = 0;
	// (set on NimBLE's task)
	volatile uint16_t iConn = BLE_HS_CONN_HANDLE_NONE;
	volatile bool iSecure = false, iSubscribed = false, iBonded = false;
	volatile bool iEvConnect = false, iEvDisconnect = false, iEvAuth = false;
	volatile int iReason = 0;
	};

} // namespace

KbdLink* KbdBleLink(Hal& aHal)
	{
	static BleLink* sLink = 0;
	if (!sLink)
		sLink = new BleLink(aHal);
	return sLink;
	}

} // namespace am

#else

namespace am {
KbdLink* KbdBleLink(Hal& aHal) { (void)aHal; return 0; }
} // namespace am

#endif
