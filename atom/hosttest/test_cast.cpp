// test_cast.cpp - PsiCast on the Atom (src/cast): link A's framing (CRC,
// resync, oversize), the session against a Psion played with the real
// encoder (HELLO/WELCOME, ACKs that keep the window moving, KEYREQ after a
// framing or decode error, PING, the 20 s silence, BYE), tokens, the
// WebSocket handshake and frames, per-viewer dirty tracking (a fast and a
// slow viewer, scrolls, both pixel-exact at the end, the slow one merged),
// input from a viewer with and without control, and the modem route
// (ATDT192.168.7.1:5961 through Modem), and the phone keyboard: its
// character tables, KEYS (bad lengths, unknown kinds, more than 64 events, a
// busy link), letting go (link A closing, HOLD 0, MODE 0, the phone going),
// the mode's gating with a fake phone behind KbdLink, KBD on link A, and its
// settings, page, LCD line and light. With the sanitizers. MIT licence.
#include "fakehal.h"
#include "../src/cast.h"
#include "../src/castext.h"
#include "../src/websock.h"
#include "../src/castlcd.h"
#include "../src/kbd.h"
#include "../src/kbdmap.h"
#include "../src/kbdext.h"
#include "status.h"
#include "schema.h"
#include <string.h>
#include "pccodec.h"
#include <zlib.h>
#include <vector>
#include <string>

static int gChecks = 0, gFails = 0;
#define CHECK(c) do { gChecks++; if (!(c)) { gFails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

typedef std::vector<uint8_t> Bytes;

static uint32_t Le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t Le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static Bytes Frame(int aType, int aFlags, const Bytes& aPayload)
	{
	Bytes out(aPayload.size() + 12);
	size_t n = am::CastFrame(out.data(), out.size(), aType, aFlags, aPayload.data(), aPayload.size());
	out.resize(n);
	return out;
	}

// the messages in a byte stream from the Atom (to the Psion)
struct Msg { int type; Bytes p; };
static std::vector<Msg> Parse(const Bytes& aIn)
	{
	std::vector<Msg> out;
	std::vector<uint8_t> buf(70000);
	am::CastRx rx;
	rx.Init(buf.data(), buf.size());
	size_t at = 0;
	while (at < aIn.size())
		{
		at += rx.Feed(aIn.data() + at, aIn.size() - at);
		uint8_t t, f;
		const uint8_t* p;
		uint32_t n;
		while (rx.Next(t, f, p, n))
			out.push_back({ t, Bytes(p, p + n) });
		}
	return out;
	}

// ----- framing -----------------------------------------------------------------------

static void TestFraming()
	{
	Bytes a = Frame(am::ECastStatus, 0, Bytes(12, 7));
	Bytes b = Frame(am::ECastUpdate, 1, Bytes(3000, 0x50));     // (full of 'P's: a hard resync)
	CHECK(a.size() == 24 && a[0] == 'P' && a[1] == 'C' && Le32(a.data() + 4) == 12);
	CHECK(Le32(a.data() + 20) == (uint32_t)crc32(0, a.data(), 20));
	// rubbish, a whole message, one with a bad CRC, another whole one, split byte by byte
	Bytes s;
	const char* junk = "PPC\x01\xff\xff\xff\xffhelloPCxx";
	s.insert(s.end(), junk, junk + 18);
	s.insert(s.end(), a.begin(), a.end());
	Bytes bad = b;
	bad[100] ^= 0x40;
	s.insert(s.end(), bad.begin(), bad.end());
	s.insert(s.end(), b.begin(), b.end());
	s.insert(s.end(), a.begin(), a.end());
	std::vector<uint8_t> buf(70000);
	am::CastRx rx;
	rx.Init(buf.data(), buf.size());
	std::vector<int> types;
	for (size_t i = 0; i < s.size(); i++)
		{
		CHECK(rx.Feed(&s[i], 1) == 1);
		uint8_t t, f;
		const uint8_t* p;
		uint32_t n;
		while (rx.Next(t, f, p, n))
			{
			types.push_back(t);
			if (t == am::ECastUpdate)
				CHECK(f == 1 && n == 3000 && p[2999] == 0x50);
			}
		}
	CHECK(types.size() == 3 && types[0] == am::ECastStatus && types[1] == am::ECastUpdate && types[2] == am::ECastStatus);
	CHECK(rx.Errors() == 2);                     // (the rubbish, and the bad CRC: one each)
	// a message longer than the buffer: skipped whole, the next one found
	std::vector<uint8_t> small(1024);
	am::CastRx r2;
	r2.Init(small.data(), small.size());
	Bytes s2 = b;
	s2.insert(s2.end(), a.begin(), a.end());
	size_t at = 0;
	int got = 0;
	while (at < s2.size())
		{
		at += r2.Feed(s2.data() + at, s2.size() - at);
		uint8_t t, f;
		const uint8_t* p;
		uint32_t n;
		while (r2.Next(t, f, p, n))
			got++;
		}
	CHECK(got == 1 && r2.Oversize() == 1);
	}

// ----- a Psion, played with the real encoder ------------------------------------------

struct Psion
	{
	pc_enc* enc;
	uint32_t seq = 0;
	bool stamp = false;                          // (WELCOME asked: each UPDATE carries stampMs)
	uint32_t stampMs = 0;
	std::vector<uint8_t> frame;                  // 640x240 packed, the model
	Psion() : enc(pc_enc_new(640, 240, 3)), frame(640 * 240 / 2, 0xff) {}
	~Psion() { pc_enc_free(enc); }
	static Bytes Hello(uint32_t aCaps = 3, const char* aName = "My 5mx")
		{
		Bytes p(12);
		p[0] = 1; p[2] = 640 & 255; p[3] = 640 >> 8; p[4] = 240; p[6] = 0;
		p[8] = (uint8_t)aCaps;
		p.insert(p.end(), aName, aName + strlen(aName));
		return Frame(am::ECastHello, 0, p);
		}
	// the next UPDATE for the frame as it is now (empty if nothing changed)
	Bytes Update(bool aKey)
		{
		std::vector<uint8_t> out(65536);
		int len = 0;
		int r = pc_enc_frame(enc, frame.data(), 320, aKey ? 1 : 0, out.data() + 4, (int)out.size() - 4, &len);
		if (r == 1 || r < 0)
			return Bytes();
		seq++;
		out[0] = (uint8_t)seq; out[1] = (uint8_t)(seq >> 8); out[2] = (uint8_t)(seq >> 16); out[3] = (uint8_t)(seq >> 24);
		out.resize((size_t)len + 4);
		if (stamp)
			{
			uint8_t t[4] = { (uint8_t)stampMs, (uint8_t)(stampMs >> 8), (uint8_t)(stampMs >> 16), (uint8_t)(stampMs >> 24) };
			out.insert(out.begin() + 4, t, t + 4);
			}
		return Frame(am::ECastUpdate, (aKey ? 1 : 0) | (stamp ? 2 : 0), out);
		}
	// a newer Psion's STATUS: its clock, and the PING it answers (0: none)
	static Bytes StatusClock(uint32_t aPsionMs, uint32_t aToken)
		{
		Bytes f = Status();
		Bytes p(f.begin() + 8, f.end() - 4);
		for (int i = 0; i < 4; i++) p.push_back((uint8_t)(aPsionMs >> (8 * i)));
		for (int i = 0; i < 4; i++) p.push_back((uint8_t)(aToken >> (8 * i)));
		return Frame(am::ECastStatus, 0, p);
		}
	static Bytes Status(int aBattery = 80, bool aCtl = true)
		{
		Bytes p(12, 0);
		p[0] = (uint8_t)aBattery; p[1] = 0; p[2] = 0; p[3] = 0; p[4] = 0x30;   // (3 MB free)
		p[6] = 50; p[8] = 20; p[10] = aCtl ? 1 : 0; p[11] = 0;
		return Frame(am::ECastStatus, 0, p);
		}
	void Px(int aX, int aY, int aG)
		{
		uint8_t& b = frame[aY * 320 + aX / 2];
		b = (aX & 1) ? (uint8_t)((b & 0x0f) | (aG << 4)) : (uint8_t)((b & 0xf0) | aG);
		}
	void Box(int aX, int aY, int aW, int aH, int aG)
		{
		for (int y = aY; y < aY + aH && y < 240; y++)
			for (int x = aX; x < aX + aW && x < 640; x++)
				Px(x, y, aG);
		}
	// a line of "text" (random marks) at row aY
	void Text(int aY, unsigned& aSeed)
		{
		for (int x = 4; x < 600; x += 7)
			{
			aSeed = aSeed * 1103515245 + 12345;
			if ((aSeed >> 16) % 5 == 0) continue;
			Box(x, aY + 2, 5, 9, (int)((aSeed >> 20) % 3) * 5);
			}
		}
	// scroll the area below the title by one 12-pixel line, and write a new one
	void Scroll(unsigned& aSeed)
		{
		memmove(&frame[20 * 320], &frame[32 * 320], (size_t)(228 - 32 + 12) * 320 - 12 * 320);
		Box(0, 216, 640, 12, 15);
		Text(216, aSeed);
		}
	};

