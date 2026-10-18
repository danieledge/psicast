// PSICAST.CPP - PsiCast.app: share the Psion's screen with a web browser.
//
// One view (CCsView), PsiCast's page: three big choices - Share screen,
// Type on phone, Allow control - each saying how it is now, and a status
// strip with where to look on the phone and how fast it goes. They are
// another way in to the Share menu's commands and the toolbar's buttons.
// In the keyboard mode (Share > Type on phone) the view says how the typing
// on the phone goes instead, with a big pen target to stop. Reading,
// encoding and sending the screen is psicast.exe's work, through the chunk
// in ../engine/psicast.h. See csapp.h.

#include <e32keys.h>
#include <e32hal.h>
#include <eiklabel.h>
#include <eikchlst.h>
#include <eikmfne.h>
#include <eikcmbut.h>
#include <eiktbar.h>
#include <eikimage.h>
#include <eikon.rsg>
#include "csapp.h"
#include "csicons.h"
#include "psilink.h"
#include "pglinktest.h"

_LIT(KVersion, "0.14");            // also cast/pkg/psicast.pkg
_LIT(KEngineExe, "psicast.exe");
_LIT(KIniFile, "C:\\System\\Apps\\PsiCast\\PsiCast.ini");
_LIT(KAtomHost, "192.168.7.1");    // the Atom's own address (PROTOCOL.md link A)
const TInt KAtomPort = 5961;
const TInt KRates[4] = { 2, 5, 10, 20 };
const TInt KTickFast = 250000;     // while connecting: the state and the busy message
const TInt KTickIdle = 1000000;
const TUint KPauseKeyCode = 'w' - 'a' + 1;     // Shift+Ctrl+W: the key code Ctrl makes of W
const TInt KKbdLeaveScan = 'B';                 // Shift+Ctrl+B: leaves the keyboard mode (never sent)
_LIT(KNoPhoneKbd, "Not available - the Atom has no phone keyboard");
const TInt KBigTwips[3] = { 200, 250, 300 };
const TInt KTextTwips[3] = { 150, 200, 250 };
const TInt KSmallTwips[3] = { 120, 150, 200 };
const TInt KNameTwips[3] = { 200, 250, 250 };    // the page's choices (at most 250: "Allow control" fits beside the toolbar)

static void CopyToC(char* aDst, TInt aMax, const TDesC& aSrc)
	{
	TInt n = aSrc.Length() < aMax - 1 ? aSrc.Length() : aMax - 1;
	for (TInt i = 0; i < n; i++)
		aDst[i] = (char)aSrc[i];
	aDst[n] = 0;
	}

// ===========================================================================
// The view
// ===========================================================================

CCsView::~CCsView()
	{
	CWsScreenDevice* dev = iCoeEnv->ScreenDevice();
	if (iBig) dev->ReleaseFont(iBig);
	if (iLabel) dev->ReleaseFont(iLabel);
	if (iText) dev->ReleaseFont(iText);
	if (iSmall) dev->ReleaseFont(iSmall);
	if (iSmallBold) dev->ReleaseFont(iSmallBold);
	if (iName) dev->ReleaseFont(iName);
	for (TInt i = 0; i < ECsTileCount; i++)
		{
		delete iPic[i];
		delete iMask[i];
		}
	}

void CCsView::ConstructL(const TRect& aRect)
	{
	CreateWindowL();
	EnableDragEvents();                      // (the pen leaving a choice lets it go)
	Window().SetPointerGrab(ETrue);          // (...and its up comes here)
	SetRectL(aRect);
	ApplyZoomL();
	TRAPD(pics, LoadPicturesL());
	(void)pics;                              // (no PsiCast.mbm: words only)
	ActivateL();
	}

// the choices' pictures, from PsiCast.mbm (cast/pkg/mktbar.py)
void CCsView::LoadPicturesL()
	{
	const TInt ids[ECsTileCount] = { EMbmPageShare, EMbmPageType, EMbmPageControl };
	TFileName mbm = iEikonEnv->EikAppUi()->Application()->BitmapStoreName();
	for (TInt i = 0; i < ECsTileCount; i++)
		{
		iPic[i] = iEikonEnv->CreateBitmapL(mbm, ids[i]);
		iMask[i] = iEikonEnv->CreateBitmapL(mbm, ids[i] + 1);
		}
	}

CFont* CCsView::FontL(TInt aTwips, TBool aBold, CFont*& aOld)
	{
	TFontSpec spec(_L("Arial"), aTwips);
	if (aBold)
		spec.iFontStyle.SetStrokeWeight(EStrokeWeightBold);
	CFont* font = NULL;
	CWsScreenDevice* dev = iCoeEnv->ScreenDevice();
	User::LeaveIfError(dev->GetNearestFontInTwips(font, spec));
	if (aOld)
		dev->ReleaseFont(aOld);
	aOld = font;
	return font;
	}

void CCsView::ApplyZoomL()
	{
	TInt z = iUi.Settings().iZoom;
	FontL(KBigTwips[z], ETrue, iBig);
	FontL(KTextTwips[z], ETrue, iLabel);
	FontL(KTextTwips[z], EFalse, iText);
	FontL(KSmallTwips[z], EFalse, iSmall);
	FontL(KSmallTwips[z], ETrue, iSmallBold);
	FontL(KNameTwips[z], ETrue, iName);
	}

void CCsView::SetViewRectL(const TRect& aRect)
	{
	SetRectL(aRect);
	DrawNow();
	}

static void DrawClipped(CWindowGc& aGc, const CFont* aFont, const TDesC& aText, TPoint aAt, TInt aWidth)
	{
	if (aWidth <= 0)
		return;
	if (aFont->TextWidthInPixels(aText) <= aWidth)
		{
		aGc.DrawText(aText, aAt);
		return;
		}
	_LIT(KDots, "...");
	TInt n = aFont->TextCount(aText, aWidth - aFont->TextWidthInPixels(KDots));
	TBuf<130> t(aText.Left(n < 120 ? n : 120));
	t.Append(KDots);
	aGc.DrawText(t, aAt);
	}

// a labelled line: the label in bold, what it says beside it
// (aRight: where the text must stop, if short of the right-hand edge)
void CCsView::DrawLine(CWindowGc& aGc, TInt aY, const TDesC& aLabel, const TDesC& aText, TInt aRight) const
	{
	TRect r = Rect();
	TInt labelW = iLabel->TextWidthInPixels(_L("On the phone")) + 16;
	TInt right = aRight > 0 && aRight < r.iBr.iX - 10 ? aRight : r.iBr.iX - 10;
	aGc.UseFont(iLabel);
	aGc.SetPenColor(KRgbBlack);
	DrawClipped(aGc, iLabel, aLabel, TPoint(r.iTl.iX + 14, aY), labelW - 4);
	aGc.UseFont(iText);
	DrawClipped(aGc, iText, aText, TPoint(r.iTl.iX + 14 + labelW, aY), right - (r.iTl.iX + 14 + labelW));
	}

// the keyboard mode's pen target, bottom right: big enough for a finger
TRect CCsView::StopRect() const
	{
	TRect r = Rect();
	TInt w = iBig->TextWidthInPixels(_L("Stop typing")) + 36, h = iBig->HeightInPixels() + 26;
	return TRect(r.iBr.iX - 12 - w, r.iBr.iY - 10 - h, r.iBr.iX - 12, r.iBr.iY - 10);
	}

// The page: one tap on a choice acts, as a button does - pressed in while
// the pen is down on it, acting when the pen comes up on it (taken off it
// first, it doesn't). The keyboard mode: Stop typing acts at once.
void CCsView::HandlePointerEventL(const TPointerEvent& aEvent)
	{
	if (iUi.KbdMode())
		{
		if (aEvent.iType == TPointerEvent::EButton1Down && StopRect().Contains(aEvent.iPosition))
			iUi.HandleCommandL(ECsCmdKeyboard);     // (the same as Shift+Ctrl+B: leaves)
		iPressed = iDown = -1;
		return;
		}
	TInt at = TileAt(aEvent.iPosition);
	switch (aEvent.iType)
		{
	case TPointerEvent::EButton1Down:
		iDown = at;
		if (at >= 0)
			{
			InvalidateTile(iFocus);
			iFocus = at;
			iPressed = at;
			InvalidateTile(at);
			}
		break;
	case TPointerEvent::EDrag:
		if (iDown >= 0)
			{
			TInt p = at == iDown ? iDown : -1;
			if (p != iPressed)
				{
				iPressed = p;
				InvalidateTile(iDown);
				}
			}
		break;
	case TPointerEvent::EButton1Up:
		if (iDown >= 0)
			{
			TInt tile = iDown;
			TBool act = iPressed == tile && at == tile;
			iPressed = iDown = -1;
			InvalidateTile(tile);
			if (act)
				ActL(tile);
			}
		break;
	default:
		break;
		}
	}

// the arrow keys move between the choices (Fn+arrows: the first, the
// last), Enter acts; everything else is EIKON's (the shortcuts)
TKeyResponse CCsView::OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType)
	{
	if (aType != EEventKey || iUi.KbdMode() || (aKeyEvent.iModifiers & EModifierCtrl))
		return EKeyWasNotConsumed;
	switch (aKeyEvent.iCode)
		{
	case EKeyLeftArrow:
		SetFocusTile(iFocus - 1);
		break;
	case EKeyRightArrow:
		SetFocusTile(iFocus + 1);
		break;
	case EKeyHome:
		SetFocusTile(0);
		break;
	case EKeyEnd:
		SetFocusTile(ECsTileCount - 1);
		break;
	case EKeyEnter:
		ActL(iFocus);
		break;
	default:
		return EKeyWasNotConsumed;
		}
	return EKeyWasConsumed;
	}

