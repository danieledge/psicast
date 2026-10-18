// CSAPP.H - PsiCast.app: shares the Psion's screen with a web browser
// through the Atom modem.
//
// The app is small: one view, a page with its three choices (Share screen,
// Type on phone, Allow control) and how the sharing is going, the commands,
// and the settings. In its keyboard mode ("Type on phone", Shift+Ctrl+B) the
// view says how the typing goes instead, and every key typed on the Psion
// goes to the phone the Atom is a keyboard for (PROTOCOL.md, KEYS). The work - reading the screen, encoding it, sending it,
// injecting the viewer's taps and keys - is psicast.exe's, through the
// chunk in ../engine/psicast.h. The engine runs only while the screen is
// shared, and may go on after PsiCast has closed (Preferences); a PsiCast
// opened again finds it and takes it up.

#ifndef __CSAPP_H
#define __CSAPP_H

#include <coecntrl.h>
#include <coemain.h>
#include <eikappui.h>
#include <eikapp.h>
#include <eikdoc.h>
#include <eikenv.h>
#include <eikmenup.h>
#include <eikcmds.hrh>
#include <eikmenu.hrh>
#include <f32file.h>
#include <eikdialg.h>
#include <eikdialg.hrh>
#include <eikkeys.h>
#include <badesca.h>

extern "C" {
#include <psicast.h>
}
#include "psibell.h"
#include <psicast.rsg>
#include "psicast.hrh"

class CCsAppUi;

const TUid KUidPsiCast = { 0x01000A92 };

// Left() that doesn't mind a short text: EPOC's Left(n) panics (USER 22)
// when n is more than the length
inline TPtrC8 Clip8(const TDesC8& aText, TInt aMax)
	{
	return aText.Left(aMax < 0 ? 0 : aMax < aText.Length() ? aMax : aText.Length());
	}

// a C string from the chunk (always NUL-terminated by its writer), at most aMax long
inline TPtrC8 FromC(const char* aText, TInt aMax)
	{
	TInt n = 0;
	while (n < aMax && aText[n])
		n++;
	return TPtrC8((const TUint8*)aText, n);
	}

// PsiCast.ini: a versioned stream - a version, a count, then that many
// 32-bit fields. New settings are added at the end; an older file simply
// has fewer, and the rest keep their standard values.
struct TCsSettings
	{
	TInt iToolbar;                 // View > Show toolbar
	TInt iZoom;                    // 0 small, 1 medium, 2 large
	TInt iRate;                    // Highest update rate: 0 2, 1 5, 2 10, 3 20 a second
	TInt iControl;                 // Allow control from the viewer
	TInt iBatteryStop;             // Stop sharing when the battery is low
	TInt iKeepOnClose;             // When PsiCast closes: 0 stop sharing, 1 keep sharing
	TInt iAtomKbd;                 // the Atom last said: 0 not known, 1 it has a phone keyboard, 2 it has none
	};
const TInt KCsSettingsFields = 7;
const TInt KCsSettingsVersion = 1;

// ----- the view ---------------------------------------------------------------

// The main view is PsiCast's page: three big choices side by side (Share
// screen, Type on phone, Allow control), each a picture, its name and how
// it is now, and a status strip under them (where to look on the phone, how
// fast it goes, the keys sent). The arrow keys move between the choices,
// Enter or one tap acts. In the keyboard mode the view says how the typing
// goes instead, with a big Stop typing target.
enum TCsTile
	{
	ECsTileShare,
	ECsTileType,
	ECsTileControl,
	ECsTileCount
	};

const TInt KCsPartText = 260;