// a viewer's canvas, from link B's messages
struct ViewModel
	{
	int w = 0, h = 0;
	std::vector<uint8_t> grey;                   // a pixel a byte
	am::WsReader reader;
	size_t bytes = 0;
	int fulls = 0, rects = 0, scrolls = 0, partial = 0;
	std::vector<std::string> texts;
	std::vector<Bytes> pings;                    // (the Atom's WebSocket pings: their payloads)
	std::vector<Bytes> closes;                   // (close frames: code and reason)
	static void OnMsg(void* aCtx, int aOp, const uint8_t* aP, size_t aN)
		{
		ViewModel* self = (ViewModel*)aCtx;
		if (aOp == am::EWsPing)
			self->pings.push_back(Bytes(aP, aP + aN));
		else if (aOp == am::EWsClose)
			self->closes.push_back(Bytes(aP, aP + aN));
		else if (aOp == am::EWsText)
			self->texts.push_back(std::string((const char*)aP, aN));
		else if (aOp == am::EWsBinary)
			self->Binary(aP, aN);
		}
	void Binary(const uint8_t* aP, size_t aN)
		{
		if (aP[0] == am::ECastFull)
			{
			w = Le16(aP + 1); h = Le16(aP + 3);
			grey.assign((size_t)w * h, 15);
			CHECK(aN == 5 + (size_t)w * h / 2);
			for (size_t i = 0; i < (size_t)w * h / 2; i++)
				{
				grey[i * 2] = aP[5 + i] & 15;
				grey[i * 2 + 1] = aP[5 + i] >> 4;
				}
			fulls++;
			}
		else if (aP[0] == am::ECastRects)
			{
			int n = Le16(aP + 1);
			size_t o = 3 + (size_t)n * 8;
			for (int k = 0; k < n; k++)
				{
				int x = Le16(aP + 3 + k * 8), y = Le16(aP + 5 + k * 8), rw = Le16(aP + 7 + k * 8), rh = Le16(aP + 9 + k * 8);
				CHECK(x % 2 == 0 && rw % 2 == 0 && x + rw <= w && y + rh <= h);
				for (int r = 0; r < rh; r++)
					for (int c = 0; c < rw; c += 2)
						{
						uint8_t b = aP[o++];
						grey[(size_t)(y + r) * w + x + c] = b & 15;
						grey[(size_t)(y + r) * w + x + c + 1] = b >> 4;
						}
				}
			CHECK(o == aN);
			rects++;
			}
		else if (aP[0] == am::ECastScroll)
			{
			int y0 = Le16(aP + 1), y1 = Le16(aP + 3), dy = (int16_t)Le16(aP + 5);
			int x = 0, cw = w;
			if (aN >= 11) { x = Le16(aP + 7); cw = Le16(aP + 9); }   // (the columns that moved)
			CHECK(aN == 11 && x % 2 == 0 && cw % 2 == 0 && x + cw <= w);
			int d0 = y0 + dy, d1 = y1 + dy;
			if (d0 < 0) { y0 -= d0; d0 = 0; }
			if (d1 > h) { y1 -= d1 - h; d1 = h; }
			std::vector<uint8_t> band;
			for (int r = y0; r < y1; r++)
				band.insert(band.end(), &grey[(size_t)r * w + x], &grey[(size_t)r * w + x + cw]);
			for (int r = d0; r < d1; r++)
				memcpy(&grey[(size_t)r * w + x], &band[(size_t)(r - d0) * cw], cw);
			if (cw < w) partial++;
			scrolls++;
			}
		}
	void Feed(const uint8_t* aP, size_t aN)
		{
		bytes += aN;
		// (server frames are unmasked: set the mask bit so the client-side reader takes them)
		static std::vector<uint8_t> tmp;
		tmp.assign(aP, aP + aN);
		FeedUnmasked(tmp);
		}
	// a tiny unmasked-frame reader (WsReader is the server's, for masked frames)
	std::vector<uint8_t> pend;
	void FeedUnmasked(const std::vector<uint8_t>& aIn)
		{
		pend.insert(pend.end(), aIn.begin(), aIn.end());
		for (;;)
			{
			if (pend.size() < 2) return;
			int op = pend[0] & 15;
			uint64_t len = pend[1] & 127;
			size_t h = 2;
			if (len == 126) { if (pend.size() < 4) return; len = (pend[2] << 8) | pend[3]; h = 4; }
			else if (len == 127)
				{
				if (pend.size() < 10) return;
				len = 0;
				for (int i = 2; i < 10; i++) len = (len << 8) | pend[i];
				h = 10;
				}
			if (pend.size() < h + len) return;
			OnMsg(this, op, pend.data() + h, (size_t)len);
			pend.erase(pend.begin(), pend.begin() + h + len);
			}
		}
	bool Same(const uint8_t* aScreen) const
		{
		if (!w) return false;
		for (size_t i = 0; i < (size_t)w * h / 2; i++)
			if (grey[i * 2] != (aScreen[i] & 15) || grey[i * 2 + 1] != (aScreen[i] >> 4))
				return false;
		return true;
		}
	};

// a client frame (masked), as a browser sends it
static Bytes ClientFrame(int aOp, const std::string& aText, bool aFin = true)
	{
	Bytes f;
	f.push_back((uint8_t)((aFin ? 0x80 : 0) | aOp));
	size_t n = aText.size();
	if (n < 126) f.push_back((uint8_t)(0x80 | n));
	else { f.push_back(0x80 | 126); f.push_back((uint8_t)(n >> 8)); f.push_back((uint8_t)n); }
	uint8_t mask[4] = { 0x12, 0x34, 0x56, 0x78 };
	f.insert(f.end(), mask, mask + 4);
	for (size_t i = 0; i < n; i++)
		f.push_back((uint8_t)aText[i] ^ mask[i & 3]);
	return f;
	}

struct Rig
	{
	FakeHal hal;
	am::Cast cast;
	uint32_t id = 0;
	Bytes fromAtom;                              // to the Psion
	Rig() : cast(hal) { hal.webInfo = "http://192.168.1.50/"; cast.SetEnabled(true); }
	void ToAtom(const Bytes& aB) { CHECK(cast.LinkIn(id, aB.data(), aB.size()) == aB.size()); }
	void Drain()
		{
		uint8_t b[512];
		size_t n;
		while ((n = cast.LinkOut(id, b, sizeof(b))) > 0)
			fromAtom.insert(fromAtom.end(), b, b + n);
		}
	std::vector<Msg> Take()
		{
		Drain();
		std::vector<Msg> m = Parse(fromAtom);
		fromAtom.clear();
		return m;
		}
	void Ms(uint32_t aMs) { for (uint32_t i = 0; i < aMs; i += 50) { hal.Advance(50000); cast.Tick(); } }
	int Viewer(bool aControl = false)
		{
		char t[32], why[64];
		CHECK(cast.NewToken(aControl, t, sizeof(t)));
		return cast.ViewerOpen(t, why, sizeof(why));
		}
	void Pump(int aIdx, ViewModel& aV, size_t aMax = 1 << 20)
		{
		uint8_t b[1460];
		size_t total = 0, n;
		while (total < aMax && (n = cast.ViewerOut(aIdx, b, aMax - total < sizeof(b) ? aMax - total : sizeof(b))) > 0)
			{
			aV.Feed(b, n);
			total += n;
			}
		}
	};

static int Count(const std::vector<Msg>& aM, int aType)
	{
	int n = 0;
	for (const Msg& m : aM) if (m.type == aType) n++;
	return n;
	}

static void TestSession()
	{
	Rig r;
	Psion ps;
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	CHECK(r.id != 0 && !r.cast.LinkUp());
	r.ToAtom(Psion::Hello());
	std::vector<Msg> m = r.Take();
	CHECK(m.size() == 2 && m[0].type == am::ECastWelcome && m[1].type == am::ECastViewers);
	if (m.size() == 2)
		{
		CHECK(Le16(m[0].p.data()) == 1 && Le16(m[0].p.data() + 2) == 2 && (Le32(m[0].p.data() + 4) & 1));
		CHECK(Le32(m[0].p.data() + 8) == 65536);          // (the largest UPDATE payload taken)
		CHECK(std::string(m[0].p.begin() + 12, m[0].p.end()) == "http://192.168.1.50/screen");
		CHECK(m[1].p[0] == 0 && m[1].p[1] == 0);
		}
	CHECK(r.cast.LinkUp() && strcmp(r.cast.PsionName(), "My 5mx") == 0);
	// the keyframe, then two changes: each acknowledged
	unsigned seed = 1;
	ps.Box(0, 0, 640, 18, 0);
	for (int y = 20; y < 228; y += 12) ps.Text(y, seed);
	r.ToAtom(ps.Update(true));
	m = r.Take();
	CHECK(m.size() == 1 && m[0].type == am::ECastAck && Le32(m[0].p.data()) == 1);
	CHECK(memcmp(r.cast.Screen(), ps.frame.data(), ps.frame.size()) == 0);
	ps.Box(100, 100, 50, 20, 7);
	Bytes u2 = ps.Update(false);
	ps.Scroll(seed);
	Bytes u3 = ps.Update(false);
	Bytes both = u2;
	both.insert(both.end(), u3.begin(), u3.end());
	r.ToAtom(both);                              // (two in one read: one ACK, for the latest)
	m = r.Take();
	CHECK(m.size() == 1 && m[0].type == am::ECastAck && Le32(m[0].p.data()) == 3);
	CHECK(memcmp(r.cast.Screen(), ps.frame.data(), ps.frame.size()) == 0);
	// a damaged update (bad CRC): KEYREQ; what follows is not applied until a keyframe
	ps.Box(10, 30, 200, 40, 3);
	Bytes u4 = ps.Update(false);
	u4[20] ^= 1;
	r.ToAtom(u4);
	m = r.Take();
	CHECK(Count(m, am::ECastKeyReq) == 1 && Count(m, am::ECastAck) == 0);
	ps.Box(300, 30, 20, 40, 9);
	r.ToAtom(ps.Update(false));                  // seq 5: acknowledged, not applied (seq 4 went missing)
	m = r.Take();
	CHECK(Count(m, am::ECastAck) == 1 && Count(m, am::ECastKeyReq) == 0);
	CHECK(Count(m, am::ECastPing) == 1);          // (discarded: a PING with the last seq taken)
	for (const Msg& x : m)
		if (x.type == am::ECastPing)
			CHECK(x.p.size() == 8 && Le32(x.p.data() + 4) == 5);
	CHECK(memcmp(r.cast.Screen(), ps.frame.data(), ps.frame.size()) != 0);
	r.Ms(3100);                                  // no keyframe yet: asked again
	m = r.Take();
	CHECK(Count(m, am::ECastKeyReq) == 1);
	r.ToAtom(ps.Update(true));
	m = r.Take();
	CHECK(Count(m, am::ECastAck) == 1);
	CHECK(memcmp(r.cast.Screen(), ps.frame.data(), ps.frame.size()) == 0);
	// a body that does not decode (good CRC, rubbish inside): KEYREQ
	Bytes junk(4 + 50, 0xa5);
	junk[0] = (uint8_t)(++ps.seq);
	junk[1] = junk[2] = junk[3] = 0;
	r.ToAtom(Frame(am::ECastUpdate, 0, junk));
	m = r.Take();
	CHECK(Count(m, am::ECastKeyReq) == 1 && Count(m, am::ECastAck) == 1);
	CHECK(r.cast.Errors() >= 2);
	r.ToAtom(ps.Update(true));
	r.Take();
	CHECK(memcmp(r.cast.Screen(), ps.frame.data(), ps.frame.size()) == 0);
	// STATUS: its fields, and the PING it answered
	r.Ms(5200);
	m = r.Take();
	CHECK(Count(m, am::ECastPing) == 1);
	r.Ms(100);
	r.ToAtom(Psion::Status(64, true));
	CHECK(r.cast.Battery() == 64 && r.cast.ControlAllowed());
	// DEVINFO: the digitiser's size, to the viewers
	Bytes di(4);
	di[0] = 695 & 255; di[1] = 695 >> 8; di[2] = 280 & 255; di[3] = 280 >> 8;
	r.ToAtom(Frame(am::ECastDevInfo, 0, di));
	CHECK(r.cast.DigitiserW() == 695 && r.cast.DigitiserH() == 280);
	// silence: closed after 20 s
	CHECK(r.cast.LinkAlive(r.id));
	r.Ms(19000);
	CHECK(r.cast.LinkAlive(r.id));
	r.Ms(1500);
	CHECK(!r.cast.LinkAlive(r.id));
	r.cast.LinkClose(r.id);
	CHECK(!r.cast.LinkUp());
	// a new link, then BYE
	r.id = r.cast.LinkOpen(am::Cast::ERouteModem);
	r.ToAtom(Psion::Hello());
	r.ToAtom(Frame(am::ECastBye, 0, Bytes()));
	CHECK(!r.cast.LinkAlive(r.id));
	r.cast.LinkClose(r.id);
	// a HELLO it cannot take: BYE back
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	Bytes h = Psion::Hello();
	h[8 + 2] = 0x80; h[8 + 3] = 0x02;            // (width 640 -> 0x280... made 0x0280 + odd below)
	Bytes p(12, 0);
	p[0] = 1; p[2] = 0x81; p[3] = 2; p[4] = 240;   // 641 wide: odd
	r.ToAtom(Frame(am::ECastHello, 0, p));
	m = r.Take();
	CHECK(Count(m, am::ECastBye) == 1 && !r.cast.LinkAlive(r.id));
	r.cast.LinkClose(r.id);
	// a second connection replaces the first
	uint32_t a = r.cast.LinkOpen(am::Cast::ERouteTcp);
	uint32_t b = r.cast.LinkOpen(am::Cast::ERouteTcp);
	CHECK(a && b && a != b && !r.cast.LinkAlive(a) && r.cast.LinkAlive(b));
	// off: refused
	r.cast.SetEnabled(false);
	CHECK(r.cast.LinkOpen(am::Cast::ERouteTcp) == 0);
	}