// (at the ends it stays: the choices don't wrap round)
void CCsView::SetFocusTile(TInt aTile)
	{
	if (aTile < 0 || aTile >= ECsTileCount || aTile == iFocus)
		return;
	InvalidateTile(iFocus);
	iFocus = aTile;
	InvalidateTile(aTile);
	}

// the same commands as the toolbar's and the Share menu's
void CCsView::ActL(TInt aTile)
	{
	const TInt cmd[ECsTileCount] = { ECsCmdShareButton, ECsCmdKeyboard, ECsCmdControl };
	if (aTile >= 0 && aTile < ECsTileCount)
		iUi.HandleCommandL(cmd[aTile]);
	}

// ----- the page ----------------------------------------------------------------
// Three choices side by side, the full height but the status strip's, which
// takes two lines at the bottom. Sizes follow the zoom (the fonts); the
// pictures stay 64x48.

const TInt KPageMargin = 8;        // round the page
const TInt KTileGap = 8;           // between the choices
const TInt KTileShadow = 2;        // a raised choice's shadow, right and bottom
const TInt KPicW = 64, KPicH = 48;
const TInt KTileLines = 4;         // a choice's state takes at most this many lines

// the width a choice's words may take (that of a choice the arrow keys are
// on, its edge thicker, so a choice's words don't move as it gets the focus)
TInt CCsView::TileTextWidth() const
	{
	TRect t = TileRect(0);
	return t.Width() - KTileShadow - 2 * (3 + 6);
	}

TInt CCsView::StripTop() const
	{
	TInt pitch = iSmall->HeightInPixels() + 3;
	return Rect().iBr.iY - 4 - 2 * pitch - 4;
	}

TRect CCsView::TileRect(TInt aTile) const
	{
	TRect r = Rect();
	TInt w = (r.Width() - 2 * KPageMargin - (ECsTileCount - 1) * KTileGap) / ECsTileCount;
	TInt x = r.iTl.iX + KPageMargin + aTile * (w + KTileGap);
	return TRect(x, r.iTl.iY + KPageMargin, x + w, StripTop() - 6);
	}

TInt CCsView::TileAt(const TPoint& aPoint) const
	{
	for (TInt i = 0; i < ECsTileCount; i++)
		if (TileRect(i).Contains(aPoint))
			return i;
	return -1;
	}

// a choice's state in at most aMax lines of aWidth, as a person would
// break it: one line if it fits; else after its " - " ("Sharing -",
// "2 watching"); else two lines as even as they can be ("Viewers can",
// "tap & type"); else as many words as fit on each line. The last line
// takes the rest (clipped with "..." when drawn).
static TInt WrapText(const CFont* aFont, const TDesC& aText, TInt aWidth, TPtrC* aLines, TInt aMax)
	{
	if (aMax <= 0)
		return 0;
	TInt len = aText.Length();
	if (aMax == 1 || aFont->TextWidthInPixels(aText) <= aWidth)
		{
		aLines[0].Set(aText);
		return 1;
		}
	_LIT(KDash, " - ");
	TInt dash = aText.Find(KDash);
	if (dash > 0 && aFont->TextWidthInPixels(aText.Left(dash + 2)) <= aWidth)
		{
		aLines[0].Set(aText.Left(dash + 2));
		return 1 + WrapText(aFont, aText.Mid(dash + 3), aWidth, aLines + 1, aMax - 1);
		}
	TInt best = -1, bestW = KMaxTInt;
	for (TInt i = 1; i < len - 1; i++)
		{
		if (aText[i] != ' ')
			continue;
		TInt a = aFont->TextWidthInPixels(aText.Left(i)), b = aFont->TextWidthInPixels(aText.Mid(i + 1));
		TInt wider = a > b ? a : b;
		if (wider <= aWidth && wider < bestW)
			{
			best = i;
			bestW = wider;
			}
		}
	if (best > 0)
		{
		aLines[0].Set(aText.Left(best));
		aLines[1].Set(aText.Mid(best + 1));
		return 2;
		}
	// the most words that fit (a word too long alone: all of it)
	TInt end = -1;
	for (TInt i = 1; i < len; i++)
		if (aText[i] == ' ' && aFont->TextWidthInPixels(aText.Left(i)) <= aWidth)
			end = i;
	if (end < 0)
		{
		end = aText.Locate(' ');
		if (end < 0)
			end = len;
		}
	aLines[0].Set(aText.Left(end));
	if (end >= len)
		return 1;
	return 1 + WrapText(aFont, aText.Mid(end + 1), aWidth, aLines + 1, aMax - 1);
	}

// One choice: a picture, its name in bold, how it is now under it. At rest
// it is raised (white, a black edge, a dark grey shadow); while on - or
// while the pen is down on it - it is pressed in (pale grey, as a latched
// button). The one the arrow keys are on has a thick black edge. Not
// available: its words are grey (a tap still says why).
void CCsView::DrawTile(CWindowGc& aGc, TInt aTile, TInt aLines) const
	{
	_LIT(KShare, "Share screen");
	_LIT(KType, "Type on phone");
	_LIT(KControl, "Allow control");
	const TDesC* names[ECsTileCount] = { &KShare, &KType, &KControl };
	TBuf<80> state;
	TBool on = EFalse, dimmed = EFalse;
	iUi.TileState(aTile, state, on, dimmed);
	TBool in = on || iPressed == aTile;
	TRect t = TileRect(aTile);
	TRect face(t.iTl, TPoint(t.iBr.iX - KTileShadow, t.iBr.iY - KTileShadow));
	if (in)
		face.Move(1, 1);                     // (pressed in: no shadow, down a pixel)
	else
		{
		aGc.SetPenStyle(CGraphicsContext::ENullPen);
		aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
		aGc.SetBrushColor(KRgbDarkGray);
		aGc.DrawRect(TRect(face.iBr.iX, t.iTl.iY + KTileShadow, t.iBr.iX, t.iBr.iY));
		aGc.DrawRect(TRect(t.iTl.iX + KTileShadow, face.iBr.iY, face.iBr.iX, t.iBr.iY));
		}
	aGc.SetPenStyle(CGraphicsContext::ESolidPen);
	aGc.SetPenColor(KRgbBlack);
	aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
	aGc.SetBrushColor(in ? KRgbGray : KRgbWhite);
	aGc.DrawRect(face);
	aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
	TInt edge = 1;
	if (iFocus == aTile)
		{
		TRect f(face);
		for (; edge < 3; edge++)
			{
			f.Shrink(1, 1);
			aGc.DrawRect(f);
			}
		}
	if (in)
		{
		// the pressed-in shade along the top and the left
		aGc.SetPenColor(KRgbDarkGray);
		aGc.DrawLine(TPoint(face.iTl.iX + edge, face.iTl.iY + edge), TPoint(face.iBr.iX - edge, face.iTl.iY + edge));
		aGc.DrawLine(TPoint(face.iTl.iX + edge, face.iTl.iY + edge), TPoint(face.iTl.iX + edge, face.iBr.iY - edge));
		}

	// what goes in it, as a block in the middle, the same height in every
	// choice (so the names line up across the page)
	TInt textW = TileTextWidth();
	TPtrC lines[KTileLines];
	TInt n = WrapText(iText, state, textW, lines, KTileLines);
	TBool pic = iPic[aTile] && iMask[aTile];
	TInt picH = pic ? KPicH + 8 : 0;
	TInt titleH = iName->HeightInPixels();
	TInt lineH = iText->HeightInPixels() + 1;
	TInt block = picH + titleH + 4 + aLines * lineH;
	TInt y = face.iTl.iY + (face.Height() - block) / 2;
	if (y < face.iTl.iY + edge + 4)
		y = face.iTl.iY + edge + 4;
	TInt cx = (face.iTl.iX + face.iBr.iX) / 2;
	if (pic)
		{
		aGc.BitBltMasked(TPoint(cx - KPicW / 2, y), iPic[aTile], TRect(TSize(KPicW, KPicH)), iMask[aTile], ETrue);
		y += picH;
		}
	TRgb ink = dimmed ? KRgbDarkGray : KRgbBlack;
	aGc.SetPenColor(ink);
	aGc.UseFont(iName);
	const TDesC& name = *names[aTile];
	TInt w = iName->TextWidthInPixels(name);
	DrawClipped(aGc, iName, name, TPoint(w < textW ? cx - w / 2 : cx - textW / 2, y + iName->AscentInPixels()), textW);
	y += titleH + 4;
	aGc.UseFont(iText);
	for (TInt i = 0; i < n; i++)
		{
		w = iText->TextWidthInPixels(lines[i]);
		DrawClipped(aGc, iText, lines[i], TPoint(w < textW ? cx - w / 2 : cx - textW / 2, y + iText->AscentInPixels()), textW);
		y += lineH;
		}
	}

// What the status strip says: where to look on the phone; how fast it goes
// (while sharing) or, not connected, why; the keys sent to the phone (while
// connected)
void CCsView::StripTexts(TDes& aPhone, TDes& aSending, TDes& aKeys, TDes& aWhy) const
	{
	const CsShared* s = iUi.Shared();
	aSending.Zero();
	aKeys.Zero();
	aWhy.Zero();
	TBuf8<64> url;
	if (s)
		url = FromC(s->viewer_url, sizeof(s->viewer_url) - 1);
	if (url.Length())
		{
		aPhone = _L("Open ");
		TBuf<64> u;
		u.Copy(url);
		aPhone.Append(u);
		}
	else
		aPhone = _L("Open the Atom's address, then /screen");
	if (!s)
		return;
	TUint keys = iUi.KeysSent();
	if (keys)
		aKeys.Format(keys == 1 ? _L("1 key sent") : _L("%u keys sent"), keys);
	if (s->phase == CS_PHASE_SHARING && iUi.Sharing())
		{
		TUint b = s->bytes_sec, u = s->ups_x10;
		if (b >= 1024)
			aSending.Format(_L("%u.%u KB a second"), b / 1024, (b % 1024) * 10 / 1024);
		else
			aSending.Format(_L("%u bytes a second"), b);
		aSending.AppendFormat(_L(", %u.%u updates a second"), u / 10, u % 10);
		}
	else if (s->phase != CS_PHASE_SHARING && s->msg[0])
		{
		TBuf8<96> m = FromC(s->msg, sizeof(s->msg) - 1);
		aWhy.Copy(m);
		}
	}

