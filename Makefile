# /MD link with MSVCRT.LIB                 /MTd link with LIBCMTD.LIB debug library
# /MDd link with MSVCRTD.LIB debug library /F<num> set stack size
# /ML link with LIBC.LIB                   /LD Create .DLL
# /MLd link with LIBCD.LIB debug library   /LDd Create .DLL debug libary
# /MT link with LIBCMT.LIB                 /link [linker options and libraries]

# /Zi generate debugging information
# /Z7 generate old-style debug info
# /Zd line number debugging info only

# /GF enable read-only string pooling     /Gy[-] separate functions for linker



CC = cl
INCLUDES = /I. /Iplatform
DEFINES = /DSTATIC_INLINE="static _inline" /Dinline=_inline
OPT = /O /Gy
DEBUG = /Zi
CLIB = /MDd
CFLAGS = $(INCLUDES) $(DEFINES) $(OPT) $(DEBUG)
GDIR = game
IDIR = int
PDIR = plib

GOBJ = $(GDIR)\ability.obj $(GDIR)\actions.obj $(GDIR)\amutex.obj $(GDIR)\anim.obj $(GDIR)\art.obj \
	$(GDIR)\artload.obj $(GDIR)\automap.obj $(GDIR)\bmpdlog.obj $(GDIR)\cache.obj $(GDIR)\combat.obj \
	$(GDIR)\combatai.obj $(GDIR)\config.obj $(GDIR)\counter.obj $(GDIR)\credits.obj $(GDIR)\critter.obj \
	$(GDIR)\cycle.obj $(GDIR)\diskspce.obj $(GDIR)\display.obj $(GDIR)\editor.obj $(GDIR)\elevator.obj \
	$(GDIR)\endgame.obj $(GDIR)\ereg.obj $(GDIR)\fontmgr.obj $(GDIR)\game.obj $(GDIR)\gconfig.obj \
	$(GDIR)\gdebug.obj $(GDIR)\gdialog.obj $(GDIR)\gmemory.obj $(GDIR)\gmouse.obj $(GDIR)\gmovie.obj \
	$(GDIR)\graphlib.obj $(GDIR)\gsound.obj $(GDIR)\heap.obj $(GDIR)\intface.obj $(GDIR)\inventry.obj \
	$(GDIR)\item.obj $(GDIR)\light.obj $(GDIR)\lip_sync.obj $(GDIR)\loadsave.obj $(GDIR)\main.obj \
	$(GDIR)\mainmenu.obj $(GDIR)\map.obj $(GDIR)\message.obj $(GDIR)\moviefx.obj $(GDIR)\object.obj \
	$(GDIR)\options.obj $(GDIR)\palette.obj $(GDIR)\party.obj $(GDIR)\perk.obj $(GDIR)\pipboy.obj \
	$(GDIR)\protinst.obj $(GDIR)\proto.obj $(GDIR)\queue.obj $(GDIR)\reaction.obj $(GDIR)\roll.obj \
	$(GDIR)\scripts.obj $(GDIR)\select.obj $(GDIR)\selfrun.obj $(GDIR)\sfxcache.obj $(GDIR)\sfxlist.obj \
	$(GDIR)\skill.obj $(GDIR)\skilldex.obj $(GDIR)\stat.obj $(GDIR)\textobj.obj $(GDIR)\tile.obj \
	$(GDIR)\trait.obj $(GDIR)\trap.obj $(GDIR)\version.obj $(GDIR)\wordwrap.obj $(GDIR)\worldmap.obj \
	$(GDIR)\worldmap_walkmask.obj 

TOBJ = 	missing.obj movie_lib.obj sound_decoder.obj

IOBJ = $(IDIR)\audio.obj $(IDIR)\audiof.obj $(IDIR)\datafile.obj $(IDIR)\dialog.obj $(IDIR)\export.obj $(IDIR)\intlib.obj \
	$(IDIR)\intrpret.obj $(IDIR)\memdbg.obj $(IDIR)\mousemgr.obj $(IDIR)\movie.obj $(IDIR)\nevs.obj $(IDIR)\pcx.obj \
	$(IDIR)\region.obj $(IDIR)\share1.obj $(IDIR)\sound.obj $(IDIR)\widget.obj $(IDIR)\window.obj $(IDIR)\support\intextra.obj

POBJ = $(PDIR)\assoc\assoc.obj $(PDIR)\color\color.obj $(PDIR)\db\db.obj $(PDIR)\db\lzss.obj $(PDIR)\gnw\button.obj $(PDIR)\gnw\debug.obj \
	$(PDIR)\gnw\doscmdln.obj $(PDIR)\gnw\dxinput.obj $(PDIR)\gnw\gnw.obj $(PDIR)\gnw\gnw95dx.obj $(PDIR)\gnw\grbuf.obj \
	$(PDIR)\gnw\input.obj  $(PDIR)\gnw\intrface.obj $(PDIR)\gnw\kb.obj $(PDIR)\gnw\memory.obj $(PDIR)\gnw\mmx.obj $(PDIR)\gnw\mouse.obj \
	$(PDIR)\gnw\rect.obj $(PDIR)\gnw\svga.obj $(PDIR)\gnw\text.obj $(PDIR)\gnw\vcr.obj $(PDIR)\gnw\winmain.obj

.c.obj:
	$(CC) /c $(CFLAGS) $*.c /Fo$*.obj


fallout1.exe : $(GOBJ) $(TOBJ) $(IOBJ) $(POBJ)
	cl $(CLIB) $(DEBUG) $(GOBJ) $(TOBJ) $(IOBJ) $(POBJ) /Fefallout1.exe winmm.lib kernel32.lib user32.lib

clean:
	@del $(GOBJ)
	@del $(TOBJ)
	@del $(IOBJ)
	@del $(POBJ)
	@del fallout1.ncb vc*.pdb fallout1.pdb fallout1.ilk
	del fallout1.exe