class CCsView : public CCoeControl
	{
public:
	CCsView(CCsAppUi& aUi) : iUi(aUi), iPressed(-1), iDown(-1) {}
	~CCsView();
	void ConstructL(const TRect& aRect);
	void ApplyZoomL();
	void SetViewRectL(const TRect& aRect);
	TKeyResponse OfferKeyEventL(const TKeyEvent& aKeyEvent, TEventCode aType);
	void Update();                          // the engine said something new
private:
	void Draw(const TRect& aRect) const;
	void DrawKeyboard(CWindowGc& aGc) const;
	void DrawLine(CWindowGc& aGc, TInt aY, const TDesC& aLabel, const TDesC& aText, TInt aRight = 0) const;
	void DrawTile(CWindowGc& aGc, TInt aTile, TInt aLines) const;
	TInt TileTextWidth() const;
	void DrawStrip(CWindowGc& aGc) const;
	void StripTexts(TDes& aPhone, TDes& aSending, TDes& aKeys, TDes& aWhy) const;
	TInt StripTop() const;
	TRect StripRect() const;
	TInt StateLines() const;
	void PartText(TInt aPart, TDes& aText) const;
	void InvalidateTile(TInt aTile);
	TRect TileRect(TInt aTile) const;
	TInt TileAt(const TPoint& aPoint) const;
	void ActL(TInt aTile);
	void SetFocusTile(TInt aTile);
	void LoadPicturesL();
	TRect StopRect() const;                 // keyboard mode: the "Stop typing" pen target
	void HandlePointerEventL(const TPointerEvent& aEvent);
	CFont* FontL(TInt aTwips, TBool aBold, CFont*& aOld);
	CCsAppUi& iUi;
	CFont* iBig;                   // the state: Sharing, Not sharing...
	CFont* iLabel;                 // the lines' labels (bold)
	CFont* iText;                  // ...and what they say; how each choice is
	CFont* iSmall;                 // the link's words, the status strip
	CFont* iSmallBold;             // the status strip's labels
	CFont* iName;                  // the page's choices' names
	CFbsBitmap* iPic[ECsTileCount];        // the choices' pictures (PsiCast.mbm), and their masks
	CFbsBitmap* iMask[ECsTileCount];
	TInt iFocus;                   // the choice the arrow keys are on
	TInt iPressed;                 // the choice the pen is down on (-1: none)
	TInt iDown;                    // ...and the one it went down on
	TBuf<KCsPartText> iShown[ECsTileCount + 1];   // what the choices and the strip said (Update)
	TInt iShownLines;              // ...and the lines the states took
	};

// ----- dialogs -----------------------------------------------------------------

class CCsPrefsDialog : public CEikDialog
	{
public:
	CCsPrefsDialog(TCsSettings& aSettings) : iSettings(aSettings) {}
private:
	void PreLayoutDynInitL();
	TBool OkToExitL(TInt aButtonId);
	TCsSettings& iSettings;
	};

class CCsConnDialog : public CEikDialog
	{
private:
	void PreLayoutDynInitL();
	void HandleControlStateChangeL(TInt aControlId);
	TBool OkToExitL(TInt aButtonId);
	void DimKeepUpLine();
	};

class CCsAboutDialog : public CEikDialog
	{
public:
	CCsAboutDialog(const TDesC& aStatus) : iStatus(aStatus) {}
private:
	void PreLayoutDynInitL();
	const TDesC& iStatus;
	};

class CCsHelpDialog : public CEikDialog
	{
private:
	void PreLayoutDynInitL();
	void PostLayoutDynInitL();
	void HandleControlStateChangeL(TInt aControlId);
	void ShowTopicL(TInt aTopic);
	TInt iTopic;
	};

// ----- the engine's process -------------------------------------------------------

class CCsWatcher : public CActive
	{
public:
	CCsWatcher(CCsAppUi& aUi);
	~CCsWatcher();
	void Watch(RProcess& aProcess);
private:
	void RunL();
	void DoCancel();
	CCsAppUi& iUi;
	RProcess* iProcess;
	};

// ----- the app -----------------------------------------------------------------