TRect CCsView::StripRect() const
	{
	TRect r = Rect();
	r.iTl.iY = StripTop();
	return r;
	}

// The status strip: two lines under the choices, the labels in bold, the
// keys sent on the right, why not connected in grey (secondary)
void CCsView::DrawStrip(CWindowGc& aGc) const
	{
	TRect r = Rect();
	TInt x0 = r.iTl.iX + KPageMargin, x1 = r.iBr.iX - KPageMargin;
	TInt y = StripTop();
	aGc.SetPenColor(KRgbDarkGray);
	aGc.DrawLine(TPoint(x0, y), TPoint(x1, y));
	TInt pitch = iSmall->HeightInPixels() + 3;
	TInt base = y + 4 + iSmall->AscentInPixels();
	_LIT(KOnPhone, "On the phone");
	_LIT(KSending, "Sending");
	TInt labelW = iSmallBold->TextWidthInPixels(KOnPhone) + 12;
	TBuf<100> phone, sending, why;
	TBuf<32> keys;
	StripTexts(phone, sending, keys, why);
	aGc.SetPenColor(KRgbBlack);
	aGc.UseFont(iSmallBold);
	aGc.DrawText(KOnPhone, TPoint(x0, base));
	aGc.UseFont(iSmall);
	DrawClipped(aGc, iSmall, phone, TPoint(x0 + labelW, base), x1 - x0 - labelW);
	base += pitch;
	TInt right = x1;
	if (keys.Length())
		{
		TInt kw = iSmall->TextWidthInPixels(keys);
		aGc.DrawText(keys, TPoint(x1 - kw, base));
		right = x1 - kw - 16;
		}
	if (sending.Length())
		{
		aGc.UseFont(iSmallBold);
		aGc.DrawText(KSending, TPoint(x0, base));
		aGc.UseFont(iSmall);
		DrawClipped(aGc, iSmall, sending, TPoint(x0 + labelW, base), right - x0 - labelW);
		}
	else if (why.Length())
		{
		aGc.SetPenColor(KRgbDarkGray);
		DrawClipped(aGc, iSmall, why, TPoint(x0, base), right - x0);
		}
	}

// the lines the tallest of the choices' states takes (2 at least): every
// choice keeps that room, so their names line up
TInt CCsView::StateLines() const
	{
	TInt lines = 2;
	TInt w = TileTextWidth();
	for (TInt i = 0; i < ECsTileCount; i++)
		{
		TBuf<80> state;
		TBool on, dimmed;
		iUi.TileState(i, state, on, dimmed);
		TPtrC l[KTileLines];
		TInt n = WrapText(iText, state, w, l, KTileLines);
		if (n > lines)
			lines = n;
		}
	return lines;
	}

// what a part of the page shows now (a choice, or the strip), to see if it
// has changed
void CCsView::PartText(TInt aPart, TDes& aText) const
	{
	aText.Zero();
	if (aPart < ECsTileCount)
		{
		TBool on, dimmed;
		iUi.TileState(aPart, aText, on, dimmed);
		aText.Append(on ? '+' : '-');
		aText.Append(dimmed ? '+' : '-');
		return;
		}
	TBuf<100> phone, sending, why;
	TBuf<32> keys;
	StripTexts(phone, sending, keys, why);
	aText.Append(phone);
	aText.Append('|');
	aText.Append(sending);
	aText.Append('|');
	aText.Append(keys);
	aText.Append('|');
	aText.Append(why);
	}

// The engine said something new: only the parts of the page that now say
// something else are drawn again (a page drawn whole every second would
// flicker, and every change on the screen is sent to the viewers)
void CCsView::Update()
	{
	if (iUi.KbdMode())
		{
		DrawNow();
		return;
		}
	TInt lines = StateLines();
	TBool all = lines != iShownLines;
	iShownLines = lines;
	TBuf<KCsPartText> t;
	for (TInt i = 0; i <= ECsTileCount; i++)
		{
		PartText(i, t);
		if (t == iShown[i])
			continue;
		iShown[i] = t;
		if (!all)
			Window().Invalidate(i < ECsTileCount ? TileRect(i) : StripRect());
		}
	if (all)
		DrawNow();
	}

void CCsView::InvalidateTile(TInt aTile)
	{
	if (aTile >= 0 && aTile < ECsTileCount)
		Window().Invalidate(TileRect(aTile));
	}

// Keyboard mode: who is typed on, how the phone's end is, the keys that
// differ, how to stop, and how many keys have gone
void CCsView::DrawKeyboard(CWindowGc& aGc) const
	{
	TRect r = Rect();
	const CsShared* s = iUi.Shared();
	TRect stop = StopRect();
	TBuf<120> t;
	TInt y = r.iTl.iY + 10 + iBig->AscentInPixels();
	TBuf<40> phone;
	if (s && s->phone[0])
		phone.Copy(FromC(s->phone, sizeof(s->phone) - 1));
	t = _L("Typing on ");
	if (phone.Length())
		t.Append(phone);
	else
		t.Append(_L("your phone"));
	aGc.UseFont(iBig);
	aGc.SetPenColor(KRgbBlack);
	DrawClipped(aGc, iBig, t, TPoint(r.iTl.iX + 14, y), r.Width() - 28);
	y += iBig->DescentInPixels() + 4;
	aGc.SetPenColor(KRgbDarkGray);
	aGc.DrawLine(TPoint(r.iTl.iX + 14, y), TPoint(r.iBr.iX - 14, y));
	TInt pitch = iText->HeightInPixels() + 7;
	y += 6 + iText->AscentInPixels();

	// the phone's end (KBD)
	TBool waiting = EFalse;
	if (!s || s->phase == CS_PHASE_STARTING || s->phase == CS_PHASE_CONNECTING
		|| (s->phase == CS_PHASE_SHARING && s->atom_kbd < 0))
		t = _L("Connecting to the Atom...");
	else if (s->phase != CS_PHASE_SHARING)
		t = _L("Waiting for the Atom...");
	else if (s->kbd_state == 0)
		t = _L("Off in the Atom's settings");
	else if (s->kbd_state == 2)
		{
		t = _L("Waiting for the phone");
		waiting = ETrue;
		}
	else if (s->kbd_state == 3)
		{
		t = _L("Connected");
		if (s->kbd_transport == 1)
			t.Append(_L(" by Bluetooth"));
		else if (s->kbd_transport == 2)
			t.Append(_L(" by USB"));
		}
	else
		t = _L("Starting the keyboard...");
	DrawLine(aGc, y, _L("Phone"), t);
	y += pitch;
	if (waiting)
		{
		DrawLine(aGc, y, _L("First time"), _L("On the phone, pair 'PsiConnect keyboard'"));
		y += pitch;
		DrawLine(aGc, y, KNullDesC, _L("in Settings > Bluetooth"));
		y += pitch;
		}
	DrawLine(aGc, y, _L("Keys"), _L("Menu is Cmd, Ctrl is Control"));
	y += pitch;
	DrawLine(aGc, y, KNullDesC, _L("Fn+arrows are Home, End, Page up & down"));
	y += pitch;
	DrawLine(aGc, y, _L("To stop"), _L("Press Shift+Ctrl+B, or tap Stop typing"),
		y + iText->DescentInPixels() > stop.iTl.iY ? stop.iTl.iX - 10 : 0);
	y += pitch;
	TUint n = iUi.KeysSent();
	if (n == 0)
		t = _L("No keys yet");
	else
		t.Format(n == 1 ? _L("1 key") : _L("%u keys"), n);
	DrawLine(aGc, y, _L("Sent"), t, y + iText->DescentInPixels() > stop.iTl.iY ? stop.iTl.iX - 10 : 0);

	// why not connected, in grey at the bottom (secondary)
	if (s && s->phase != CS_PHASE_SHARING && s->msg[0])
		{
		TBuf8<96> m = FromC(s->msg, sizeof(s->msg) - 1);
		t.Copy(m);
		aGc.UseFont(iSmall);
		aGc.SetPenColor(KRgbDarkGray);
		DrawClipped(aGc, iSmall, t, TPoint(r.iTl.iX + 14, r.iBr.iY - 6 - iSmall->DescentInPixels()), stop.iTl.iX - 24 - r.iTl.iX);
		}

	// Stop typing: a button-like target (pale grey, black text, as EIKON's)
	aGc.SetPenColor(KRgbBlack);
	aGc.SetBrushStyle(CGraphicsContext::ESolidBrush);
	aGc.SetBrushColor(KRgbGray);
	aGc.DrawRect(stop);
	aGc.SetBrushColor(TRgb(0xdddddd));
	TRect in(stop);
	in.Shrink(2, 2);
	aGc.SetPenStyle(CGraphicsContext::ENullPen);
	aGc.DrawRect(in);
	aGc.SetPenStyle(CGraphicsContext::ESolidPen);
	aGc.SetPenColor(KRgbWhite);
	aGc.DrawLine(TPoint(stop.iTl.iX + 1, stop.iBr.iY - 2), TPoint(stop.iTl.iX + 1, stop.iTl.iY + 1));
	aGc.DrawLine(TPoint(stop.iTl.iX + 1, stop.iTl.iY + 1), TPoint(stop.iBr.iX - 1, stop.iTl.iY + 1));
	aGc.SetBrushStyle(CGraphicsContext::ENullBrush);
	aGc.UseFont(iBig);
	aGc.SetPenColor(KRgbBlack);
	TInt baseline = (stop.Height() + iBig->AscentInPixels() - iBig->DescentInPixels()) / 2;
	aGc.DrawText(_L("Stop typing"), stop, baseline, CGraphicsContext::ECenter);
	}

