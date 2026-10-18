// PsiCast's card for the emulator, loaded by core/tools/emu/mkcard.ts from
// this checkout's test/emu/ (REPO is this checkout):
//   node --experimental-strip-types core/tools/emu/mkcard.ts psicast [out.img]
export const cards = {
	psicast: (c: any) => {
		const P = c.pkg(c.REPO + 'build/cast-pkg/');
		for (const n of ['psicast.app','psicast.rsc','psicast.exe','psicast.aif','psicast.mbm'])
			c.w('SYSTEM/APPS/PSICAST/' + n.toUpperCase(), c.f(P + n));
	},
};
