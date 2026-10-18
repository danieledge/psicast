// cshelp.cpp - Tools > Help on PsiCast (Shift+Ctrl+H): the topics in an
// EIKON dialog with a topic list and a read-only text viewer, as the other
// programs' help. The wording follows the EIKON style guide: the user's
// words, keys as the keyboard prints them, commands named as on the menus.
#include "csapp.h"
#include <eikchlst.h>
#include <eikrted.h>
#include <txtrich.h>

struct TCsHelpTopic { const char* iTitle; const char* iText; };

static const TCsHelpTopic KHelpTopics[] =
	{
	{ "Getting started",
	  "PsiCast shows this Psion's screen, live, in a web browser - on an iPhone, say - "
	  "through the Atom modem. You can let the person watching tap and type on the Psion "
	  "too.\n"
	  "\n"
	  "It needs the Atom with its PsiCast firmware, connected to the Psion the way "
	  "Tools > Connection settings says (the same settings as PsiTerm, PsiMail and PsiWeb).\n"
	  "\n"
	  "PsiCast's page has three choices: Share only, Type on phone and Allow control. "
	  "Tap one to use it, or move to it with the arrow keys and press Enter. Each says "
	  "how it is now. The lines at the bottom say what to open on the phone and how "
	  "fast the screen goes.\n"
	  "\n"
	  "To share the screen for watching, tap Share only. To share it and let the "
	  "viewer tap and type too, tap Allow control (it asks first). Either one connects to "
	  "the Atom and says \"Sharing the screen\"; Shift+Ctrl+G shares the way Preferences "
	  "say. Then "
	  "go to the program you want to show: tap Back (Shift+Ctrl+Y) to go back to the one "
	  "you used last, or tap its icon.\n"
	  "\n"
	  "To stop, press Shift+Ctrl+E, or tap Share only while it is pressed in. "
	  "Tapping Allow control while it is pressed in turns control off and goes on sharing." },

	{ "Watching on the phone",
	  "On the phone, join the same Wi-Fi network as the Atom and open its address in the "
	  "browser, followed by /screen - for example http://192.168.1.40/screen. The Atom "
	  "shows its address on its own screen; PsiCast shows it too when the Atom says.\n"
	  "\n"
	  "The page asks for the Atom's web password, then shows the Psion's screen and keeps "
	  "it up to date: about ten times a second while it changes, and within about a tenth "
	  "of a second of a change after a still spell. Turn the phone sideways for a bigger "
	  "picture.\n"
	  "\n"
	  "More than one phone or computer can watch at once. PsiCast says how many are "
	  "watching." },

	{ "Pause and control",
	  "Share > Pause sharing (Shift+Ctrl+W) stops sending the screen: viewers keep the last "
	  "picture until you press it again. While the screen is shared, Shift+Ctrl+W works in "
	  "every program, so you can pause before you type a password without going back to "
	  "PsiCast (not while you type on the phone, when every key but Shift+Ctrl+B goes to "
	  "the phone, nor after PsiCast has closed with Keep sharing).\n"
	  "\n"
	  "Share > Allow control from the viewer (Shift+Ctrl+R) lets the "
	  "viewer tap on the screen and on the icons round it, and type: the Psion does what "
	  "they do as if it were you. It is off as standard. While it is off, or while sharing "
	  "is paused, PsiCast ignores the viewer's taps and keys.\n"
	  "\n"
	  "The viewer cannot switch the Psion off." },

	{ "Typing on the phone",
	  "Share > Type on phone (Shift+Ctrl+B, or Type on phone on the page) makes this Psion's keyboard "
	  "the phone's: the Atom is a Bluetooth keyboard to the phone, and every key you type "
	  "goes to it. It needs an Atom with a phone keyboard; if it has none, the command and "
	  "the choice on the page are dimmed. (PsiCast goes by what the Atom said last: after the Atom's firmware is "
	  "updated, share the screen once.) If the screen is not shared, PsiCast connects to "
	  "the Atom for the keyboard only, and sends no pictures meanwhile.\n"
	  "\n"
	  "The first time, pair the phone: on an iPhone, go to Settings > Bluetooth and tap "
	  "'PsiConnect keyboard' while PsiCast says \"Waiting for the phone\". After that the "
	  "phone connects by itself whenever you start typing, and lets go when you stop, so "
	  "its own on-screen keyboard comes back.\n"
	  "\n"
	  "Set the iPhone's keyboard layout to match the Psion's once: Settings > General > "
	  "Keyboard > Hardware Keyboard > British. Then \"@\", \"\xa3\" and the other symbols come "
	  "out as on the Psion.\n"
	  "\n"
	  "The keys: letters, numbers and symbols as on the Psion (Shift and Fn as usual). "
	  "Menu is Cmd, so Menu+Tab switches programs on the phone and Menu+C copies. Ctrl is "
	  "Control. Enter, Esc, Tab and Del are the phone's; Fn+Del (or Shift+Del) deletes "
	  "forwards; Fn and the arrow keys are Home, End, Page up and Page down.\n"
	  "\n"
	  "To stop, press Shift+Ctrl+B (the one key that does not go to the phone), or tap "
	  "Stop typing on the screen. Typing stops by itself if you go "
	  "to another program." },

	{ "Preferences",
	  "Tools > Preferences (Ctrl+K, or Preferences on the toolbar):\n"
	  "\n"
	  "Highest update rate: how often PsiCast may send the screen while it changes. Fewer "
	  "updates save the batteries; every change still shows, a little later.\n"
	  "\n"
	  "Allow control from the viewer: the same as Share > Allow control from the viewer.\n"
	  "\n"
	  "Stop sharing when the battery is low: as standard PsiCast stops when the Psion says "
	  "its batteries are low, unless it is on mains power.\n"
	  "\n"
	  "When PsiCast closes: Stop sharing, or Keep sharing - the screen goes on being "
	  "shared after PsiCast has closed, until you open PsiCast again and stop it." },

	{ "Connection and power",
	  "PsiCast connects to the Atom's own address, the way Tools > Connection settings "
	  "says: through the modem (the serial port is then PsiCast's while it shares), or "
	  "through Psion Internet, which other programs can use at the same time.\n"
	  "\n"
	  "If the Atom does not answer, PsiCast tries again by itself, less often as time goes "
	  "on; the line at the bottom of its screen says why.\n"
	  "\n"
	  "Sharing uses the batteries faster than usual. PsiCast reads the screen only while "
	  "someone is watching and the Psion is on, and slows down when the screen is still. "
	  "When the Psion is switched off, sharing waits, and goes on when it is switched on "
	  "again." },
	};