static void TestTokens()
	{
	Rig r;
	char t[32], t2[32], why[64];
	CHECK(r.cast.NewToken(false, t, sizeof(t)) && strlen(t) == 24);
	CHECK(r.cast.NewToken(true, t2, sizeof(t2)) && strcmp(t, t2) != 0);
	CHECK(r.cast.ViewerOpen("nonsense", why, sizeof(why)) < 0 && strstr(why, "token"));
	int v = r.cast.ViewerOpen(t, why, sizeof(why));
	CHECK(v >= 0);
	CHECK(r.cast.ViewerOpen(t, why, sizeof(why)) < 0);        // single use
	r.Ms(61000);
	CHECK(r.cast.ViewerOpen(t2, why, sizeof(why)) < 0);       // expired
	CHECK(r.cast.NewToken(false, t, sizeof(t)));
	r.Ms(59000);
	r.cast.ViewerIn(v, ClientFrame(am::EWsText, "{\"t\":\"ping\",\"n\":1}").data(), 20);   // (keep v alive)
	CHECK(r.cast.ViewerOpen(t, why, sizeof(why)) >= 0);       // (just in time)
	int n = 0;
	for (int i = 0; i < 6; i++)
		{
		char x[32];
		r.cast.NewToken(false, x, sizeof(x));
		if (r.cast.ViewerOpen(x, why, sizeof(why)) >= 0) n++;
		}
	CHECK(n == 2 && strstr(why, "viewers"));                  // (four at most)
	r.cast.SetEnabled(false);
	CHECK(!r.cast.NewToken(false, t, sizeof(t)));
	}

static void TestWebSocket()
	{
	uint8_t h[20];
	am::Sha1((const uint8_t*)"abc", 3, h);
	static const uint8_t kAbc[20] = { 0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d };
	CHECK(memcmp(h, kAbc, 20) == 0);
	std::string big(1000, 'a');
	am::Sha1((const uint8_t*)big.data(), big.size(), h);
	static const uint8_t kThousand[20] = { 0x29, 0x1e, 0x9a, 0x6c, 0x66, 0x99, 0x49, 0x49, 0xb5, 0x7b, 0xa5, 0xe6, 0x50, 0x36, 0x1e, 0x98, 0xfc, 0x36, 0xb1, 0xba };
	CHECK(memcmp(h, kThousand, 20) == 0);        // (two blocks and a padding block)
	char acc[29];
	CHECK(am::WsAcceptKey("dGhlIHNhbXBsZSBub25jZQ==", acc) && strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
	const char* req = "GET /cast?t=0123456789abcdef01234567&x=1 HTTP/1.1\r\nHost: 192.168.1.50:5962\r\nUpgrade: WebSocket\r\n"
		"Connection: keep-alive, Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\nrest";
	am::WsRequest rq;
	size_t used = 0;
	CHECK(am::WsParseRequest(req, 40, rq, used) == 0);
	CHECK(am::WsParseRequest(req, strlen(req), rq, used) == 1 && used == strlen(req) - 4);
	CHECK(rq.get && rq.upgrade && rq.version13 && strcmp(rq.key, "dGhlIHNhbXBsZSBub25jZQ==") == 0);
	char t[40];
	CHECK(am::WsQuery(rq.path, "t", t, sizeof(t)) && strcmp(t, "0123456789abcdef01234567") == 0);
	CHECK(!am::WsQuery(rq.path, "q", t, sizeof(t)));
	const char* plain = "GET /cast HTTP/1.1\r\nHost: x\r\n\r\n";
	CHECK(am::WsParseRequest(plain, strlen(plain), rq, used) == 1 && !rq.upgrade);
	CHECK(am::WsParseRequest("not http at all\r\n\r\n", 19, rq, used) == -1);
	// frames: text, fragmented with a ping in between, then errors
	struct Got { std::vector<std::pair<int, std::string>> m; } got;
	auto on = [](void* c, int op, const uint8_t* p, size_t n) { ((Got*)c)->m.push_back({ op, std::string((const char*)p, n) }); };
	am::WsReader rd;
	Bytes s = ClientFrame(am::EWsText, "{\"t\":\"full\"}");
	Bytes f1 = ClientFrame(am::EWsText, "hel", false), pg = ClientFrame(am::EWsPing, "p"), f2 = ClientFrame(am::EWsCont, "lo");
	s.insert(s.end(), f1.begin(), f1.end());
	s.insert(s.end(), pg.begin(), pg.end());
	s.insert(s.end(), f2.begin(), f2.end());
	Bytes e = ClientFrame(am::EWsText, "");
	s.insert(s.end(), e.begin(), e.end());
	for (size_t i = 0; i < s.size(); i++)
		CHECK(rd.Feed(&s[i], 1, on, &got) == 1);
	CHECK(got.m.size() == 4);
	if (got.m.size() == 4)
		{
		CHECK(got.m[0].first == am::EWsText && got.m[0].second == "{\"t\":\"full\"}");
		CHECK(got.m[1].first == am::EWsPing && got.m[1].second == "p");
		CHECK(got.m[2].first == am::EWsText && got.m[2].second == "hello");
		CHECK(got.m[3].first == am::EWsText && got.m[3].second.empty());
		}
	am::WsReader r2;
	uint8_t unmasked[] = { 0x81, 0x01, 'x' };
	CHECK(r2.Feed(unmasked, 3, on, &got) < 0 && r2.CloseCode() == 1002);
	am::WsReader r3;
	Bytes huge = ClientFrame(am::EWsText, std::string(2000, 'x'));
	CHECK(r3.Feed(huge.data(), huge.size(), on, &got) < 0 && r3.CloseCode() == 1009);
	// a 64-bit length that would wrap the size check (the sum of it and what
	// is already joined comes out small): refused at the header, nothing written
	am::WsReader r4;
	Bytes wrap = ClientFrame(am::EWsText, "abc", false);
	uint8_t tail[] = { 0x80, 0x80 | 127, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0x12, 0x34, 0x56, 0x78, 'x', 'y' };
	wrap.insert(wrap.end(), tail, tail + sizeof(tail));
	size_t before = got.m.size();
	CHECK(r4.Feed(wrap.data(), wrap.size(), on, &got) < 0 && r4.CloseCode() == 1009);
	CHECK(got.m.size() == before);
	uint8_t hd[10];
	CHECK(am::WsHeader(hd, am::EWsBinary, 100) == 2 && hd[0] == 0x82 && hd[1] == 100);
	CHECK(am::WsHeader(hd, am::EWsBinary, 300) == 4 && hd[1] == 126 && hd[2] == 1 && hd[3] == 44);
	CHECK(am::WsHeader(hd, am::EWsBinary, 76805) == 10 && hd[1] == 127 && hd[7] == 1 && hd[8] == 0x2c && hd[9] == 0x05);
	}

// two viewers, one fast and one slow; random changes and scrolls from the
// real encoder: both end up pixel-exact, and the slow one was sent less
static void TestViewers()
	{
	Rig r;
	Psion ps;
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello());
	unsigned seed = 7;
	ps.Box(0, 0, 640, 18, 0);
	for (int y = 20; y < 228; y += 12) ps.Text(y, seed);
	r.ToAtom(ps.Update(true));
	r.Take();
	int fast = r.Viewer(), slow = r.Viewer();
	CHECK(fast >= 0 && slow >= 0);
	std::vector<Msg> m = r.Take();
	CHECK(Count(m, am::ECastViewers) >= 1 && Count(m, am::ECastKeyReq) == 1);   // (the first viewer: a keyframe)
	r.ToAtom(ps.Update(true));
	r.Take();
	ViewModel vf, vs;
	r.Pump(fast, vf);
	CHECK(vf.fulls == 1 && vf.Same(r.cast.Screen()));
	CHECK(vf.texts.size() >= 2 && vf.texts[0].find("\"t\":\"hello\"") != std::string::npos);
	size_t fastBytes = 0;
	for (int step = 0; step < 300; step++)
		{
		seed = seed * 1103515245 + 12345;
		int what = (seed >> 16) % 10;
		if (what < 3)
			ps.Scroll(seed);
		else if (what < 8)
			ps.Box((int)((seed >> 8) % 600), 20 + (int)((seed >> 4) % 200), 8 + (int)(seed % 120), 4 + (int)((seed >> 12) % 30),
				(int)((seed >> 20) % 16));
		else
			ps.Text(20 + 12 * (int)((seed >> 9) % 17), seed);
		Bytes u = ps.Update(false);
		if (!u.empty())
			r.ToAtom(u);
		r.Take();
		CHECK(memcmp(r.cast.Screen(), ps.frame.data(), ps.frame.size()) == 0);
		size_t before = vf.bytes;
		r.Pump(fast, vf);
		fastBytes += vf.bytes - before;
		CHECK(vf.Same(r.cast.Screen()));
		if (step % 25 == 0)
			r.Pump(slow, vs, 3000);                  // (the slow viewer: 3 KB now and then)
		}
	r.Pump(slow, vs);
	CHECK(vs.Same(r.cast.Screen()) && vf.Same(r.cast.Screen()));
	CHECK(vs.bytes < vf.bytes / 2);
	CHECK(vf.scrolls > 20);
	printf("  viewers: fast %zu bytes (%d rects, %d scrolls), slow %zu bytes (%d rects, %d scrolls)\n", vf.bytes, vf.rects,
		vf.scrolls, vs.bytes, vs.rects, vs.scrolls);
	// a repaint asked for
	r.cast.ViewerIn(fast, ClientFrame(am::EWsText, "{\"t\":\"full\"}").data(), 18);
	r.Pump(fast, vf);
	CHECK(vf.fulls == 2 && vf.Same(r.cast.Screen()));
	// the ping answered
	Bytes pg = ClientFrame(am::EWsText, "{\"t\":\"ping\",\"n\":42}");
	r.cast.ViewerIn(fast, pg.data(), pg.size());
	r.Pump(fast, vf);
	CHECK(vf.texts.back() == "{\"t\":\"pong\",\"n\":42}");
	// a viewer that goes quiet is closed after 45 s (the slow one keeps pinging)
	for (int i = 0; i < 10; i++)
		{
		r.Ms(5000);
		r.cast.ViewerIn(slow, pg.data(), pg.size());
		r.ToAtom(Psion::Status());
		r.Pump(fast, vf);
		r.Pump(slow, vs);
		}
	CHECK(r.cast.ViewerDone(fast) && !r.cast.ViewerDone(slow));
	r.cast.ViewerClose(fast);
	CHECK(r.cast.Viewers() == 1);
	// the link goes: the viewer is told
	r.cast.LinkClose(r.id);
	r.Pump(slow, vs);
	bool bye = false, down = false;
	for (const std::string& t : vs.texts)
		{
		if (t == "{\"t\":\"bye\"}") bye = true;
		if (t.find("\"linkUp\":false") != std::string::npos) down = true;
		}
	CHECK(bye && down);
	CHECK(vs.Same(r.cast.Screen()));              // (the last picture stays)
	}

// the last status JSON a viewer was sent
static std::string LastStatus(const ViewModel& aV)
	{
	for (size_t i = aV.texts.size(); i-- > 0;)
		if (aV.texts[i].find("\"t\":\"status\"") != std::string::npos)
			return aV.texts[i];
	return "";
	}

