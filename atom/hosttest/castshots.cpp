// castshots.cpp - the Sharing page (castlcd.h) for the firmware's host
// renderer (hosttest/render_screens, shots.h), with its picture of the
// System screen (casthome.h): one viewer with control and the delay known,
// two without, paused, waiting for the Psion, PsiCast off, and the phone
// keyboard (waiting for the phone, typing, keyboard only).
//   make -C cast/atom/hosttest screens   (into screens/, or OUT=folder)
// MIT licence (see LICENSE at the top of the repository).
#include "shots.h"
#include "castlcd.h"
#include "casthome.h"
#include "kbd.h"
#include <stdio.h>
#include <string.h>

namespace {

ShotSave gSave;
void Shot(const char* aName, const am::CastLcdView& aView)
	{
	struct Page : am::Extension
		{
		const am::CastLcdView* v;
		const char* Name() const override { return "Shot"; }
		void ScreenDraw(am::ExtPen& aPen, uint32_t) override { am::CastLcdDraw(aPen, *v); }
		} page;
	page.v = &aView;
	am::Layout l;
	am::StatusModel::LayExt(page, 0, l);
	if (l.dropped || l.n >= am::kDrawOps)
		fprintf(stderr, "%s: %d steps did not fit\n", aName, l.dropped ? l.dropped : 1);
	gSave(aName, l);
	}

void Run(ShotSave aSave)
	{
	gSave = aSave;
	static uint8_t pic[am::kCastPicW * am::kCastPicH];
	for (int i = 0; i < am::kCastPicW * am::kCastPicH; i++)
		pic[i] = (uint8_t)(((kCastHome[i / 2] >> ((i & 1) * 4)) & 15) * 17);
	am::CastLcdView v = {};
	v.enabled = true;
	v.live = true;
	v.viewers = 1;
	v.control = true;
	v.haveDelay = true;
	v.delayMs = 120;
	v.kbdVia = am::EKbdViaBt;
	v.pic = pic;
	snprintf(v.address, sizeof(v.address), "192.168.1.50/screen");
	Shot("30-share-1-control", v);
	v.viewers = 2; v.control = false; v.delayMs = 85;
	Shot("31-share-2-watching", v);
	v.haveDelay = false;
	Shot("32-share-2-nodelay", v);
	v.paused = true; v.viewers = 1;
	Shot("33-share-paused", v);
	am::CastLcdView w = v;
	w.live = false; w.paused = false; w.viewers = 0;
	Shot("34-share-waiting", w);
	w.enabled = false;
	Shot("34-share-off", w);
	// the phone keyboard: waiting for the phone with one watching; typing
	// with nobody watching; and a Psion that is not sharing (paused)
	am::CastLcdView k = v;
	k.paused = false;
	k.viewers = 1;
	k.kbd = am::EKbdWaiting;
	Shot("35-share-kbd-waiting", k);
	k.viewers = 0;
	k.kbd = am::EKbdTyping;
	snprintf(k.kbdName, sizeof(k.kbdName), "Dan's iPhone");
	Shot("36-share-kbd-typing", k);
	k.paused = true;
	k.kbdName[0] = 0;
	Shot("37-share-kbd-only", k);
	}

ShotRegister gShots(&Run);

} // namespace