const TInt KHelpCount = sizeof(KHelpTopics) / sizeof(KHelpTopics[0]);

void CCsHelpDialog::PreLayoutDynInitL()
	{
	CDesCArrayFlat* topics = new(ELeave) CDesCArrayFlat(KHelpCount);
	CleanupStack::PushL(topics);
	for (TInt i = 0; i < KHelpCount; i++)
		{
		TBuf<40> t;
		t.Copy(TPtrC8((const TUint8*)KHelpTopics[i].iTitle));
		topics->AppendL(t);
		}
	CEikChoiceList* list = (CEikChoiceList*)Control(ECsDlgHelpTopic);
	list->SetArrayL(topics);
	CleanupStack::Pop();                    // topics (the list owns it)
	list->SetCurrentItem(0);
	iTopic = 0;
	ShowTopicL(0);
	}

void CCsHelpDialog::ShowTopicL(TInt aTopic)
	{
	const TCsHelpTopic& topic = KHelpTopics[aTopic];
	TPtrC8 title((const TUint8*)topic.iTitle);
	TPtrC8 text((const TUint8*)topic.iText);
	HBufC* b = HBufC::NewLC(title.Length() + text.Length() + 4);
	TPtr p = b->Des();
	p.Copy(title);
	p.Append(CEditableText::EParagraphDelimiter);
	for (TInt i = 0; i < text.Length(); i++)
		{
		TUint8 c = text[i];
		if (c == '\n')
			p.Append(CEditableText::EParagraphDelimiter);
		else
			p.Append((TText)c);
		}
	CEikRichTextEditor* ed = (CEikRichTextEditor*)Control(ECsDlgHelpText);
	ed->SetTextL(b);
	TCharFormat cf;
	TCharFormatMask cm;
	cf.iFontSpec.iFontStyle.SetStrokeWeight(EStrokeWeightBold);
	cm.SetAttrib(EAttFontStrokeWeight);
	ed->RichText()->ApplyCharFormatL(cf, cm, 0, title.Length());
	ed->HandleTextChangedL();
	ed->SetCursorPosL(0, EFalse);
	CleanupStack::PopAndDestroy();          // b
	}

// the keys start on the topic list (Left and Right change the topic; Down
// moves into the text, where Up and Down scroll it)
void CCsHelpDialog::PostLayoutDynInitL()
	{
	TryChangeFocusToL(ECsDlgHelpTopic);
	}

void CCsHelpDialog::HandleControlStateChangeL(TInt aControlId)
	{
	if (aControlId != ECsDlgHelpTopic)
		return;
	TInt t = ((CEikChoiceList*)Control(ECsDlgHelpTopic))->CurrentItem();
	if (t != iTopic && t >= 0 && t < KHelpCount)
		{
		iTopic = t;
		ShowTopicL(t);
		}
	}