// Latency (PROTOCOL.md): stamps asked for only from a Psion that can; the
// clocks compared by PING and the STATUS that answers it; capture -> applied
// in the viewers' status; a viewer's wait and WebSocket round trip
static void TestLatency()
	{
	// an older Psion (no HELLO bit2): no stamps asked for, no extra PINGs
		{
		Rig r;
		Psion ps;
		r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
		r.ToAtom(Psion::Hello(3));
		std::vector<Msg> m = r.Take();
		CHECK(m.size() == 2 && m[0].type == am::ECastWelcome && Le32(m[0].p.data() + 4) == 1);
		r.ToAtom(ps.Update(true));
		for (int i = 0; i < 40; i++)              // (busy: an update every 100 ms for 4 s)
			{
			r.Ms(100);
			ps.Box(10 * i, 50, 8, 8, i & 15);
			r.ToAtom(ps.Update(false));
			}
		m = r.Take();
		CHECK(Count(m, am::ECastPing) == 0);
		int v = r.Viewer();
		ViewModel vm;
		r.Ms(1100);
		r.Pump(v, vm);
		CHECK(LastStatus(vm).find("\"lat\":{\"link\":null,\"linkMax\":null,\"acc\":null") != std::string::npos);
		}
	Rig r;
	Psion ps;
	const uint32_t kOff = 0x80000123u;           // (the Psion's clock: the Atom's + this, wrapping)
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(7));
	std::vector<Msg> m = r.Take();
	CHECK(m.size() >= 1 && m[0].type == am::ECastWelcome && Le32(m[0].p.data() + 4) == 3);
	int v = r.Viewer();                          // (the first viewer: KEYREQ, so before the keyframe)
	ViewModel vm;
	ps.stamp = true;
	ps.stampMs = r.hal.Millis() + kOff;
	r.ToAtom(ps.Update(true));                   // (no offset yet: applied, no figure)
	CHECK(memcmp(r.cast.Screen(), ps.frame.data(), ps.frame.size()) == 0);
	r.Ms(50);                                    // the clocks are compared at once
	m = r.Take();
	uint32_t token = 0;
	for (const Msg& x : m)
		if (x.type == am::ECastPing && x.p.size() == 8)
			token = Le32(x.p.data());
	CHECK(token != 0);
	// a STATUS that answers no PING, and one with the wrong token: ignored
	r.ToAtom(Psion::StatusClock(r.hal.Millis() + kOff + 5000, 0));
	r.ToAtom(Psion::StatusClock(r.hal.Millis() + kOff + 5000, token + 1));
	// the answer, 40 ms later; the Psion read its clock half-way
	uint32_t sent = r.hal.Millis();
	r.hal.Advance(40000);
	r.ToAtom(Psion::StatusClock(sent + 20 + kOff, token));
	r.Take();
	r.Pump(v, vm);
	// captured now, taken 120 ms later; then 300 ms
	ps.Box(100, 100, 50, 20, 7);
	ps.stampMs = r.hal.Millis() + kOff;
	r.hal.Advance(120000);
	r.ToAtom(ps.Update(false));
	r.Pump(v, vm);                               // (sent on at once: a wait of 0)
	ps.Box(200, 100, 50, 20, 3);
	ps.stampMs = r.hal.Millis() + kOff;
	r.hal.Advance(300000);
	r.ToAtom(ps.Update(false));
	CHECK(memcmp(r.cast.Screen(), ps.frame.data(), ps.frame.size()) == 0);
	r.Ms(1000);
	r.Pump(v, vm);
	std::string st = LastStatus(vm);
	CHECK(st.find("\"lat\":{\"link\":210,\"linkMax\":300,\"acc\":36,\"queue\":0,\"view\":null}") != std::string::npos);
	if (st.find("\"link\":210") == std::string::npos)
		printf("  status: %s\n", st.c_str());
	// the viewer's WebSocket round trip: our ping, its pong 30 ms later
	size_t had = vm.pings.size();
	for (int i = 0; i < 50 && vm.pings.size() == had; i++)
		{
		r.Ms(50);
		r.Pump(v, vm);
		}
	CHECK(vm.pings.size() == had + 1 && vm.pings.back().size() == 4);
	if (!vm.pings.empty())
		{
		r.hal.Advance(30000);
		std::string pl(vm.pings.back().begin(), vm.pings.back().end());
		uint32_t rtt = r.hal.Millis() - Le32(vm.pings.back().data());   // (its payload: when it went)
		CHECK(rtt >= 30 && rtt < 100);
		Bytes pong = ClientFrame(am::EWsPong, pl);
		r.cast.ViewerIn(v, pong.data(), pong.size());
		r.Ms(1000);
		r.Pump(v, vm);
		char want[32];
		snprintf(want, sizeof(want), "\"view\":%u}", (unsigned)rtt);
		CHECK(LastStatus(vm).find(want) != std::string::npos);
		}
	// busy (an update every 100 ms): the clocks compared again every 5 s
	r.Take();
	int pings = 0;
	for (int i = 0; i < 110; i++)
		{
		r.Ms(100);
		ps.Box(5 * i, 150, 4, 4, i & 15);
		ps.stampMs = r.hal.Millis() + kOff;
		r.ToAtom(ps.Update(false));
		m = r.Take();
		for (const Msg& x : m)
			if (x.type == am::ECastPing)
				{
				pings++;
				r.ToAtom(Psion::StatusClock(r.hal.Millis() + kOff, Le32(x.p.data())));
				}
		r.Pump(v, vm);
		}
	CHECK(pings >= 2 && pings <= 3);
	CHECK(LastStatus(vm).find("\"link\":0,") != std::string::npos);
	// the Psion was switched off (its clock stopped for a minute): the figure
	// that cannot be drops the offset, and the clocks are compared again
	ps.Box(0, 200, 30, 30, 0);
	ps.stampMs = r.hal.Millis() + kOff - 61000;
	r.ToAtom(ps.Update(false));
	r.Ms(50);
	m = r.Take();
	CHECK(Count(m, am::ECastPing) == 1);
	}

static void TestInput()
	{
	Rig r;
	Psion ps;
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(3));
	r.ToAtom(ps.Update(true));
	int watch = r.Viewer(false), ctl = r.Viewer(true);
	r.Take();
	auto say = [&](int v, const char* j) { Bytes f = ClientFrame(am::EWsText, j); r.cast.ViewerIn(v, f.data(), f.size()); };
	say(watch, "{\"t\":\"pen\",\"e\":\"down\",\"x\":10,\"y\":20}");
	r.Ms(100);
	CHECK(Count(r.Take(), am::ECastInput) == 0);  // (no control asked for)
	say(ctl, "{\"t\":\"pen\",\"e\":\"down\",\"x\":-27,\"y\":262}");
	say(ctl, "{\"t\":\"pen\",\"e\":\"up\",\"x\":-27,\"y\":262}");
	say(ctl, "{\"t\":\"key\",\"e\":\"down\",\"sc\":148}");
	say(ctl, "{\"t\":\"key\",\"e\":\"up\",\"sc\":148}");
	say(ctl, "{\"t\":\"char\",\"c\":163}");
	r.Ms(100);
	std::vector<Msg> m = r.Take();
	CHECK(Count(m, am::ECastInput) == 1);
	for (const Msg& x : m)
		if (x.type == am::ECastInput)
			{
			CHECK(x.p.size() == 40);
			if (x.p.size() == 40)
				{
				CHECK(x.p[0] == 1 && (int16_t)Le16(&x.p[2]) == -27 && Le16(&x.p[4]) == 262);
				CHECK(x.p[8] == 3 && x.p[16] == 4 && Le16(&x.p[18]) == 148 && x.p[24] == 5 && x.p[32] == 6 && Le16(&x.p[34]) == 163);
				}
			}
	// the Psion turns control off (STATUS): input stops
	r.ToAtom(Psion::Status(80, false));
	say(ctl, "{\"t\":\"key\",\"e\":\"down\",\"sc\":4}");
	r.Ms(100);
	CHECK(Count(r.Take(), am::ECastInput) == 0);
	// the viewer count with control reaches the Psion
	r.ToAtom(Psion::Status(80, true));
	r.Ms(100);
	m = r.Take();
	(void)m;
	CHECK(r.cast.ViewersWithControl() == 1);
	}

// the modem route: ATDT192.168.7.1:5961 is a call to PsiCast, in the modem
static void TestModemRoute()
	{
	FakeHal hal;
	hal.webInfo = "http://192.168.1.50/";
	std::vector<uint8_t> ring(64 * 1024);
	am::Cast cast(hal);
	am::Modem modem(hal, ring.data(), ring.size());
	am::CastExt ext;                             // (the module, on a service of the test's own)
	ext.Use(cast, hal);
	modem.AddExtension(ext);
	modem.Begin();
	auto run = [&](uint32_t ms) { for (uint32_t t = 0; t < ms * 1000; t += 200) { modem.Loop(); cast.Tick(); hal.Advance(200); } };
	auto type = [&](const char* s) { while (*s) hal.rx.push_back((uint8_t)*s++); };
	run(50);
	CHECK(cast.Enabled());                       // (on as standard)
	type("ATDT192.168.7.1:5961\r");
	run(200);
	std::string w = hal.Wire();
	CHECK(w.find("CONNECT") != std::string::npos && modem.ExtCall() == &ext);
	CHECK(hal.tcpHost.empty());                  // (no outside connection)
	size_t mark = hal.wire.size();
	Bytes h = Psion::Hello();
	for (uint8_t b : h) hal.rx.push_back(b);
	run(300);
	Bytes back;
	for (size_t i = mark; i < hal.wire.size(); i++) back.push_back(hal.wire[i].c);
	std::vector<Msg> m = Parse(back);
	CHECK(m.size() >= 2 && m[0].type == am::ECastWelcome);
	CHECK(cast.LinkUp() && cast.Route() == am::Cast::ERouteModem);
	// "+++" inside a message, with a guard time's quiet either side: data
	{
	Bytes odd = Frame(0x7f, 0, Bytes({ 'a', 'b', '+', '+', '+', 'c' }));   // (a type it ignores)
	for (size_t i = 0; i < 10; i++) hal.rx.push_back(odd[i]);
	run(1500);
	for (size_t i = 10; i < 13; i++) hal.rx.push_back(odd[i]);
	run(1500);
	for (size_t i = 13; i < odd.size(); i++) hal.rx.push_back(odd[i]);
	run(300);
	CHECK(modem.ExtCall() == &ext && modem.Online() && cast.LinkUp() && cast.Errors() == 0);
	CHECK(hal.Wire().find("OK", mark) == std::string::npos);
	}
	// +++ and ATH between messages: the link ends with the call
	run(1000);
	type("+++");
	run(1200);
	type("ATH\r");
	run(200);
	CHECK(!modem.ExtCall() && !cast.LinkUp());
	// off: the dial gives NO CARRIER and opens nothing
	type("AT$CAST=0\r");
	run(100);
	CHECK(!cast.Enabled());
	mark = hal.Wire().size();
	type("ATDT192.168.7.1:5961\r");
	run(200);
	CHECK(hal.Wire().substr(mark).find("NO CARRIER") != std::string::npos && hal.tcpHost.empty());
	type("AT$CAST?\r");
	run(100);
	CHECK(hal.Wire().find("0") != std::string::npos);
	// another address or port is an ordinary call, as before
	type("AT$CAST=1\r");
	run(100);
	type("ATDT192.168.7.1:5962\r");
	run(200);
	CHECK(hal.tcpHost == "192.168.7.1" && hal.tcpPort == 5962 && !modem.ExtCall());
	}