void CCsView::Draw(const TRect& aRect) const
	{
	CWindowGc& gc = SystemGc();
	TRect r = Rect();
	gc.SetBrushStyle(CGraphicsContext::ESolidBrush);
	gc.SetBrushColor(KRgbWhite);
	gc.SetPenStyle(CGraphicsContext::ENullPen);
	gc.DrawRect(iUi.KbdMode() ? r : aRect);
	gc.SetPenStyle(CGraphicsContext::ESolidPen);
	gc.SetBrushStyle(CGraphicsContext::ENullBrush);

	if (iUi.KbdMode())
		DrawKeyboard(gc);
	else
		{
		// (only the parts asked for: a choice, or the strip, at a time)
		TInt lines = StateLines();
		for (TInt i = 0; i < ECsTileCount; i++)
			if (TileRect(i).Intersects(aRect))
				DrawTile(gc, i, lines);
		if (StripRect().Intersects(aRect))
			DrawStrip(gc);
		}
	gc.DiscardFont();
	}

// ===========================================================================
// Dialogs
// ===========================================================================

void CCsPrefsDialog::PreLayoutDynInitL()
	{
	SetChoiceListCurrentItem(ECsDlgRate, iSettings.iRate);
	SetChoiceListCurrentItem(ECsDlgControl, iSettings.iControl ? 1 : 0);
	SetChoiceListCurrentItem(ECsDlgBattery, iSettings.iBatteryStop ? 1 : 0);
	SetChoiceListCurrentItem(ECsDlgOnClose, iSettings.iKeepOnClose ? 1 : 0);
	}

TBool CCsPrefsDialog::OkToExitL(TInt /*aButtonId*/)
	{
	iSettings.iRate = ChoiceListCurrentItem(ECsDlgRate);
	iSettings.iControl = ChoiceListCurrentItem(ECsDlgControl) == 1;
	iSettings.iBatteryStop = ChoiceListCurrentItem(ECsDlgBattery) == 1;
	iSettings.iKeepOnClose = ChoiceListCurrentItem(ECsDlgOnClose) == 1;
	return ETrue;
	}

void CCsConnDialog::PreLayoutDynInitL()
	{
	TPsiLink link;
	link.SetDefaults();
	link.Load(iCoeEnv->FsSession());
	SetChoiceListCurrentItem(ECsDlgLink, link.iNetMode ? 1 : 0);
	SetChoiceListCurrentItem(ECsDlgBaud, link.iBaudIndex);
	SetChoiceListCurrentItem(ECsDlgFlow, link.iRtsCts ? 1 : 0);
	SetEdwinTextL(ECsDlgPppStart, &link.iPppStart);
	SetChoiceListCurrentItem(ECsDlgProxy, link.iUseProxy ? 1 : 0);
	SetEdwinTextL(ECsDlgProxyHost, &link.iProxyHost);
	SetNumberEditorValue(ECsDlgProxyPort, link.iProxyPort);
	SetChoiceListCurrentItem(ECsDlgKeepUp, link.iKeepUp ? 0 : 1);
	DimKeepUpLine();
	}

// staying connected after use is the Psion Internet route's only: the
// modem route always hangs up (it holds the serial port)
void CCsConnDialog::DimKeepUpLine()
	{
	SetLineDimmedNow(ECsDlgKeepUp, ChoiceListCurrentItem(ECsDlgLink) != 1);
	}

void CCsConnDialog::HandleControlStateChangeL(TInt aControlId)
	{
	if (aControlId == ECsDlgLink)
		DimKeepUpLine();
	}

TBool CCsConnDialog::OkToExitL(TInt aButtonId)
	{
	TBuf<40> ppp;
	GetEdwinText(ppp, ECsDlgPppStart);
	ppp.TrimAll();
	if (aButtonId == ECsBidTest)
		{
		TBuf8<40> ppp8;
		ppp8.Copy(ppp);
		PgLinkTestL(ChoiceListCurrentItem(ECsDlgBaud), ChoiceListCurrentItem(ECsDlgFlow) == 1,
			ChoiceListCurrentItem(ECsDlgLink) == 1, ppp8, R_CS_TEST_DIALOG, ECsDlgTest1);
		return EFalse;
		}
	TBuf<60> host;
	GetEdwinText(host, ECsDlgProxyHost);
	host.Trim();
	RFs& fs = iCoeEnv->FsSession();
	TPsiLink link;
	link.SetDefaults();
	link.Load(fs);
	link.iNetMode = ChoiceListCurrentItem(ECsDlgLink) == 1;
	link.iBaudIndex = ChoiceListCurrentItem(ECsDlgBaud);
	link.iRtsCts = ChoiceListCurrentItem(ECsDlgFlow) == 1;
	link.iPppStart = ppp;
	link.iUseProxy = ChoiceListCurrentItem(ECsDlgProxy) == 1;
	link.iProxyHost = host;
	TInt port = NumberEditorValue(ECsDlgProxyPort);
	if (port > 0 && port < 65536)
		link.iProxyPort = port;
	link.iKeepUp = ChoiceListCurrentItem(ECsDlgKeepUp) == 0;
	link.Save(fs);
	// a link already up and now to stay up: hold it (psihold.exe ends by
	// itself when the setting goes the other way)
	if (link.iKeepUp && link.iNetMode)
		{
		TFileName near(iEikonEnv->EikAppUi()->Application()->AppFullName());
		PsiHoldStart(fs, near);
		}
	return ETrue;
	}

void CCsAboutDialog::PreLayoutDynInitL()
	{
	TBuf<32> title(_L("PsiCast "));
	title.Append(KVersion);
	((CEikLabel*)Control(ECsDlgAbout1))->SetFont(iEikonEnv->TitleFont());
	SetLabelL(ECsDlgAbout1, title);
	SetLabelL(ECsDlgAboutStatus, iStatus);
	}

// ===========================================================================
// The engine's process
// ===========================================================================

CCsWatcher::CCsWatcher(CCsAppUi& aUi)
	: CActive(EPriorityStandard), iUi(aUi)
	{
	CActiveScheduler::Add(this);
	}

CCsWatcher::~CCsWatcher()
	{
	Cancel();
	}

void CCsWatcher::Watch(RProcess& aProcess)
	{
	Cancel();
	iProcess = &aProcess;
	aProcess.Logon(iStatus);
	SetActive();
	}

void CCsWatcher::RunL()
	{
	iUi.EngineEnded();
	}

void CCsWatcher::DoCancel()
	{
	if (iProcess)
		iProcess->LogonCancel(iStatus);
	}

// ===========================================================================
// The app
// ===========================================================================

void CCsAppUi::ConstructL()
	{
	BaseConstructL();
	iLastPhase = -1;
	LoadSettings();
	TRAPD(pics, ToolbarPicturesL());
	(void)pics;                              // (no PsiCast.mbm: words only)
	if (iToolBar)
		{
		const TInt latch[3] = { ECsCmdShareButton, ECsCmdPause, ECsCmdKeyboard };
		for (TInt k = 0; k < 3; k++)
			{
			CEikButtonBase* b = (CEikButtonBase*)iToolBar->ControlById(latch[k]);
			if (b)
				b->SetBehavior(EEikButtonLatches);
			}
		if (!iSettings.iToolbar)
			iToolBar->MakeVisible(EFalse);
		}
	iView = new(ELeave) CCsView(*this);
	iView->ConstructL(ViewRect(iSettings.iToolbar));
	AddToStackL(iView);
	iTimer = CPeriodic::NewL(CActive::EPriorityStandard);
	iCoeEnv->RootWin().EnableOnEvents(EEventControlAlways);   // switch-on events in the background too
	iForeground = ETrue;
	// still sharing from before PsiCast closed? take it up again
	TRAPD(err, ReattachL());
	(void)err;
	SetTickRate(EFalse);
	ButtonStates();
	}

CCsAppUi::~CCsAppUi()
	{
	delete iTimer;
	iTimer = NULL;
	CapturePauseKey(EFalse);
	iClosing = ETrue;                        // (nothing more is drawn)
	KbdLeave(KNullDesC);                     // (closed while typing: the phone's keyboard goes)
	if (iView)
		{
		RemoveFromStack(iView);
		delete iView;
		iView = NULL;
		}
	if (iEngineOn)
		{
		// closed from the System screen or the list of open programs: the
		// same as Close
		if (iSettings.iKeepOnClose)
			Detach();
		else
			Stop(EFalse);
		}
	delete iWatcher;
	iRinger.Close();
	if (iChunkOpen)
		iChunk.Close();
	}

// ----- settings -----------------------------------------------------------------

void CCsAppUi::LoadSettings()
	{
	iSettings.iToolbar = 1;
	iSettings.iZoom = 1;
	iSettings.iRate = 2;                     // 10 a second
	iSettings.iControl = 0;                  // watch only, as standard
	iSettings.iBatteryStop = 1;
	iSettings.iKeepOnClose = 0;
	iSettings.iAtomKbd = 0;
	RFile f;
	if (f.Open(iCoeEnv->FsSession(), KIniFile, EFileRead) != KErrNone)
		return;
	TBuf8<8 + 4 * 32> d;
	TInt r = f.Read(d);
	f.Close();
	if (r != KErrNone || d.Length() < 8)
		return;
	const TUint8* p = d.Ptr();
	TInt version = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
	TInt count = p[4] | (p[5] << 8) | (p[6] << 16) | (p[7] << 24);
	if (version < 1 || count < 0)
		return;
	TInt* field = &iSettings.iToolbar;
	for (TInt i = 0; i < count && i < KCsSettingsFields && 8 + 4 * i + 4 <= d.Length(); i++)
		{
		const TUint8* q = p + 8 + 4 * i;
		field[i] = q[0] | (q[1] << 8) | (q[2] << 16) | (q[3] << 24);
		}
	if (iSettings.iZoom < 0 || iSettings.iZoom > 2)
		iSettings.iZoom = 1;
	if (iSettings.iRate < 0 || iSettings.iRate > 3)
		iSettings.iRate = 2;
	if (iSettings.iAtomKbd < 0 || iSettings.iAtomKbd > 2)
		iSettings.iAtomKbd = 0;
	}

