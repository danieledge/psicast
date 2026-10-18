// PsiCast's card for the emulator, loaded by tools/emu/mkcard.ts from
// tools/emu/local/ (a link to this file):
//   node --experimental-strip-types tools/emu/mkcard.ts psicast [out.img]
export const cards = {
	psicast: (c: any) => {
		const P = c.pkg(c.REPO + 'build/cast-pkg/');
		for (const n of ['psicast.app','psicast.rsc','psicast.exe','psicast.aif','psicast.mbm'])
			c.w('SYSTEM/APPS/PSICAST/' + n.toUpperCase(), c.f(P + n));
	},
};