// the module on the firmware's hooks (ext.h): its setting in the schema, its
// lines in ATI and AT$STAT?, the LCD's Call page, and its web routes, as they
// were when PsiCast was in the firmware itself
struct TestQuery : am::ExtQuery
	{
	const char* control = "";
	const char* Arg(const char* aName) const override { return strcmp(aName, "control") == 0 ? control : ""; }
	};

static void TestHooks()
	{
	// registered: AT$CAST is in the schema, under PsiCast, in Settings2's byte 442
	const am::SchemaEntry* e = am::SchemaFind("CAST");
	CHECK(e && e->group == am::SGExt && e->rec == am::SR2 && e->offset == 442 && e->type == am::STBoolInv);
	CHECK(e && strcmp(am::SchemaGroupNameOf(*e), "PsiCast") == 0 && am::ExtSchemaOwner(*e) == &am::TheCastExt());
	char line[256] = "";
	for (int i = 0; i < am::SchemaCount(); i++)
		if (&am::SchemaAt(i) == e)
			am::SchemaLine(i, line, sizeof(line));
	CHECK(strcmp(line, "+SCHEMA:CAST,BOOL,0,1,10,,\"PsiCast\",\"PsiCast\"") == 0);
	if (strcmp(line, "+SCHEMA:CAST,BOOL,0,1,10,,\"PsiCast\",\"PsiCast\"") != 0)
		printf("  %s\n", line);
	// a bad byte in a record read from NVS is made valid (on)
	am::Settings2 s2;
	am::FactoryDefaults2(s2);
	CHECK(s2.ext[0] == 0);
	s2.ext[0] = 7;
	CHECK(am::AcceptSettings2(s2, sizeof(s2)) && s2.ext[0] == 0);

	FakeHal hal;
	hal.webInfo = "http://192.168.1.50/";
	std::vector<uint8_t> ring(64 * 1024);
	am::Cast cast(hal);
	am::Modem modem(hal, ring.data(), ring.size());
	am::CastExt ext;
	ext.Use(cast, hal);
	CHECK(modem.AddExtension(ext) && modem.ExtensionCount() == 1);
	modem.Begin();
	auto run = [&](uint32_t ms) { for (uint32_t t = 0; t < ms * 1000; t += 200) { modem.Loop(); cast.Tick(); hal.Advance(200); } };
	auto type = [&](const char* s) { while (*s) hal.rx.push_back((uint8_t)*s++); };
	run(50);
	size_t mark = hal.Wire().size();
	type("AT$STAT?\r");
	run(100);
	std::string w = hal.Wire().substr(mark);
	CHECK(w.find("+STAT:CAST=WAIT\r\n+STAT:CASTV=0\r\n") != std::string::npos);
	CHECK(w.find("+STAT:WGTX=") < w.find("+STAT:CAST=") && w.find("+STAT:CASTV=") < w.find("+STAT:FW="));
	mark = hal.Wire().size();
	type("ATI\r");
	run(100);
	CHECK(hal.Wire().substr(mark).find("PsiCast: ") != std::string::npos);
	char row[40];
	CHECK(ext.CallPageRow(true, row, sizeof(row)) && strcmp(row, "On, no Psion") == 0);
	CHECK(!ext.CallPageRow(false, row, sizeof(row)));
	CHECK(strcmp(ext.WebHome(), "/screen") == 0 && ext.WebAction() && strcmp(ext.WebAction(), "show the screen") == 0);
	// the routes: which are open (no password), and the replies
	int n = 0, open = 0;
	for (const am::ExtRoute* r = ext.Routes(); r->path; r++, n++)
		open += r->open;
	CHECK(n == 9 && open == 3);
	TestQuery q;
	{ am::ExtReply r; ext.Get("/screen", q, r); CHECK(r.status == 200 && r.gzip && r.len > 1000 && strcmp(r.cache, "no-cache") == 0); }
	{ am::ExtReply r; ext.Get("/screen/manifest.json", q, r); CHECK(r.status == 200 && !r.gzip && strstr((const char*)r.body, "\"start_url\":\"/screen\"")); }
	{ am::ExtReply r; ext.Get("/screen/s7icons.json", q, r); CHECK(r.status == 200 && r.gzip && r.body[0] == 0x1f && strcmp(r.cache, "max-age=604800") == 0); }
	{ am::ExtReply r; ext.Get("/screen/silk5mx.jpg", q, r); CHECK(r.status == 200 && !r.gzip && r.body[0] == 0xff); }
	{
	q.control = "1";
	am::ExtReply r;
	ext.Get("/screen/token", q, r);
	CHECK(r.status == 200 && strstr(r.text, "\"port\":5962,\"control\":true}") && strcmp(r.cache, "no-store") == 0);
	}
	// off (AT$CAST=0): no tokens, the dial refused, and the rows say so
	type("AT$CAST=0\r");
	run(100);
	{ am::ExtReply r; ext.Get("/screen/token", q, r); CHECK(r.status == 403 && strcmp(r.text, "{\"error\":\"PsiCast is off\",\"why\":\"off\"}") == 0); }
	CHECK(ext.CallPageRow(true, row, sizeof(row)) && strcmp(row, "Off") == 0 && !ext.WebAction());
	mark = hal.Wire().size();
	type("AT$STAT?\r");
	run(100);
	CHECK(hal.Wire().substr(mark).find("+STAT:CAST=OFF\r\n") != std::string::npos);
	}

// the LCD's Sharing page and the LED's light (castlcd.h): the picture's
// box-averaging, when the page is up, its picture's pace, and the light
static void TestLcd()
	{
	// the page: up over Home while anyone is watching, gone when the last
	// leaves; one of the button's pages always
	Rig r;
	am::CastExt ext;
	ext.Use(r.cast, r.hal);
	am::ExtLed led;
	CHECK(ext.HasPage() && !ext.ScreenWanted() && !ext.LedWanted(led));
	am::CastLcdView v;
	ext.iLcd.View(r.cast, r.hal.Millis(), v);
	CHECK(v.enabled && !v.live && strcmp(v.address, "192.168.1.50/screen") == 0 && v.kbdVia == -1);
	// the picture: the System screen from flash, the same whatever the Psion shows
	CHECK(v.pic && v.pic[0] % 17 == 0);
	std::vector<uint8_t> pic(v.pic, v.pic + am::kCastPicW * am::kCastPicH);
	int greys[16] = { 0 };
	for (uint8_t g : pic) greys[g / 17]++;
	int used = 0;
	for (int g : greys) used += g > 0;
	CHECK(used >= 6 && greys[0] > 0);             // (a real screen: blacks and many greys)
	int a = r.Viewer(false);
	CHECK(a >= 0 && !ext.ScreenWanted());         // (watching, the Psion not sharing: not over Home)
	CHECK(ext.LedWanted(led) && led.style == am::EExtLedPulse && led.over == am::EExtLedOverCall && led.b > 0 && led.r == 0);
	ext.iLcd.View(r.cast, r.hal.Millis(), v);
	CHECK(!v.live && v.viewers == 1 && !v.control);
	uint32_t n0 = ext.ScreenNews();
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(3));
	Psion ps;
	ps.Box(0, 0, 320, 120, 0);
	r.ToAtom(ps.Update(true));
	r.Take();
	CHECK(ext.ScreenNews() != n0);                // (the Psion came: worth waking for)
	ext.iLcd.View(r.cast, r.hal.Millis(), v);
	CHECK(v.live && !v.haveDelay && v.pic && memcmp(v.pic, pic.data(), pic.size()) == 0);
	// the Psion's screen changing changes nothing on the page
	am::Layout l0, l1;
	am::StatusModel::LayExt(ext, r.hal.Millis(), l0);
	ps.Box(320, 0, 320, 240, 3);
	r.ToAtom(ps.Update(false));
	r.Take();
	r.Ms(600);
	am::StatusModel::LayExt(ext, r.hal.Millis(), l1);
	CHECK(l0.dropped == 0 && l0.n < am::kDrawOps && l0.pic && memcmp(&l0, &l1, sizeof(l0)) == 0);
	// control: the page's line and a quick blink
	uint32_t n1 = ext.ScreenNews();
	int c = r.Viewer(true);
	r.Ms(100);
	CHECK(c >= 0 && r.cast.Controlled() && ext.ScreenNews() != n1);
	ext.iLcd.View(r.cast, r.hal.Millis(), v);
	CHECK(v.control && v.viewers == 2);
	CHECK(ext.LedWanted(led) && led.style == am::EExtLedBlink);
	r.ToAtom(Psion::Status(80, false));          // the Psion takes control back
	r.Ms(100);
	CHECK(!r.cast.Controlled() && ext.LedWanted(led) && led.style == am::EExtLedPulse);
	// the last viewer leaves: not wanted over Home (still a page of the ring)
	r.cast.ViewerClose(c);
	CHECK(ext.ScreenWanted());
	r.cast.ViewerClose(a);
	CHECK(!ext.ScreenWanted() && !ext.LedWanted(led) && ext.HasPage());
	// off: never wanted, viewers or not; the page says so
	int b = r.Viewer(false);
	CHECK(b >= 0 && ext.ScreenWanted());
	r.cast.SetEnabled(false);
	CHECK(!ext.ScreenWanted() && !ext.LedWanted(led));
	ext.iLcd.View(r.cast, r.hal.Millis(), v);
	CHECK(!v.enabled);
	}

// the picture delay on the page: a new figure at most every 5 s
static void TestLcdDelay()
	{
	Rig r;
	Psion ps;
	ps.stamp = true;
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(3 | 4));
	r.ToAtom(ps.Update(true));
	am::CastLcd lcd;
	am::CastLcdView v;
	const uint32_t kOff = 123456;
	std::vector<uint32_t> shown;
	for (int i = 0; i < 40; i++)
		{
		// a PING answered with the Psion's clock, then an update stamped
		// i ms before it arrives (a delay that keeps changing)
		r.Take();
		r.Ms(300);
		std::vector<Msg> m = r.Take();
		for (const Msg& x : m)
			if (x.type == am::ECastPing)
				r.ToAtom(Psion::StatusClock(r.hal.Millis() + kOff, Le32(x.p.data())));
		ps.Box(i * 10, 30, 10, 10, i & 15);
		ps.stampMs = r.hal.Millis() + kOff - 20 - (uint32_t)i;
		r.ToAtom(ps.Update(false));
		r.Ms(800);
		lcd.View(r.cast, r.hal.Millis(), v);
		if (v.haveDelay && (shown.empty() || shown.back() != v.delayMs))
			shown.push_back(v.delayMs);
		}
	// 40 s of a changing figure: about 8 changes on the page, not 40
	CHECK(!shown.empty() && shown.size() <= 9);
	if (shown.empty() || shown.size() > 9) printf("  delay changes on the page: %d\n", (int)shown.size());
	}

// ----- the phone keyboard ------------------------------------------------------------