void CCsAppUi::SaveSettings()
	{
	RFs& fs = iCoeEnv->FsSession();
	fs.MkDirAll(KIniFile);
	TBuf8<8 + 4 * KCsSettingsFields> d;
	const TInt head[2] = { KCsSettingsVersion, KCsSettingsFields };
	const TInt* field = &iSettings.iToolbar;
	for (TInt i = 0; i < 2 + KCsSettingsFields; i++)
		{
		TInt v = i < 2 ? head[i] : field[i - 2];
		d.Append((TUint8)v);
		d.Append((TUint8)(v >> 8));
		d.Append((TUint8)(v >> 16));
		d.Append((TUint8)(v >> 24));
		}
	RFile f;
	if (f.Replace(fs, KIniFile, EFileWrite) != KErrNone)
		return;
	f.Write(d);
	f.Close();
	}

// ----- the engine ---------------------------------------------------------------

static TUint ProcessIdOf(RProcess& aProcess)
	{
	TProcessId id = aProcess.Id();
	TUint v = 0;
	Mem::Copy(&v, &id, sizeof(v));
	return v;
	}

static TBool OpenProcess(RProcess& aProcess, TUint aPid)
	{
	if (aPid == 0)
		return EFalse;
	TProcessId id;
	Mem::Copy(&id, &aPid, sizeof(aPid));
	if (aProcess.Open(id) != KErrNone)
		return EFalse;
	if (aProcess.ExitType() != EExitPending)
		{
		aProcess.Close();
		return EFalse;
		}
	return ETrue;
	}

// An engine still sharing after PsiCast closed (Keep sharing): PsiCast
// watches it again, and it watches PsiCast again
TBool CCsAppUi::ReattachL()
	{
	RChunk c;
	if (c.OpenGlobal(_L(PSI_SHARED_NAME), EFalse) != KErrNone)
		return EFalse;
	CsShared* s = (CsShared*)c.Base();
	if (s->magic != CS_MAGIC || s->layout != CS_LAYOUT || s->state == PSI_STATE_EXITED
		|| s->phase == CS_PHASE_STOPPED || !OpenProcess(iProcess, s->eng_pid))
		{
		c.Close();
		return EFalse;
		}
	iChunk = c;
	iChunkOpen = ETrue;
	iShared = s;
	RProcess me;
	s->app_pid = ProcessIdOf(me);
	s->detached = 0;
	s->kbd_mode = 0;
	s->kbd_only = 0;
	iPaused = s->paused;
	iEngineOn = ETrue;
	iSaidSharing = s->phase == CS_PHASE_SHARING;
	if (!iWatcher)
		iWatcher = new(ELeave) CCsWatcher(*this);
	iWatcher->Watch(iProcess);
	PassSettings();
	CapturePauseKey(ETrue);
	return ETrue;
	}

// a byte in the engine's key ring, and its doorbell: its wait ends at once
void CCsAppUi::Poke()
	{
	if (!iShared)
		return;
	PsiShared& n = iShared->net;
	if (n.kbd_head - n.kbd_tail < PSI_KBD_SIZE)
		{
		n.kbd[n.kbd_head % PSI_KBD_SIZE] = 0;
		n.kbd_head++;
		}
	iRinger.Ring(&n.eng_bell);
	}

void CCsAppUi::PassSettings()
	{
	if (!iShared)
		return;
	iShared->max_rate = KRates[iSettings.iRate];
	iShared->allow_control = iSettings.iControl;
	iShared->paused = iPaused;
	iShared->battery_stop = iSettings.iBatteryStop;
	iShared->kbd_mode = iKbdMode;
	iShared->kbd_only = iKbdOnly;
	iShared->kbd_hold = iKbdMode ? iHeld : 0;
	iShared->app_seq++;
	Poke();
	}

// aKbdOnly: for the phone keyboard only (Type on phone while not sharing):
// the engine connects, but sends no pictures
void CCsAppUi::StartL(TBool aKbdOnly)
	{
	if (iEngineOn)
		return;
	if (!iChunkOpen)
		{
		TInt r = iChunk.CreateGlobal(_L(PSI_SHARED_NAME), sizeof(CsShared), sizeof(CsShared));
		if (r == KErrAlreadyExists)
			{
			r = iChunk.OpenGlobal(_L(PSI_SHARED_NAME), EFalse);
			// (an engine from another version may still hold it: stop that
			// one. Only net, magic and layout are where this version has
			// them; with another layout eng_pid is not, so the engine is
			// asked to quit through net, which every version has first)
			if (r == KErrNone)
				{
				CsShared* old = (CsShared*)iChunk.Base();
				if (old->magic == CS_MAGIC && old->layout == CS_LAYOUT)
					{
					RProcess p;
					if (OpenProcess(p, old->eng_pid))
						{
						p.Kill(0);
						p.Close();
						PsiLinkTimersBack();
						}
					}
				else if (old->magic == CS_MAGIC && old->net.magic == PSI_SHARED_MAGIC
					&& old->net.state != PSI_STATE_EXITED)
					{
					old->net.quit = 1;
					PsiShared& n = old->net;
					if (n.kbd_head - n.kbd_tail < PSI_KBD_SIZE)
						{
						n.kbd[n.kbd_head % PSI_KBD_SIZE] = 0;
						n.kbd_head++;
						}
					iRinger.Ring(&n.eng_bell);
					for (TInt i = 0; i < 60 && old->net.state != PSI_STATE_EXITED; i++)
						User::After(100000);
					PsiLinkTimersBack();
					}
				}
			}
		User::LeaveIfError(r);
		iChunkOpen = ETrue;
		iShared = (CsShared*)iChunk.Base();
		}
	CsShared* s = iShared;
	Mem::FillZ(s, sizeof(CsShared));
	s->net.magic = PSI_SHARED_MAGIC;
	TPsiLink link;
	link.SetDefaults();
	link.Load(iCoeEnv->FsSession());
	s->net.baud_index = link.iBaudIndex;
	s->net.rtscts = link.iRtsCts;
	s->net.net_mode = link.iNetMode;
	s->net.modem_tls = 0;
	CopyToC(s->net.ppp_start, sizeof(s->net.ppp_start), link.iPppStart);
	CopyToC(s->net.dial_prefix, sizeof(s->net.dial_prefix), _L("ATDT"));
	CopyToC(s->net.host, sizeof(s->net.host), KAtomHost);
	s->net.port = KAtomPort;
	// (psiglue's own files go next to PsiCast, wherever it was installed)
	TParse home;
	home.Set(Application()->AppFullName(), NULL, NULL);
	TFileName homePath(home.DriveAndPath());
	if (homePath.Length() > 1 && homePath[homePath.Length() - 1] == '\\')
		homePath.SetLength(homePath.Length() - 1);
	CopyToC(s->net.home, sizeof(s->net.home), homePath);
	CopyToC(s->net.version, sizeof(s->net.version), KVersion);
	s->net.bell_magic = PSI_BELL_MAGIC;      // we ring eng_bell for every change, quit and switch-on
	s->net.app_beat = 0;                     // (not watched by psiglue: the engine watches our process)
	s->magic = CS_MAGIC;
	s->layout = CS_LAYOUT;
	RProcess me;
	s->app_pid = ProcessIdOf(me);
	s->viewers = -1;
	s->atom_kbd = -1;
	s->kbd_state = -1;
	iPaused = EFalse;
	iKbdOnly = aKbdOnly;
	PassSettings();

	TParse parse;
	parse.Set(Application()->AppFullName(), NULL, NULL);
	TFileName exe(parse.DriveAndPath());
	exe.Append(KEngineExe);
	TInt r = iProcess.Create(exe, KNullDesC);
	if (r != KErrNone)
		{
		iKbdOnly = EFalse;
		iEikonEnv->InfoMsg(_L("psicast.exe not found - install PsiCast again"));
		User::Leave(r);
		}
	iEngineOn = ETrue;
	iSaidSharing = EFalse;
	iLastPhase = -1;
	if (!iWatcher)
		iWatcher = new(ELeave) CCsWatcher(*this);
	iWatcher->Watch(iProcess);
	iProcess.Resume();
	CapturePauseKey(ETrue);
	SetTickRate(ETrue);
	ButtonStates();
	iView->DrawNow();
	}