class CCsAppUi : public CEikAppUi
	{
public:
	~CCsAppUi();
	void ConstructL();
	const TCsSettings& Settings() const { return iSettings; }
	TBool Sharing() const { return iEngineOn && !iKbdOnly; }   // (not a keyboard-only connection)
	TBool KbdMode() const { return iKbdMode; }
	TUint KeysSent() const { return iShared ? iShared->keys_sent - iKeysBase : 0; }
	TBool Paused() const { return iPaused; }
	const CsShared* Shared() const { return iEngineOn ? iShared : NULL; }
	void StateText(TDes& aText) const;      // Sharing, Not sharing, Paused...
	void TileState(TInt aTile, TDes& aText, TBool& aOn, TBool& aDimmed) const;   // the page's choices
	TBool KbdAvailable() const;
	void EngineEnded();
	void HandleCommandL(TInt aCommand);
private:
	void DynInitMenuPaneL(TInt aMenuId, CEikMenuPane* aMenuPane);
	void HandleSwitchOnEventL(CCoeControl* aDestination);
	void HandleWsEventL(const TWsEvent& aEvent, CCoeControl* aDestination);

	// the engine
	TBool ReattachL();
	void StartL(TBool aKbdOnly = EFalse);
	void Stop(TBool aSay);
	void StopSharing();
	void StartSharingL();
	void Poke();
	void PassSettings();
	void Detach();

	// the timer
	static TInt TickCallback(TAny* aSelf);
	void Tick();
	void SetTickRate(TBool aFast);

	void PauseL(TBool aPause);
	void ControlL(TBool aAllow);

	// the phone keyboard
	void KbdStartL();
	void KbdLeave(const TDesC& aSay);
	void KbdKey(const TWsEvent& aEvent);
	void KbdPut(TInt aKind, TUint aMods, TUint aCode);
	void PreviousProgram();
	void PrefsL();
	void ConnSettingsL();
	void AboutL();
	void ZoomL(TInt aStep);
	void ShowToolBarL(TBool aShow);
	TRect ViewRect(TBool aToolbar) const;
	void ButtonPictureL(TInt aId, TInt aIcon);
	void ToolbarPicturesL();
	void ButtonStates();
	void CapturePauseKey(TBool aOn);
	void Say(const TDesC& aText);           // an infoprint, wherever PsiCast is
	void LoadSettings();
	void SaveSettings();

	TCsSettings iSettings;
	CCsView* iView;
	CPeriodic* iTimer;
	TBool iFast;
	RChunk iChunk;
	TBool iChunkOpen;
	CsShared* iShared;
	RProcess iProcess;
	TBool iEngineOn;
	CCsWatcher* iWatcher;
	TPsiBellRinger iRinger;
	TBool iPaused;
	TInt iLastPhase;
	TUint iSeenSeq;
	TBool iBusyShown;
	TBool iForeground;
	TBool iClosing;                // the app is being destroyed
	TBool iSaidSharing;            // "Sharing the screen" said for this start
	TInt32 iPauseCapture;          // Shift+Ctrl+W taken from every program while sharing (0: not)
	TBool iKbdMode;                // "Type on phone": the keys go to the phone
	TBool iKbdOnly;                // the engine is connected for the keyboard only: no pictures
	TUint iHeld;                   // the modifiers held for the phone (CS_MOD_CTRL, CS_MOD_GUI)
	TBool iCtrlDigits;             // digits typed with Ctrl held (sent at once: see KbdKey)
	TBool iSwallowKey;             // ...so the character code they make when Ctrl goes up is not sent
	TBool iKeysPut;                // events put in the ring since the last ring of the bell
	TUint iKeysBase;               // keys_sent when the mode started
	};

class CCsDocument : public CEikDocument
	{
public:
	CCsDocument(CEikApplication& aApp) : CEikDocument(aApp) {}
private:
	CEikAppUi* CreateAppUiL();
	};

class CCsApplication : public CEikApplication
	{
private:
	CApaDocument* CreateDocumentL();
	TUid AppDllUid() const;
	};

#endif