// a phone behind KbdLink: started or not, connected or not, and the reports
// it took (at most perPass between two looks, to play a busy link)
struct FakePhone : am::KbdLink
	{
	bool usable = true, started = false, ready = false, paired = false;
	int starts = 0, stops = 0, forgets = 0;
	int perPass = 1000, taken = 0;
	std::vector<std::vector<uint8_t> > reports;
	int why = am::EKbdWhyFailed;               // (what it says when not usable)
	bool Usable(char* aWhy, size_t aMax, int& aCode) override
		{
		if (!usable)
			{
			snprintf(aWhy, aMax, why == am::EKbdWhyMemory ? "not enough memory" : "no radio");
			aCode = why;
			}
		return usable;
		}
	void Start() override { started = true; starts++; }
	void Stop() override { started = false; ready = false; stops++; }
	bool Ready() const override { return started && ready; }
	bool Send(const uint8_t aR[8]) override
		{
		if (!Ready() || taken >= perPass)
			return false;
		taken++;
		reports.push_back(std::vector<uint8_t>(aR, aR + 8));
		return true;
		}
	void Loop(uint32_t) override { taken = 0; }
	bool Paired() const override { return paired; }
	bool Forget() override { forgets++; paired = false; return true; }
	void PhoneName(char* aOut, size_t aMax) const override { snprintf(aOut, aMax, "Dan's iPhone"); }
	// the reports as "mods:key" ("2:04" Shift and A), and cleared
	std::string Take()
		{
		std::string o;
		char b[12];
		for (auto& r : reports)
			{
			snprintf(b, sizeof(b), "%s%x:%02x", o.empty() ? "" : " ", r[0], r[2]);
			o += b;
			}
		reports.clear();
		return o;
		}
	};

static Bytes Ev(int aKind, int aMods, unsigned aCode)
	{
	return Bytes({ (uint8_t)aKind, (uint8_t)aMods, (uint8_t)aCode, (uint8_t)(aCode >> 8) });
	}
static Bytes Chars(const char* aText, int aMods = 0)
	{
	Bytes b;
	for (const unsigned char* p = (const unsigned char*)aText; *p; p++)
		{
		Bytes e = Ev(am::EKbdChar, aMods, *p);
		b.insert(b.end(), e.begin(), e.end());
		}
	return b;
	}
static Bytes operator+(Bytes a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); return a; }

static std::string Strokes(int aLayout, unsigned aCode)
	{
	am::KbdStroke s[2];
	int n = am::KbdStrokes(aLayout, aCode, s);
	std::string o;
	char b[12];
	for (int i = 0; i < n; i++)
		{
		snprintf(b, sizeof(b), "%s%x:%02x", i ? " " : "", s[i].mods, s[i].usage);
		o += b;
		}
	return o;
	}

static void TestKbdMap()
	{
	const int UK = am::EKbdBritish, US = am::EKbdUs;
	// ASCII, the same on both
	CHECK(Strokes(UK, 'a') == "0:04" && Strokes(UK, 'A') == "2:04" && Strokes(US, 'z') == "0:1d");
	CHECK(Strokes(UK, '1') == "0:1e" && Strokes(UK, '0') == "0:27" && Strokes(UK, ' ') == "0:2c");
	CHECK(Strokes(UK, '@') == "2:1f" && Strokes(US, '@') == "2:1f" && Strokes(UK, '"') == "2:34");
	CHECK(Strokes(UK, '\\') == "0:31" && Strokes(UK, '|') == "2:31" && Strokes(US, '?') == "2:38");
	// where the layouts differ: # and £, € and ™, ` and ~
	CHECK(Strokes(UK, '#') == "4:20" && Strokes(US, '#') == "2:20");
	CHECK(Strokes(UK, 0xa3) == "2:20" && Strokes(US, 0xa3) == "4:20");
	CHECK(Strokes(UK, 0x80) == "4:1f" && Strokes(US, 0x80) == "6:1f");
	CHECK(Strokes(UK, 0x99) == "6:1f" && Strokes(US, 0x99) == "4:1f");
	CHECK(Strokes(UK, '`') == "0:64" && Strokes(US, '`') == "0:35" && Strokes(UK, '~') == "2:64" && Strokes(US, '~') == "2:35");
	// Option's characters, and the dead keys
	CHECK(Strokes(UK, 0xac) == "4:0f" && Strokes(UK, 0xa7) == "4:23" && Strokes(US, 0xb0) == "6:25");   // ¬ § °
	CHECK(Strokes(UK, 0xe7) == "4:06" && Strokes(UK, 0xc7) == "6:06" && Strokes(UK, 0xdf) == "4:16");   // ç Ç ß
	CHECK(Strokes(UK, 0xe9) == "4:08 0:08" && Strokes(UK, 0xc9) == "4:08 2:08");                        // é É
	CHECK(Strokes(UK, 0xe0) == "4:64 0:04" && Strokes(US, 0xe0) == "4:35 0:04");                        // à
	CHECK(Strokes(US, 0xfc) == "4:18 0:18" && Strokes(US, 0x9f) == "4:18 2:1c");                        // ü Ÿ
	CHECK(Strokes(UK, 0xf1) == "4:11 0:11" && Strokes(UK, 0xd5) == "4:11 2:12");                        // ñ Õ
	// what cannot be made
	CHECK(Strokes(UK, 0xd7) == "" && Strokes(UK, 0xa4) == "" && Strokes(US, 0xfe) == "" && Strokes(UK, 0x8a) == "");
	CHECK(Strokes(UK, 13) == "" && Strokes(UK, 9) == "" && Strokes(UK, 0) == "" && Strokes(UK, 0x7f) == "" && Strokes(UK, 0x100) == "");
	// every code, both layouts: only known keys and Shift/Option; every
	// printable ASCII made; no two characters on the same single stroke; a
	// dead key's second stroke a plain letter
	for (int lay = 0; lay < 2; lay++)
		{
		std::vector<std::string> seen;
		int made = 0;
		for (unsigned c = 0; c < 0x110; c++)
			{
			am::KbdStroke st[2];
			int n = am::KbdStrokes(lay, c, st);
			CHECK(n >= 0 && n <= 2);
			if (c >= 0x20 && c < 0x7f)
				CHECK(n == 1);
			for (int i = 0; i < n; i++)
				CHECK(st[i].usage >= 0x04 && st[i].usage <= 0x65 && (st[i].mods & ~(am::kKbdShift | am::kKbdAlt)) == 0);
			if (n == 2)
				CHECK(st[0].mods == am::kKbdAlt && st[1].usage <= 0x1d && !(st[1].mods & am::kKbdAlt));
			if (n == 1)
				{
				std::string k = Strokes(lay, c);
				for (const std::string& x : seen)
					CHECK(x != k);
				seen.push_back(k);
				}
			made += n > 0;
			}
		CHECK(made > 160);
		}
	}

// PhoneKbd on its own, with a fake phone
static void TestKbdKeys()
	{
	FakeHal hal;
	FakePhone bt, usb;
	am::PhoneKbd k(hal);
	k.SetLinks(&bt, &usb);
	CHECK(k.Built() && k.State() == am::EKbdOff);   // (not yet configured)
	k.Configure(true, am::EKbdViaBt, am::EKbdBritish);
	CHECK(k.State() == am::EKbdReady && k.Via() == am::EKbdViaBt && !bt.started);
	auto keys = [&](const Bytes& b) { return k.Keys(b.data(), (uint32_t)b.size()); };
	auto loop = [&](int n = 1) { for (int i = 0; i < n; i++) { hal.Advance(10000); k.Loop(); } };
	// out of keyboard mode: nothing reaches a phone, and none is started
	CHECK(keys(Chars("hi")) && bt.reports.empty() && !bt.started && k.Dropped() == 2);
	// MODE 1: advertising; typing before the phone is there goes nowhere
	CHECK(keys(Ev(am::EKbdMode, 0, 1)) && bt.started && k.State() == am::EKbdWaiting && k.InMode());
	keys(Chars("x"));
	loop();
	CHECK(k.Queued() == 0 && bt.reports.empty());
	// the phone connects: a clean start (nothing down)
	bt.ready = true;
	loop();
	CHECK(k.State() == am::EKbdTyping && bt.Take() == "0:00");
	char name[32];
	k.Name(name, sizeof(name));
	CHECK(strcmp(name, "Dan's iPhone") == 0);
	// characters: a press and a release each; Shift as the character needs
	keys(Chars("aA#"));
	CHECK(bt.Take() == "0:04 0:00 2:04 0:00 4:20 0:00");
	// Ctrl+letter is the letter with Ctrl; the event's Shift is ignored
	keys(Ev(am::EKbdChar, am::kKbdCtrl, 'c') + Ev(am::EKbdChar, am::kKbdCtrl | am::kKbdShift, 'C') + Ev(am::EKbdChar, am::kKbdShift, 'c'));
	CHECK(bt.Take() == "1:06 0:00 3:06 0:00 0:06 0:00");
	// a dead key and its letter
	keys(Chars("\xe9"));
	CHECK(bt.Take() == "4:08 0:00 0:08 0:00");
	// HOLD: Cmd held across Tab, Tab (the app switcher), then let go
	keys(Ev(am::EKbdHold, am::kKbdGui, 0) + Ev(am::EKbdKey, 0, 0x2b) + Ev(am::EKbdKey, 0, 0x2b) + Ev(am::EKbdHold, 0, 0));
	CHECK(bt.Take() == "8:00 8:2b 8:00 8:2b 8:00 0:00");
	// a held Shift is not in a CHAR (the character says), but is in a KEY
	keys(Ev(am::EKbdHold, am::kKbdShift, 0) + Chars("a") + Ev(am::EKbdKey, 0, 0x50) + Ev(am::EKbdKey, am::kKbdAlt, 0x50) + Ev(am::EKbdHold, 0, 0));
	CHECK(bt.Take() == "2:00 0:04 2:00 2:50 2:00 6:50 2:00 0:00");
	// keys that are not: a modifier as a KEY, usage 0, beyond the report map;
	// a kind it does not know; a character the layout cannot make
	uint32_t d0 = k.Dropped();
	keys(Ev(am::EKbdKey, 0, 0xe1) + Ev(am::EKbdKey, 0, 0) + Ev(am::EKbdKey, 0, 0x66) + Ev(9, 0, 'a') + Chars("\xd7"));
	CHECK(bt.Take() == "" && k.Dropped() == d0 + 4);
	// bad messages: empty, not whole events, more than 64 events: nothing taken
	Bytes sixty4, sixty5;
	for (int i = 0; i < 64; i++) sixty4 = sixty4 + Ev(am::EKbdChar, 0, 'q');
	sixty5 = sixty4 + Ev(am::EKbdChar, 0, 'q');
	CHECK(!k.Keys(sixty4.data(), 0) && !k.Keys(sixty4.data(), 5) && !keys(sixty5) && bt.reports.empty());
	CHECK(k.BadMessages() == 3);
	// 64 events into a busy link (3 reports a pass): all of them, in order
	bt.perPass = 3;
	bt.taken = 0;
	CHECK(keys(sixty4));
	CHECK(bt.reports.size() == 3 && k.Queued() == 125);
	loop(50);
	CHECK(bt.reports.size() == 128 && k.Queued() == 0);
	bool order = true;
	for (size_t i = 0; i < bt.reports.size(); i++)
		order = order && bt.reports[i][2] == (i % 2 ? 0 : 0x14);
	CHECK(order);
	bt.reports.clear();
	// a link that takes nothing: the queue fills, the rest is dropped whole,
	// and a HOLD 0 still gets through (what waits goes)
	bt.perPass = 0;
	keys(Ev(am::EKbdHold, am::kKbdGui, 0));
	keys(sixty4);
	keys(sixty4);
	keys(sixty4);
	CHECK(k.Queued() == am::PhoneKbd::kQueue - 1);   // (the last pair did not fit)
	keys(Ev(am::EKbdHold, am::kKbdAlt, 0));
	CHECK(k.Queued() == am::PhoneKbd::kQueue);
	keys(Ev(am::EKbdHold, 0, 0));
	CHECK(k.Queued() == 1);
	bt.perPass = 1000;
	loop();
	CHECK(bt.Take() == "0:00");
	// the phone goes: everything let go; when it is back, a clean start
	keys(Ev(am::EKbdHold, am::kKbdGui, 0));
	bt.Take();
	bt.ready = false;
	loop();
	CHECK(k.State() == am::EKbdWaiting && bt.started);
	bt.ready = true;
	loop();
	keys(Chars("a"));
	CHECK(bt.Take() == "0:00 0:04 0:00");            // (no Cmd: let go with the phone)
	// MODE 0: let go, then stopped (the phone's own keyboard comes back)
	keys(Ev(am::EKbdHold, am::kKbdGui, 0));
	bt.Take();
	int stops = bt.stops;
	keys(Ev(am::EKbdMode, 0, 0));
	CHECK(bt.stops == stops + 1 && !bt.started && bt.Take() == "0:00" && k.State() == am::EKbdReady && !k.InMode());
	keys(Chars("a"));
	CHECK(bt.reports.empty());
	// link A closing ends keyboard mode too
	keys(Ev(am::EKbdMode, 0, 1));
	bt.ready = true;
	loop();
	CHECK(k.State() == am::EKbdTyping);
	k.LinkGone();
	CHECK(!bt.started && k.State() == am::EKbdReady);
	// the settings: off stops it, a change of link moves to the other, a link
	// that cannot be used now is not started (and says why)
	keys(Ev(am::EKbdMode, 0, 1));
	CHECK(bt.started);
	k.Configure(false, am::EKbdViaBt, am::EKbdBritish);
	CHECK(!bt.started && k.State() == am::EKbdOff && k.Via() == am::EKbdViaNone);
	k.Configure(true, am::EKbdViaBt, am::EKbdBritish);
	CHECK(bt.started && k.State() == am::EKbdWaiting);
	k.Configure(true, am::EKbdViaUsb, am::EKbdUs);
	CHECK(!bt.started && usb.started && k.Via() == am::EKbdViaUsb);
	usb.ready = true;
	loop();
	keys(Chars("#"));
	CHECK(usb.Take() == "0:00 2:20 0:00");           // (US: Shift+3)
	keys(Ev(am::EKbdMode, 0, 0));
	usb.usable = false;
	hal.Advance(1100000);
	k.Loop();
	CHECK(k.State() == am::EKbdOff && strcmp(k.Why(), "no radio") == 0);
	keys(Ev(am::EKbdMode, 0, 1));
	CHECK(!usb.started && k.State() == am::EKbdOff && hal.Logged("not available (no radio)"));
	// forgetting is Bluetooth's
	bt.paired = true;
	CHECK(k.Paired() && k.Forget() && bt.forgets == 1 && !k.Paired());
	}