void CCsAppUi::Stop(TBool aSay)
	{
	if (!iEngineOn)
		return;
	if (iWatcher)
		iWatcher->Cancel();
	TBool kbdOnly = iKbdOnly;
	iKbdMode = EFalse;                       // (the engine sends HOLD 0 and MODE 0 before BYE)
	iKbdOnly = EFalse;
	iShared->kbd_mode = 0;
	iShared->quitting = 1;
	iShared->net.quit = 1;
	iShared->app_seq++;
	Poke();
	// BYE, then the modem's hang-up (+++, ATH: about 3 s), saying so, as the
	// style guide asks of a wait; an engine that has died is not waited for
	TInt busy = KErrGeneral;
	if (iForeground && !iClosing)
		TRAP(busy, iEikonEnv->BusyMsgL(_L("Stopping sharing..."), EHLeftVBottom, TTimeIntervalMicroSeconds32(0)));
	for (TInt i = 0; i < 60 && iShared->state != PSI_STATE_EXITED && iProcess.ExitType() == EExitPending; i++)
		User::After(100000);
	if (busy == KErrNone)
		iEikonEnv->BusyMsgCancel();
	if (iShared->state != PSI_STATE_EXITED)
		{
		iProcess.Kill(0);
		PsiLinkTimersBack();
		}
	iProcess.Close();
	iEngineOn = EFalse;
	iPaused = EFalse;
	CapturePauseKey(EFalse);
	if (iBusyShown)
		{
		iEikonEnv->BusyMsgCancel();
		iBusyShown = EFalse;
		}
	SetTickRate(EFalse);
	if (aSay)
		Say(kbdOnly ? _L("Stopped typing on the phone") : _L("Sharing stopped"));
	ButtonStates();
	if (iView && !iClosing)
		iView->DrawNow();
	}

// Stop sharing: while typing on the phone the connection stays, for the
// keyboard only (no more pictures)
void CCsAppUi::StopSharing()
	{
	if (iKbdMode && iEngineOn)
		{
		iKbdOnly = ETrue;
		iPaused = EFalse;
		PassSettings();
		Say(_L("Sharing stopped"));
		ButtonStates();
		iView->DrawNow();
		return;
		}
	Stop(ETrue);
	}

// Start sharing: from a keyboard-only connection, the pictures start
void CCsAppUi::StartSharingL()
	{
	if (iEngineOn && iKbdOnly)
		{
		iKbdOnly = EFalse;
		iSaidSharing = EFalse;               // ("Sharing the screen" when it does)
		PassSettings();
		ButtonStates();
		iView->DrawNow();
		return;
		}
	StartL();
	}

// PsiCast closes, the sharing goes on (Preferences > When PsiCast closes)
void CCsAppUi::Detach()
	{
	if (!iEngineOn)
		return;
	if (iWatcher)
		iWatcher->Cancel();
	iShared->kbd_mode = 0;                   // (the engine takes the phone's keyboard down)
	iShared->detached = 1;
	iShared->app_pid = 0;
	iShared->app_seq++;
	Poke();
	iProcess.Close();
	iEngineOn = EFalse;
	}

// it ended by itself: the battery, or something it could not get past
void CCsAppUi::EngineEnded()
	{
	TInt why = iShared ? iShared->stop_reason : CS_STOP_NONE;
	TExitType type = iProcess.ExitType();
	TBool kbdOnly = iKbdOnly;
	iProcess.Close();
	iEngineOn = EFalse;
	iPaused = EFalse;
	iKbdMode = EFalse;                       // (the Atom lets the phone go when link A closes)
	iKbdOnly = EFalse;
	CapturePauseKey(EFalse);
	if (iBusyShown)
		{
		iEikonEnv->BusyMsgCancel();
		iBusyShown = EFalse;
		}
	SetTickRate(EFalse);
	if (type != EExitKill && type != EExitPending)
		PsiLinkTimersBack();                 // (a panic: the link's timers back on)
	TBuf<80> t(kbdOnly ? _L("Stopped typing on the phone") : _L("Sharing stopped"));
	switch (why)
		{
	case CS_STOP_BATTERY:
		t.Append(_L(" - the battery is low"));
		break;
	case CS_STOP_CAPTURE:
		t.Append(_L(" - the screen could not be read"));
		break;
	case CS_STOP_MEMORY:
		t.Append(_L(" - not enough memory"));
		break;
	default:
		break;
		}
	Say(t);
	ButtonStates();
	iView->DrawNow();
	}

void CCsAppUi::StateText(TDes& aText) const
	{
	if (!iEngineOn || !iShared)
		{
		aText = _L("Not sharing");
		return;
		}
	switch (iShared->phase)
		{
	case CS_PHASE_STARTING:
	case CS_PHASE_CONNECTING:
		aText = _L("Connecting to the Atom...");
		return;
	case CS_PHASE_WAITING:
		aText = _L("Waiting for the Atom...");
		return;
	case CS_PHASE_STOPPED:
		aText = _L("Not sharing");
		return;
		}
	if (iKbdOnly)
		aText = _L("Typing on the phone - not sharing");
	else if (iPaused)
		aText = _L("Paused - viewers see the last picture");
	else if (iShared->viewers < 0)
		aText = _L("Sharing");
	else if (iShared->viewers == 0)
		aText = _L("Sharing - no viewers yet");
	else if (iShared->viewers == 1)
		aText = _L("Sharing with 1 viewer");
	else
		aText.Format(_L("Sharing with %d viewers"), iShared->viewers);
	}

// how each of the page's choices is now, in a few words; aOn: on (the
// choice is pressed in, as its toolbar button is latched); aDimmed: not
// available
void CCsAppUi::TileState(TInt aTile, TDes& aText, TBool& aOn, TBool& aDimmed) const
	{
	aOn = aDimmed = EFalse;
	const CsShared* s = Shared();
	TInt phase = s ? s->phase : CS_PHASE_STOPPED;
	switch (aTile)
		{
	case ECsTileShare:
		aOn = Sharing();
		if (!aOn)
			aText = _L("Not sharing");
		else if (phase == CS_PHASE_STARTING || phase == CS_PHASE_CONNECTING)
			aText = _L("Connecting to the Atom...");
		else if (phase == CS_PHASE_WAITING)
			aText = _L("Waiting for the Atom...");
		else
			{
			aText = iPaused ? _L("Paused") : _L("Sharing");
			if (s->viewers == 0 && !iPaused)
				aText.Append(_L(" - no viewers yet"));
			else if (s->viewers > 0)
				aText.AppendFormat(_L(" - %d watching"), s->viewers);
			}
		break;
	case ECsTileType:
		aOn = iKbdMode;
		if (!KbdAvailable())
			{
			aText = KNoPhoneKbd;
			aDimmed = ETrue;
			}
		else if (!s || phase != CS_PHASE_SHARING || s->kbd_state < 0)
			aText = _L("Not typing");
		else if (s->kbd_state == 0)
			aText = _L("Off in the Atom's settings");
		else if (s->kbd_state == 2)
			aText = _L("Waiting for the phone");
		else if (s->kbd_state == 3)
			aText = _L("Phone connected");
		else
			aText = _L("Not typing");
		break;
	default:
		aOn = iSettings.iControl;
		aText = aOn ? _L("Viewers can tap & type") : _L("View only");
		break;
		}
	}

// ----- the phone keyboard ------------------------------------------------------------
// Share > Type on phone (Shift+Ctrl+B, the Type button): the Atom is a
// Bluetooth keyboard to the phone, and every key typed here goes to it as
// PROTOCOL.md's KEYS. The keys are turned into KEYS events here, where the
// key events are, and put in the chunk's ring; the bell makes the engine
// send them at once.

// Offered only if the Atom has a phone keyboard: as its WELCOME said, or,
// not connected, as the last one said (not known yet: offered, and the
// mode ends if WELCOME says not)
TBool CCsAppUi::KbdAvailable() const
	{
	if (iEngineOn && iShared && iShared->phase == CS_PHASE_SHARING && iShared->atom_kbd >= 0)
		return iShared->atom_kbd == 1;
	return iSettings.iAtomKbd != 2;
	}

void CCsAppUi::KbdStartL()
	{
	if (iKbdMode)
		return;
	if (!KbdAvailable())
		{
		Say(KNoPhoneKbd);
		ButtonStates();
		return;
		}
	if (!iEngineOn)
		StartL(ETrue);                       // (connects for the keyboard only: no pictures)
	iKbdMode = ETrue;
	iHeld = 0;
	iCtrlDigits = iSwallowKey = iKeysPut = EFalse;
	iKeysBase = iShared->keys_sent;
	PassSettings();                          // (kbd_mode: the engine sends MODE 1)
	SetTickRate(ETrue);
	ButtonStates();
	iView->DrawNow();
	}

// leaves the mode (the engine sends HOLD 0 and MODE 0), and the keyboard-
// only connection; aSay: the infoprint, if any
void CCsAppUi::KbdLeave(const TDesC& aSay)
	{
	if (!iKbdMode)
		return;
	iKbdMode = EFalse;
	if (iKbdOnly && iEngineOn)
		Stop(EFalse);                        // (MODE 0, then BYE and the hang-up)
	else
		PassSettings();
	if (aSay.Length())
		Say(aSay);
	if (iClosing)
		return;
	ButtonStates();
	if (iView)
		iView->DrawNow();
	}

void CCsAppUi::KbdPut(TInt aKind, TUint aMods, TUint aCode)
	{
	CsShared* s = iShared;
	if (!s || s->keys_head - s->keys_tail >= (TUint)CS_KEYS_SIZE)
		return;                              // (the ring is full: the engine is far behind)
	volatile TUint8* e = s->keys + 4 * (s->keys_head % CS_KEYS_SIZE);
	e[0] = (TUint8)aKind;
	e[1] = (TUint8)aMods;
	e[2] = (TUint8)aCode;
	e[3] = (TUint8)(aCode >> 8);
	s->keys_head++;                          // (after the event: the engine reads up to head)
	iKeysPut = ETrue;
	}

