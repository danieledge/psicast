# Host tests for PsiCast on the Atom (the extension module in ../src), on the
# firmware's own host-test harness (psiconnect's hosttest/; the checkout is
# looked for beside this one, or in $PSICONNECT). MIT licence.
#   make test       test_cast (with the sanitizers): link A's framing and
#                   session against a Psion played with the real encoder,
#                   tokens, WebSocket, per-viewer merging (pixel-exact), the
#                   input path, the modem route, the firmware's hooks and
#                   the phone keyboard (its tables, KEYS, KBD, a fake phone);
#                   test_iconmatch.js and test_roles.js (node: the viewer's
#                   pictures and role pass on real screens) and the Modern
#                   packs' 5mx sides
#   make e2e        test_cast_e2e.py: hostmodem with the module in
#                   (HOSTMODEM_CAST=1), a fake Psion on both routes, WebSocket
#                   viewers
#   make shots      cast_page_shot.py: the real viewer page in Chromium
#                   (Playwright), screenshots in screens/
#   make hostmodem  the firmware's hostmodem with PsiCast in (casthost.cpp)
#   make screens    the LCD's Sharing page drawn by the firmware's host
#                   renderer (castshots.cpp), as PNGs in screens/ at the
#                   LCD's size and 4x (OUT=folder for another; the fonts come
#                   from M5GFX: `pio run -e atoms3r` in the firmware once)
CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -O1 -g -Wall -Wextra -Wno-unused-parameter -std=c++17
FW ?= $(if $(PSICONNECT),$(PSICONNECT),$(abspath ../../../psiconnect))
# (the picture decoders from this checkout's own psion-core)
FW_IMG = ../../core/mail/engine/img
include $(FW)/hosttest/sources.mk
CODEC = ../../codec
MOD = ../src
MOD_SRC = $(MOD)/cast.cpp $(MOD)/websock.cpp $(MOD)/castnet.cpp $(MOD)/castext.cpp $(MOD)/castlcd.cpp \
          $(MOD)/kbd.cpp $(MOD)/kbdmap.cpp $(MOD)/kbdext.cpp $(MOD)/kbdble.cpp $(MOD)/kbdusb.cpp
MOD_HDR = $(MOD)/cast.h $(MOD)/websock.h $(MOD)/castnet.h $(MOD)/castext.h $(MOD)/castlcd.h $(MOD)/castpage.h $(MOD)/casticons.h \
          $(MOD)/kbd.h $(MOD)/kbdmap.h $(MOD)/kbdext.h $(MOD)/kbdlinks.h \
          $(wildcard $(MOD)/casticons_local.h) $(CODEC)/pccodec.h
SRC = $(FW_SRC) $(MOD_SRC)
HDR = $(FW_HDR) $(MOD_HDR)
# (the screen codec, both halves: the tests play the Psion too)
IMGOBJ = $(FW_IMGOBJ) pccodec.o
INC = $(FW_INC) -I$(FW)/hosttest -I$(MOD) -I$(CODEC)

test: test_cast
	./test_cast
	if command -v node >/dev/null; then node test_iconmatch.js && node test_roles.js; else echo 'test_iconmatch, test_roles: SKIP (no node)'; fi
	# the Modern packs' 5mx sides against the built pictures (those built here)
	python3 ../tools/modicons.py --check
	if [ -f ../tools/modicons/specs_local.py ]; then python3 ../tools/modicons.py --spec ../tools/modicons/specs_local.py --check; fi

pccodec.o: $(CODEC)/pccodec.c $(CODEC)/pccodec.h
	$(CC) -O1 -g -Wall -fno-strict-aliasing -I$(CODEC) -c -o $@ $<

test_cast: test_cast.cpp $(FW)/hosttest/fakehal.h $(SRC) $(HDR) $(IMGOBJ)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined $(INC) -o $@ test_cast.cpp $(SRC) $(IMGOBJ) -lz

# TLS for the proxy comes from the PC's OpenSSL 3 (libssl.so.3), loaded at
# run time, so no OpenSSL headers are needed to build
hostmodem: $(FW)/hosttest/hostmodem.cpp casthost.cpp $(SRC) $(HDR) $(IMGOBJ)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $(FW)/hosttest/hostmodem.cpp casthost.cpp $(SRC) $(IMGOBJ) -lz -ldl

# the codec as a shared library, for the end-to-end test's fake Psion (ctypes)
libpccodec.so: $(CODEC)/pccodec.c $(CODEC)/pccodec.h
	$(CC) -O2 -fPIC -shared -fno-strict-aliasing -I$(CODEC) -o $@ $< -lz

e2e: hostmodem libpccodec.so
	python3 test_cast_e2e.py

shots: hostmodem libpccodec.so
	python3 cast_page_shot.py

M5FONTS = $(FW)/.pio/libdeps/atoms3r/M5GFX/src/lgfx/Fonts
OUT ?= screens
render_screens: $(FW)/hosttest/render_screens.cpp $(FW)/hosttest/shots.h castshots.cpp $(SRC) $(HDR) $(IMGOBJ)
	$(CXX) $(CXXFLAGS) $(INC) -I$(M5FONTS) -Wno-narrowing -o $@ $(FW)/hosttest/render_screens.cpp castshots.cpp $(SRC) $(IMGOBJ) -lz
screens: render_screens
	rm -rf $(OUT)/.all && ./render_screens $(OUT)/.all > /dev/null
	python3 shareshots.py $(OUT)/.all $(OUT)

clean:
	rm -f test_cast hostmodem render_screens libpccodec.so $(IMGOBJ)
	rm -rf screens

.PHONY: test e2e shots screens clean