// a Rig whose Atom has a phone keyboard (a fake phone)
struct KbdRig : Rig
	{
	FakePhone bt;
	am::PhoneKbd kbd;
	KbdRig() : kbd(hal)
		{
		kbd.SetLinks(&bt, 0);
		kbd.Configure(true, am::EKbdViaBt, am::EKbdBritish);
		cast.SetKeyboard(&kbd);
		}
	void Run(uint32_t aMs) { for (uint32_t i = 0; i < aMs; i += 50) { hal.Advance(50000); kbd.Loop(); cast.Tick(); } }
	};

static std::vector<Bytes> Kbds(const std::vector<Msg>& aM)
	{
	std::vector<Bytes> o;
	for (const Msg& m : aM)
		if (m.type == am::ECastKbd)
			o.push_back(m.p);
	return o;
	}

// KEYS and KBD on link A
static void TestKbdLink()
	{
	KbdRig r;
	// a Psion that cannot send KEYS: no keyboard offered, its KEYS ignored
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(3));
	std::vector<Msg> m = r.Take();
	CHECK(!m.empty() && m[0].type == am::ECastWelcome && !(Le32(m[0].p.data() + 4) & 4) && Kbds(m).empty());
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	CHECK(!r.bt.started && r.cast.KbdState() == 0);
	r.cast.LinkClose(r.id);
	// one that can: WELCOME bit2, then KBD at once (ready, Bluetooth)
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(3 | 8));
	m = r.Take();
	CHECK(!m.empty() && m[0].type == am::ECastWelcome && (Le32(m[0].p.data() + 4) & 4));
	std::vector<Bytes> k = Kbds(m);
	CHECK(k.size() == 1 && k[0].size() == 4 && k[0][0] == am::EKbdReady && k[0][1] == am::EKbdViaBt && k[0][2] == 0 && k[0][3] == 0);
	// a keyboard-only Psion: paused, no UPDATEs; no KEYREQ nagging meanwhile
	Bytes st = Psion::Status(80, true);
	st[8 + 11] = 1;                              // (paused)
	{
	Bytes p(st.begin() + 8, st.end() - 4);
	st = Frame(am::ECastStatus, 0, p);
	}
	r.ToAtom(st);
	r.Run(4000);
	r.ToAtom(st);
	r.Run(4000);
	m = r.Take();
	CHECK(Count(m, am::ECastKeyReq) == 0 && Kbds(m).empty());   // (and no KBD: nothing changed)
	// MODE 1: waiting for the phone; the phone connects: typing, with its name
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	k = Kbds(r.Take());
	CHECK(r.bt.started && k.size() == 1 && k[0][0] == am::EKbdWaiting && r.cast.KbdState() == am::EKbdWaiting);
	r.bt.ready = true;
	r.Run(100);
	k = Kbds(r.Take());
	CHECK(k.size() == 1 && k[0][0] == am::EKbdTyping && std::string(k[0].begin() + 4, k[0].end()) == "Dan's iPhone");
	r.bt.Take();
	r.ToAtom(Frame(am::ECastKeys, 0, Chars("Hi") + Ev(am::EKbdKey, 0, 0x28) + Ev(am::EKbdHold, am::kKbdGui, 0)));
	CHECK(r.bt.Take() == "2:0b 0:00 0:0c 0:00 0:28 0:00 8:00");
	m = r.Take();
	CHECK(Count(m, am::ECastInput) == 0);         // (never passed back as INPUT)
	// link A closes: the phone let go, out of keyboard mode
	r.cast.LinkClose(r.id);
	CHECK(!r.bt.started && r.bt.Take() == "0:00" && !r.kbd.InMode());
	// made again: the Psion says MODE 1 again; Cmd is no longer held
	r.id = r.cast.LinkOpen(am::Cast::ERouteModem);
	r.ToAtom(Psion::Hello(3 | 8));
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	r.bt.ready = true;
	r.Run(100);
	r.bt.Take();
	r.ToAtom(Frame(am::ECastKeys, 0, Chars("a")));
	CHECK(r.bt.Take() == "0:04 0:00");
	// a bad KEYS (not whole events) is ignored, and the link stays up
	r.ToAtom(Frame(am::ECastKeys, 0, Bytes(7, 1)));
	CHECK(r.cast.LinkAlive(r.id) && r.bt.reports.empty() && r.kbd.BadMessages() == 1);
	// the keyboard turned off in the settings: KBD 0
	r.Take();
	r.kbd.Configure(false, am::EKbdViaBt, am::EKbdBritish);
	r.Run(100);
	k = Kbds(r.Take());
	CHECK(k.size() == 1 && k[0][0] == am::EKbdOff && k[0][1] == am::EKbdViaNone && !r.bt.started);
	r.kbd.Configure(true, am::EKbdViaBt, am::EKbdBritish);
	r.Run(100);
	CHECK(r.bt.started);
	// BYE ends it; so does 20 s of silence on the next link
	r.ToAtom(Frame(am::ECastBye, 0, Bytes()));
	CHECK(!r.bt.started);
	r.cast.LinkClose(r.id);
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(8));
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	CHECK(r.bt.started);
	r.Run(21000);
	CHECK(!r.cast.LinkAlive(r.id) && !r.bt.started);
	r.cast.LinkClose(r.id);
	// PsiCast turned off ends it too
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(8));
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	CHECK(r.bt.started);
	r.cast.SetEnabled(false);
	CHECK(!r.bt.started);
	// a service with no keyboard: KEYS ignored, no bit2
	Rig plain;
	plain.id = plain.cast.LinkOpen(am::Cast::ERouteTcp);
	plain.ToAtom(Psion::Hello(3 | 8));
	m = plain.Take();
	CHECK(!m.empty() && !(Le32(m[0].p.data() + 4) & 4) && Kbds(m).empty());
	plain.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	CHECK(plain.cast.LinkAlive(plain.id) && plain.cast.KbdState() == 0);
	}

// its settings, AT$STAT?, the web page, the LCD and the light
static void TestKbdHooks()
	{
	// AT$KBD and AT$KBDLAYOUT: Settings2's ext[1] and ext[2], under "Phone keyboard"
	const am::SchemaEntry* e = am::SchemaFind("KBD");
	const am::SchemaEntry* l = am::SchemaFind("KBDLAYOUT");
	CHECK(e && e->offset == 443 && e->type == am::STEnum && e->group == am::SGExt && am::ExtSchemaOwner(*e) == &am::TheKbdExt());
	CHECK(l && l->offset == 444 && strcmp(am::SchemaGroupNameOf(*l), "Phone keyboard") == 0);
	char line[256] = "";
	for (int i = 0; i < am::SchemaCount(); i++)
		if (&am::SchemaAt(i) == e)
			am::SchemaLine(i, line, sizeof(line));
	CHECK(strstr(line, "+SCHEMA:KBD,ENUM:BT|USB|OFF,0,2,") == line);
	am::Settings2 s2;
	am::FactoryDefaults2(s2);
	CHECK(s2.ext[1] == 0 && s2.ext[2] == 0);       // (BT, UK: an older record's zeros)

	FakeHal hal;
	FakePhone bt;
	am::KbdExt x;
	x.UseLinks(&bt, 0);
	CHECK(x.Begin(hal) && x.Keyboard());
	x.Apply(s2);
	am::PhoneKbd& k = *x.Keyboard();
	CHECK(k.On() && k.ViaSetting() == am::EKbdViaBt && k.Layout() == am::EKbdBritish);
	char t[100];
	x.StatLine(0, t, sizeof(t));
	CHECK(strcmp(t, "KBD=READY") == 0);
	x.StateText(t, sizeof(t));
	CHECK(strcmp(t, "ready (Bluetooth, British layout)") == 0);
	s2.ext[2] = 1;
	x.Apply(s2);
	CHECK(k.Layout() == am::EKbdUs);
	s2.ext[1] = 2;
	x.Apply(s2);
	x.StatLine(0, t, sizeof(t));
	CHECK(!k.On() && strcmp(t, "KBD=OFF") == 0);
	s2.ext[1] = 0;
	x.Apply(s2);
	// the page: no forget button until a phone is paired; its link's nonce
	TestQuery q;
	struct ForgetQuery : am::ExtQuery
		{
		std::string forget;
		const char* Arg(const char* aName) const override { return strcmp(aName, "forget") == 0 ? forget.c_str() : ""; }
		} fq;
	{ am::ExtReply r; x.Get("/keyboard", fq, r); CHECK(r.status == 200 && strstr((const char*)r.body, "<h1>Phone keyboard</h1>") && !strstr((const char*)r.body, "forget=")); }
	CHECK(!x.WebAction() && strcmp(x.WebHome(), "/keyboard") == 0);
	bt.paired = true;
	CHECK(x.WebAction() && strcmp(x.WebAction(), "forget the paired phone") == 0);
	std::string body;
	{ am::ExtReply r; x.Get("/keyboard", fq, r); body.assign((const char*)r.body, r.len); }
	size_t at = body.find("forget=");
	CHECK(at != std::string::npos);
	std::string nonce = at == std::string::npos ? "" : body.substr(at + 7, 16);
	fq.forget = "0123456789abcdef";
	{ am::ExtReply r; x.Get("/keyboard", fq, r); CHECK(strstr((const char*)r.body, "expired") && bt.forgets == 0); }
	fq.forget = nonce;                            // (spent: the page above made a new one)
	{ am::ExtReply r; x.Get("/keyboard", fq, r); CHECK(bt.forgets == 0); }
	fq.forget = "";
	{ am::ExtReply r; x.Get("/keyboard", fq, r); body.assign((const char*)r.body, r.len); }
	at = body.find("forget=");
	fq.forget = at == std::string::npos ? "" : body.substr(at + 7, 16);
	{ am::ExtReply r; x.Get("/keyboard", fq, r); CHECK(bt.forgets == 1 && strstr((const char*)r.body, "forgotten") && r.status == 200); }
	(void)q;

	// the LCD's page and the light: up while the Psion is in keyboard mode,
	// even with nobody watching
	KbdRig r;
	am::CastExt ext;
	ext.Use(r.cast, r.hal);
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(3 | 8));
	am::ExtLed led;
	CHECK(!ext.ScreenWanted() && !ext.LedWanted(led));
	uint32_t n0 = ext.ScreenNews();
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	CHECK(ext.ScreenWanted() && ext.LedWanted(led) && led.style == am::EExtLedBlink && led.periodMs == 1600 && ext.ScreenNews() != n0);
	am::CastLcdView v;
	ext.iLcd.View(r.cast, r.hal.Millis(), v);
	CHECK(v.kbd == am::EKbdWaiting && v.viewers == 0);
	r.bt.ready = true;
	r.Run(100);
	ext.iLcd.View(r.cast, r.hal.Millis(), v);
	CHECK(v.kbd == am::EKbdTyping && strcmp(v.kbdName, "Dan's iPhone") == 0);
	CHECK(ext.LedWanted(led) && led.style == am::EExtLedSteady);
	{
	am::Layout a;
	am::StatusModel::LayExt(ext, r.hal.Millis(), a);
	CHECK(a.dropped == 0 && a.n < am::kDrawOps);
	}
	// no viewer is let in meanwhile (keyboard mode and sharing are one or
	// the other): the light stays the keyboard's
	r.ToAtom(Psion::Status(80, true));
	char tk[32];
	CHECK(!r.cast.NewToken(true, tk, sizeof(tk)) && ext.LedWanted(led) && led.style == am::EExtLedSteady);
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 0)));
	CHECK(!ext.ScreenWanted() && !ext.LedWanted(led));
	}