// A key event in keyboard mode, as KEYS events (PROTOCOL.md):
//  - Ctrl and Menu (Cmd) are HOLD, from their own key down and up, so
//    Cmd held while Tab is pressed again and again works;
//  - Ctrl+letter is the letter with Ctrl (EPOC makes a control code of it:
//    the scan code says which letter);
//  - Ctrl+digit: the window server keeps the digits back for a character
//    code (Ctrl held, digits, Ctrl let go) and makes no key event for them,
//    so each digit goes at its key down, and the code that comes when Ctrl
//    goes up is not sent;
//  - Enter, Esc, Tab, Del (Backspace), Fn+Del and Shift+Del (Delete; the
//    5mx makes Backspace of them, with Fn or Shift), the arrows and
//    Fn+arrows (Home, End, Page up, Page down) are KEY, with Ctrl and Shift;
//  - any other character is CHAR, as the Psion's layout made it (Shift and
//    Fn are in it), with Ctrl and Cmd if held;
//  - Shift+Ctrl+B leaves the mode and is not sent.
void CCsAppUi::KbdKey(const TWsEvent& aEvent)
	{
	const TKeyEvent& k = *aEvent.Key();
	TUint m = k.iModifiers;
	TInt sc = k.iScanCode;
	if (aEvent.Type() != EEventKey)
		{
		TBool down = aEvent.Type() == EEventKeyDown;
		TUint bit = sc == EStdKeyLeftCtrl || sc == EStdKeyRightCtrl ? CS_MOD_CTRL : sc == EStdKeyMenu ? CS_MOD_GUI : 0;
		if (bit)
			{
			TUint held = down ? iHeld | bit : iHeld & ~bit;
			if (held != iHeld)
				{
				iHeld = held;
				iShared->kbd_hold = iHeld;       // (for a new connection: PassSettings)
				KbdPut(CS_KEY_HOLD, iHeld, 0);
				}
			if (!down && bit == CS_MOD_CTRL && iCtrlDigits)
				{
				iCtrlDigits = EFalse;
				iSwallowKey = ETrue;
				}
			return;
			}
		if (!down)
			return;
		iSwallowKey = EFalse;                // (a new key: the code did not come)
		if ((m & EModifierCtrl) && sc >= '0' && sc <= '9')
			{
			KbdPut(CS_KEY_CHAR, CS_MOD_CTRL | (iHeld & CS_MOD_GUI), sc);
			iCtrlDigits = ETrue;
			}
		return;
		}
	TUint code = k.iCode;
	if (code == EKeyMenu || sc == EStdKeyMenu)
		return;                              // (Cmd: its HOLD)
	if (iSwallowKey)
		{
		iSwallowKey = EFalse;                // (the character code Ctrl+digits made)
		return;
		}
	TUint ctrl = m & EModifierCtrl ? CS_MOD_CTRL : 0;
	TUint shift = m & EModifierShift ? CS_MOD_SHIFT : 0;
	TUint gui = iHeld & CS_MOD_GUI;
	if (ctrl && shift && sc == KKbdLeaveScan)
		{
		KbdLeave(_L("Stopped typing on the phone"));
		return;
		}
	if (ctrl && sc >= '0' && sc <= '9')
		return;                              // (sent at its key down)
	if (ctrl && sc >= 'A' && sc <= 'Z')
		{
		KbdPut(CS_KEY_CHAR, ctrl | gui, shift ? sc : sc + ('a' - 'A'));
		return;
		}
	TUint usage = 0;
	switch (code)
		{
	case EKeyEnter: usage = 0x28; break;
	case EKeyEscape: usage = 0x29; break;
	case EKeyBackspace:                  // (Del; Fn+Del and Shift+Del delete forwards)
		usage = m & (EModifierFunc | EModifierShift) ? 0x4c : 0x2a;
		if (usage == 0x4c)
			shift = 0;
		break;
	case EKeyTab: usage = 0x2b; break;
	case EKeyDelete: usage = 0x4c; break;
	case EKeyRightArrow: usage = 0x4f; break;
	case EKeyLeftArrow: usage = 0x50; break;
	case EKeyDownArrow: usage = 0x51; break;
	case EKeyUpArrow: usage = 0x52; break;
	case EKeyHome: usage = 0x4a; break;
	case EKeyEnd: usage = 0x4d; break;
	case EKeyPageUp: usage = 0x4b; break;
	case EKeyPageDown: usage = 0x4e; break;
	default: break;
		}
	if (usage)
		{
		KbdPut(CS_KEY_KEY, ctrl | shift | gui, usage);
		return;
		}
	if (sc == EStdKeySpace)
		code = ' ';                          // (Ctrl+Space makes no space)
	if (code >= ' ' && code <= 0xff && code != 0x7f)
		KbdPut(CS_KEY_CHAR, ctrl | gui, code);
	// (the rest - the Psion's own keys, such as Off - are not the phone's)
	}

// ----- the timer ---------------------------------------------------------------------

void CCsAppUi::SetTickRate(TBool aFast)
	{
	if (!iTimer)
		return;
	if (iTimer->IsActive() && aFast == iFast)
		return;
	iTimer->Cancel();
	iFast = aFast;
	TInt t = aFast ? KTickFast : KTickIdle;
	iTimer->Start(t, t, TCallBack(TickCallback, this));
	}

TInt CCsAppUi::TickCallback(TAny* aSelf)
	{
	// (a leave here, BusyMsgL's say, must not reach the scheduler: an error
	// dialog for nothing)
	TRAPD(err, ((CCsAppUi*)aSelf)->Tick());
	(void)err;
	return 0;
	}

void CCsAppUi::Tick()
	{
	if (!iEngineOn || !iShared)
		return;
	TInt phase = iShared->phase;
	TBool connecting = phase == CS_PHASE_STARTING || phase == CS_PHASE_CONNECTING;
	SetTickRate(connecting || iKbdMode);     // (typing: the phone's state and the count soon)
	// what this Atom said about a phone keyboard, remembered for Type on
	// phone's dimming while not connected
	TInt kbd = iShared->atom_kbd == 1 ? 1 : iShared->atom_kbd == 0 ? 2 : 0;
	if (phase == CS_PHASE_SHARING && kbd && kbd != iSettings.iAtomKbd)
		{
		iSettings.iAtomKbd = kbd;
		SaveSettings();
		}
	if (iKbdMode && phase == CS_PHASE_SHARING && kbd == 2)
		{
		KbdLeave(KNoPhoneKbd);
		return;
		}
	// the busy message bottom left while it connects (in front only)
	if (connecting && iForeground && !iBusyShown)
		{
		iEikonEnv->BusyMsgL(_L("Connecting to the Atom..."), EHLeftVBottom, TTimeIntervalMicroSeconds32(300000));
		iBusyShown = ETrue;
		}
	else if (!connecting && iBusyShown)
		{
		iEikonEnv->BusyMsgCancel();
		iBusyShown = EFalse;
		}
	if (phase == CS_PHASE_SHARING && !iSaidSharing && !iKbdOnly)
		{
		Say(_L("Sharing the screen"));
		iSaidSharing = ETrue;
		}
	if (phase != iLastPhase)
		{
		if (phase == CS_PHASE_WAITING && iLastPhase == CS_PHASE_SHARING)
			Say(_L("Lost the Atom - trying again"));
		iLastPhase = phase;
		}
	if (iShared->eng_seq != iSeenSeq)
		{
		iSeenSeq = iShared->eng_seq;
		iView->Update();                     // (what has changed only)
		}
	}

// ----- commands ------------------------------------------------------------------------

void CCsAppUi::Say(const TDesC& aText)
	{
	if (iForeground)
		iEikonEnv->InfoMsg(aText);
	else
		User::InfoPrint(aText);              // (over the program in front)
	}

void CCsAppUi::PauseL(TBool aPause)
	{
	if (!Sharing())
		{
		Say(_L("Not available - not sharing"));
		ButtonStates();
		return;
		}
	if (aPause == iPaused)
		return;
	iPaused = aPause;
	PassSettings();
	Say(iPaused ? _L("Sharing paused") : _L("Sharing again"));
	ButtonStates();
	iView->DrawNow();
	}

void CCsAppUi::ControlL(TBool aAllow)
	{
	if (aAllow && !iSettings.iControl
		&& !iEikonEnv->QueryWinL(_L("Viewers will be able to tap and type on this Psion"), _L("Allow control from the viewer?")))
		{
		ButtonStates();
		return;
		}
	iSettings.iControl = aAllow;
	SaveSettings();
	PassSettings();
	Say(aAllow ? _L("Viewers can tap and type") : _L("Viewers can only watch"));
	ButtonStates();
	iView->DrawNow();
	}

// PsiCast to the back: the program used before it comes to the front, to
// be shown
void CCsAppUi::PreviousProgram()
	{
	iCoeEnv->RootWin().SetOrdinalPosition(-1);
	}

void CCsAppUi::PrefsL()
	{
	TCsSettings s = iSettings;
	CCsPrefsDialog* dlg = new(ELeave) CCsPrefsDialog(s);
	if (!dlg->ExecuteLD(R_CS_PREFS_DIALOG))
		return;
	s.iAtomKbd = iSettings.iAtomKbd;         // (Tick may have learnt it while the dialog was up)
	iSettings = s;
	SaveSettings();
	PassSettings();
	ButtonStates();
	iView->DrawNow();
	}

void CCsAppUi::ConnSettingsL()
	{
	CCsConnDialog* dlg = new(ELeave) CCsConnDialog;
	if (!dlg->ExecuteLD(R_CS_CONN_DIALOG))
		return;
	// the engine reads the settings as it starts: start it again
	if (iEngineOn)
		{
		Stop(EFalse);
		StartL();
		}
	}

void CCsAppUi::AboutL()
	{
	TBuf<100> status;
	StateText(status);
	if (iEngineOn && iShared && iShared->capture[0])
		{
		TBuf<40> c;
		c.Copy(FromC(iShared->capture, sizeof(iShared->capture) - 1));
		status.Append(_L(" - "));
		status.Append(c);
		}
	CCsAboutDialog* dlg = new(ELeave) CCsAboutDialog(status);
	dlg->ExecuteLD(R_CS_ABOUT_DIALOG);
	}

void CCsAppUi::ZoomL(TInt aStep)
	{
	iSettings.iZoom = (iSettings.iZoom + aStep + 3) % 3;     // (round, as the built-in programs)
	iView->ApplyZoomL();
	iView->DrawNow();
	SaveSettings();
	}