// keyboard mode and screen sharing: one or the other (cast.h)
static void TestKbdShare()
	{
	KbdRig r;
	Psion ps;
	am::CastExt ext;
	ext.Use(r.cast, r.hal);
	TestQuery q;
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(3 | 8));
	ps.Box(0, 0, 640, 18, 0);
	r.ToAtom(ps.Update(true));
	int a = r.Viewer(), b = r.Viewer(true);
	CHECK(a >= 0 && b >= 0 && r.cast.Viewers() == 2 && !r.cast.Typing());
	ViewModel va, vb;
	r.Pump(a, va);
	r.Pump(b, vb);
	CHECK(va.fulls == 1 && vb.fulls == 1);
	r.Take();
	// MODE 1: every viewer told why and closed (4001), VIEWERS 0 (the
	// Psion's pictures stop), the keyboard started
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	CHECK(r.cast.Typing() && r.cast.Viewers() == 0 && r.bt.started && r.hal.Logged("no sharing meanwhile (2 viewers let go)"));
	r.Run(50);
	std::vector<Msg> m = r.Take();
	bool zero = false;
	for (const Msg& x : m)
		if (x.type == am::ECastViewers && x.p.size() == 2 && x.p[0] == 0)
			zero = true;
	CHECK(zero);
	r.Pump(a, va);
	r.Pump(b, vb);
	for (ViewModel* v : { &va, &vb })
		{
		CHECK(!v->texts.empty() && v->texts.back() == "{\"t\":\"bye\",\"why\":\"keyboard\",\"text\":\"the Psion is typing into a phone\"}");
		CHECK(v->closes.size() == 1 && v->closes[0].size() > 2 && v->closes[0][0] * 256 + v->closes[0][1] == am::Cast::kCloseTyping
			&& std::string(v->closes[0].begin() + 2, v->closes[0].end()) == "the Psion is typing into a phone");
		}
	CHECK(r.cast.ViewerDone(a) && r.cast.ViewerDone(b));
	// new ones turned away: no token (409, why "keyboard"), and the WebSocket
	// refused with kTyping (castnet: 409)
	{ am::ExtReply x; ext.Get("/screen/token", q, x); CHECK(x.status == 409 && strcmp(x.text, "{\"error\":\"The Psion is typing into a phone\",\"why\":\"keyboard\"}") == 0); }
	char t[32], why[64];
	CHECK(!r.cast.NewToken(false, t, sizeof(t)));
	CHECK(r.cast.ViewerOpen("0123456789abcdef01234567", why, sizeof(why)) == am::Cast::kTyping && strcmp(why, "the Psion is typing into a phone") == 0);
	char st[100];
	r.cast.StateText(st, sizeof(st));
	CHECK(strstr(st, "typing into a phone") != 0);
	// the screen copy stays while the viewers' sockets close, then goes
	CHECK(r.cast.Screen() != 0);
	r.cast.ViewerClose(a);
	r.cast.ViewerClose(b);
	r.Run(50);
	CHECK(r.cast.Screen() == 0);
	// an UPDATE that was on its way: taken (ACK), not applied; no KEYREQ while typing
	ps.Box(0, 100, 50, 10, 3);
	r.ToAtom(ps.Update(false));
	m = r.Take();
	CHECK(Count(m, am::ECastAck) == 1 && r.cast.LinkAlive(r.id));
	r.Run(7000);
	m = r.Take();
	CHECK(Count(m, am::ECastKeyReq) == 0 && r.cast.LinkAlive(r.id));
	// typing into the phone, as ever
	r.bt.ready = true;
	r.Run(100);
	r.bt.Take();
	r.ToAtom(Frame(am::ECastKeys, 0, Chars("a")));
	CHECK(r.bt.Take() == "0:04 0:00");
	// MODE 0: the screen back; viewers let in again, the first asking for a keyframe
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 0)));
	CHECK(!r.cast.Typing() && !r.bt.started);
	r.Run(50);
	m = r.Take();
	CHECK(r.cast.Screen() != 0 && r.hal.Logged("viewers may connect again"));
	{ am::ExtReply x; ext.Get("/screen/token", q, x); CHECK(x.status == 200); }
	int c = r.Viewer();
	m = r.Take();
	CHECK(c >= 0 && Count(m, am::ECastKeyReq) == 1);
	r.ToAtom(ps.Update(true));
	ViewModel vc;
	r.Pump(c, vc);
	CHECK(vc.fulls == 1 && vc.Same(r.cast.Screen()) && memcmp(r.cast.Screen(), ps.frame.data(), ps.frame.size()) == 0);
	// a viewer that takes nothing (its socket full): closed after kShutMaxMs anyway
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	CHECK(r.cast.Typing() && !r.cast.ViewerOverdue(c) && !r.cast.ViewerDone(c));
	r.Run(am::Cast::kShutMaxMs + 100);
	CHECK(r.cast.ViewerOverdue(c));
	r.cast.ViewerClose(c);
	// link A closing ends keyboard mode: viewers may come again
	r.cast.LinkClose(r.id);
	CHECK(!r.cast.Typing());
	{ am::ExtReply x; ext.Get("/screen/token", q, x); CHECK(x.status == 200); }
	// with the phone keyboard off in the settings, keyboard mode leaves the
	// viewers be (no Bluetooth to make room for); turned on, they go
	r.kbd.Configure(false, am::EKbdViaBt, am::EKbdBritish);
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(3 | 8));
	r.ToAtom(ps.Update(true));
	c = r.Viewer();
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	r.Run(100);
	CHECK(c >= 0 && !r.cast.Typing() && r.cast.Viewers() == 1);
	r.kbd.Configure(true, am::EKbdViaBt, am::EKbdBritish);
	r.Run(100);
	CHECK(r.cast.Typing() && r.cast.Viewers() == 0 && r.bt.started);
	}

// KBD's reason (byte 2) and the keyboard short of memory while viewers go
static void TestKbdWhy()
	{
	KbdRig r;
	r.bt.usable = false;
	r.bt.why = am::EKbdWhyMemory;
	r.Run(1100);                                 // (the once-a-second look)
	r.id = r.cast.LinkOpen(am::Cast::ERouteTcp);
	r.ToAtom(Psion::Hello(3 | 8));
	// short of memory while sharing: offered all the same (keyboard mode frees it)
	std::vector<Bytes> k = Kbds(r.Take());
	CHECK(k.size() == 1 && k[0][0] == am::EKbdReady && k[0][2] == 0);
	// MODE 1: said to be starting, tried again every second, started once there is room
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	CHECK(!r.bt.started && r.cast.KbdState() == am::EKbdReady);
	r.Run(2000);
	CHECK(!r.bt.started && r.cast.KbdState() == am::EKbdReady && Kbds(r.Take()).empty());
	r.bt.usable = true;
	r.Run(1100);
	k = Kbds(r.Take());
	CHECK(r.bt.started && r.cast.KbdState() == am::EKbdWaiting && k.size() == 1 && k[0][0] == am::EKbdWaiting && k[0][2] == 0);
	// short for longer than kStartMs: KBD 0 with why 1 (memory), said once in the log
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 0)));
	r.bt.usable = false;
	r.Run(1100);
	r.Take();
	size_t logs = r.hal.logs.size();
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 1)));
	r.Run(am::PhoneKbd::kStartMs + 1100);
	k = Kbds(r.Take());
	CHECK(!k.empty() && k.back()[0] == am::EKbdOff && k.back()[1] == am::EKbdViaNone && k.back()[2] == am::EKbdWhyMemory);
	CHECK(r.kbd.WhyCode() == am::EKbdWhyMemory && !r.bt.started);
	int said = 0;
	for (size_t i = logs; i < r.hal.logs.size(); i++)
		said += r.hal.logs[i].find("not available") != std::string::npos;
	CHECK(said == 1);
	// another reason: not offered, and said at once (why 2: it did not start)
	r.ToAtom(Frame(am::ECastKeys, 0, Ev(am::EKbdMode, 0, 0)));
	r.bt.why = am::EKbdWhyFailed;
	r.Run(1100);
	k = Kbds(r.Take());
	CHECK(!k.empty() && k.back()[0] == am::EKbdOff && k.back()[2] == am::EKbdWhyFailed);
	// off in the settings: why 0, as an older Atom says
	r.kbd.Configure(false, am::EKbdViaBt, am::EKbdBritish);
	r.Run(100);
	k = Kbds(r.Take());
	CHECK(!k.empty() && k.back()[0] == am::EKbdOff && k.back()[2] == 0 && r.kbd.WhyCode() == 0);
	}

int main()
	{
	TestFraming();
	TestSession();
	TestTokens();
	TestWebSocket();
	TestViewers();
	TestInput();
	TestLatency();
	TestModemRoute();
	TestHooks();
	TestLcd();
	TestLcdDelay();
	TestKbdMap();
	TestKbdKeys();
	TestKbdLink();
	TestKbdHooks();
	TestKbdShare();
	TestKbdWhy();
	printf("test_cast: %d checks, %d failed\n", gChecks, gFails);
	return gFails ? 1 : 0;
	}