TRect CCsAppUi::ViewRect(TBool aToolbar) const
	{
	TRect r = ClientRect();
	if (!aToolbar || !iToolBar)
		r.iBr.iX = iEikonEnv->ScreenDevice()->SizeInPixels().iWidth;
	return r;
	}

void CCsAppUi::ShowToolBarL(TBool aShow)
	{
	if (!iToolBar)
		return;
	iToolBar->MakeVisible(aShow);
	if (aShow)
		iToolBar->DrawNow();
	iView->SetViewRectL(ViewRect(aShow));
	iSettings.iToolbar = aShow;
	SaveSettings();
	}

// the latching buttons say what is so
void CCsAppUi::ButtonStates()
	{
	if (!iToolBar || iClosing)
		return;
	const TInt ids[3] = { ECsCmdShareButton, ECsCmdPause, ECsCmdKeyboard };
	const TBool on[3] = { Sharing(), Sharing() && iPaused, iKbdMode };
	for (TInt k = 0; k < 3; k++)
		{
		CEikButtonBase* b = (CEikButtonBase*)iToolBar->ControlById(ids[k]);
		if (!b)
			continue;
		CEikButtonBase::TState want = on[k] ? CEikButtonBase::ESet : CEikButtonBase::EClear;
		if (b->State() != want)
			{
			b->SetState(want);
			if (iToolBar->IsVisible())
				b->DrawNow();
			}
		}
	}

// Shift+Ctrl+W pauses from any program while the screen is shared (it is
// taken from the others meanwhile): no need to come back to PsiCast first
void CCsAppUi::CapturePauseKey(TBool aOn)
	{
	if (aOn && !iPauseCapture)
		{
		TInt32 h = iCoeEnv->RootWin().CaptureKey(KPauseKeyCode, EModifierCtrl | EModifierShift, EModifierCtrl | EModifierShift);
		iPauseCapture = h > 0 ? h : 0;
		}
	else if (!aOn && iPauseCapture)
		{
		iCoeEnv->RootWin().CancelCaptureKey(iPauseCapture);
		iPauseCapture = 0;
		}
	}

void CCsAppUi::HandleCommandL(TInt aCommand)
	{
	switch (aCommand)
		{
	case ECsCmdStart:
		if (Sharing())
			Say(_L("Already sharing"));
		else
			StartSharingL();
		break;
	case ECsCmdStop:
		if (!Sharing())
			Say(_L("Not available - not sharing"));
		else
			StopSharing();
		break;
	case ECsCmdShareButton:
		if (Sharing())
			StopSharing();
		else
			StartSharingL();
		break;
	case ECsCmdKeyboard:
		if (iKbdMode)
			KbdLeave(_L("Stopped typing on the phone"));
		else
			KbdStartL();
		break;
	case ECsCmdPause:
		PauseL(!iPaused);
		break;
	case ECsCmdControl:
		ControlL(!iSettings.iControl);
		break;
	case ECsCmdPrevious:
		PreviousProgram();
		break;
	case ECsCmdZoomIn:
		ZoomL(1);
		break;
	case ECsCmdZoomOut:
		ZoomL(-1);
		break;
	case ECsCmdToolbar:
		ShowToolBarL(!iToolBar || !iToolBar->IsVisible());
		break;
	case ECsCmdPrefs:
		PrefsL();
		break;
	case ECsCmdConnSettings:
		ConnSettingsL();
		break;
	case ECsCmdDisconnect:
		{
		// the Psion Internet connection down, even when "Stay connected"
		// holds it (core/ssh/pglinktest.cpp; ends psihold.exe). Sharing stops
		// first, or it would only dial again.
		TPsiLink link;
		link.SetDefaults();
		link.Load(iCoeEnv->FsSession());
		if (!link.iNetMode)
			Say(_L("Not available - the modem route hangs up after use"));
		else
			{
			Stop(EFalse);
			PgLinkDisconnectL();
			}
		break;
		}
	case ECsCmdHelp:
		{
		CCsHelpDialog* dlg = new(ELeave) CCsHelpDialog;
		dlg->ExecuteLD(R_CS_HELP_DIALOG);
		break;
		}
	case ECsCmdAbout:
		AboutL();
		break;
	case EEikCmdExit:
		KbdLeave(KNullDesC);
		if (iEngineOn)
			{
			if (iSettings.iKeepOnClose)
				Detach();
			else
				Stop(EFalse);
			}
		Exit();
		break;
		}
	}

// Only items in this pane are dimmed or ticked (EIKON 8 otherwise)
void CCsAppUi::DynInitMenuPaneL(TInt aMenuId, CEikMenuPane* aMenuPane)
	{
	switch (aMenuId)
		{
	case R_CS_SHARE_MENU:
		aMenuPane->SetItemDimmed(ECsCmdStart, Sharing());
		aMenuPane->SetItemDimmed(ECsCmdStop, !Sharing());
		aMenuPane->SetItemDimmed(ECsCmdPause, !Sharing());
		aMenuPane->SetItemButtonState(ECsCmdPause,
			Sharing() && iPaused ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
		aMenuPane->SetItemButtonState(ECsCmdControl,
			iSettings.iControl ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
		aMenuPane->SetItemDimmed(ECsCmdKeyboard, !KbdAvailable());
		aMenuPane->SetItemButtonState(ECsCmdKeyboard,
			iKbdMode ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
		break;
	case R_CS_VIEW_MENU:
		aMenuPane->SetItemButtonState(ECsCmdToolbar,
			iToolBar && iToolBar->IsVisible() ? EEikMenuItemSymbolOn : EEikMenuItemSymbolIndeterminate);
		break;
	case R_CS_TOOLS_MENU:
		{
		TPsiLink link;
		link.SetDefaults();
		link.Load(iCoeEnv->FsSession());
		aMenuPane->SetItemDimmed(ECsCmdDisconnect, !link.iNetMode || !PgLinkUp());
		break;
		}
		}
	}

// switched back on: the engine checks its connection is still there
void CCsAppUi::HandleSwitchOnEventL(CCoeControl* /*aDestination*/)
	{
	if (iEngineOn && iShared)
		{
		iShared->net.switch_on++;
		Poke();
		}
	}

void CCsAppUi::HandleWsEventL(const TWsEvent& aEvent, CCoeControl* aDestination)
	{
	// typing on the phone: every key is the phone's, before EIKON's menu
	// bar, shortcuts and controls see it (the Menu key too: Cmd)
	TInt type = aEvent.Type();
	if (iKbdMode && (type == EEventKey || type == EEventKeyDown || type == EEventKeyUp)
		&& !IsDisplayingMenuOrDialog())
		{
		KbdKey(aEvent);
		if (iKeysPut)
			{
			iKeysPut = EFalse;
			Poke();                          // (the engine sends them now)
			}
		return;
		}
	if (aEvent.Type() == EEventKey && iPauseCapture)
		{
		const TKeyEvent* k = aEvent.Key();
		if (k->iCode == KPauseKeyCode && (k->iModifiers & EModifierShift) && (k->iModifiers & EModifierCtrl)
			&& !(IsDisplayingMenuOrDialog() && iForeground))   // (a menu or dialog of ours has the keys: EIKON's hotkey, if it is the menu)
			{
			PauseL(!iPaused);                // (taken: it comes here in the background too)
			return;
			}
		}
	if (aEvent.Type() == EEventFocusGained)
		iForeground = ETrue;
	else if (aEvent.Type() == EEventFocusLost)
		{
		iForeground = EFalse;
		KbdLeave(_L("Stopped typing on the phone"));   // (another program in front: its keys are its own)
		if (iBusyShown)
			{
			iEikonEnv->BusyMsgCancel();
			iBusyShown = EFalse;
			}
		}
	CEikAppUi::HandleWsEventL(aEvent, aDestination);
	}

// a toolbar button's picture, from PsiCast.mbm (cast/pkg/mktbar.py)
void CCsAppUi::ButtonPictureL(TInt aId, TInt aIcon)
	{
	CEikCommandButton* b = iToolBar ? (CEikCommandButton*)iToolBar->ControlById(aId) : NULL;
	if (!b)
		return;
	TFileName mbm = Application()->BitmapStoreName();
	CFbsBitmap* bmp = iEikonEnv->CreateBitmapL(mbm, aIcon);
	CleanupStack::PushL(bmp);
	CFbsBitmap* mask = iEikonEnv->CreateBitmapL(mbm, aIcon + 1);
	CleanupStack::PushL(mask);
	b->SetPictureL(bmp, mask);
	CleanupStack::Pop(2);
	if (b->Picture())
		b->Picture()->SetAlignment(EHCenterVCenter);
	if (b->Label())
		b->Label()->SetAlignment(EHLeftVCenter);
	b->LayoutComponentsL();
	}

void CCsAppUi::ToolbarPicturesL()
	{
	ButtonPictureL(ECsCmdShareButton, EMbmToolShare);
	ButtonPictureL(ECsCmdPause, EMbmToolPause);
	ButtonPictureL(ECsCmdKeyboard, EMbmToolType);
	ButtonPictureL(ECsCmdPrevious, EMbmToolPrevious);
	}

// ===========================================================================
// Document, application, DLL
// ===========================================================================

CEikAppUi* CCsDocument::CreateAppUiL()
	{
	return new(ELeave) CCsAppUi;
	}

TUid CCsApplication::AppDllUid() const
	{
	return KUidPsiCast;
	}

CApaDocument* CCsApplication::CreateDocumentL()
	{
	return new(ELeave) CCsDocument(*this);
	}

EXPORT_C CApaApplication* NewApplication()
	{
	return new CCsApplication;
	}

GLDEF_C TInt E32Dll(TDllReason)
	{
	return KErrNone;
	}
