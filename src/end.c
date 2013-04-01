/*	SCCS Id: @(#)end.c	3.4	2003/03/10	*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

#define NEED_VARARGS	/* comment line for pre-compiled headers */

#include "hack.h"
#include "eshk.h"
#ifndef NO_SIGNAL
#include <signal.h>
#endif
#include "dlb.h"

	/* these probably ought to be generated by makedefs, like LAST_GEM */
#define FIRST_GEM    DILITHIUM_CRYSTAL
#define FIRST_AMULET AMULET_OF_ESP
#define LAST_AMULET  AMULET_OF_YENDOR
 
struct valuable_data { long count; int typ; };

static struct valuable_data
	gems[LAST_GEM+1 - FIRST_GEM + 1], /* 1 extra for glass */
	amulets[LAST_AMULET+1 - FIRST_AMULET];

static struct val_list { struct valuable_data *list; int size; } valuables[] = {
	{ gems,    sizeof gems / sizeof *gems },
	{ amulets, sizeof amulets / sizeof *amulets },
	{ 0, 0 }
};

#ifndef NO_SIGNAL
STATIC_PTR void FDECL(done_intr, (int));
# if defined(UNIX) || defined(VMS) || defined (__EMX__)
static void FDECL(done_hangup, (int));
# endif
#endif
STATIC_DCL void FDECL(disclose,(int,BOOLEAN_P));
STATIC_DCL void FDECL(get_valuables, (struct obj *));
STATIC_DCL void FDECL(sort_valuables, (struct valuable_data *,int));
STATIC_DCL void FDECL(artifact_score, (struct obj *,BOOLEAN_P,winid));
STATIC_DCL void FDECL(savelife, (int));
void FDECL(list_vanquished, (CHAR_P,BOOLEAN_P));
#ifdef DUMP_LOG
extern char msgs[][BUFSZ];
extern int msgs_count[];
extern int lastmsg;
void FDECL(do_vanquished, (int, BOOLEAN_P));
#endif /* DUMP_LOG */
STATIC_DCL void FDECL(list_genocided, (int, BOOLEAN_P, BOOLEAN_P));
STATIC_DCL boolean FDECL(should_query_disclose_option, (int,char *));

#if defined(__BEOS__) || defined(MICRO) || defined(WIN32) || defined(OS2)
extern void FDECL(nethack_exit,(int));
#else
#define nethack_exit exit
#endif

#define done_stopprint program_state.stopprint

#ifdef AMIGA
# define NH_abort()	Abort(0)
#else
# ifdef SYSV
# define NH_abort()	(void) abort()
# else
#  ifdef WIN32
# define NH_abort()	win32_abort()
#  else
# define NH_abort()	abort()
#  endif
# endif
#endif

/*
 * The order of these needs to match the macros in hack.h.
 */
static NEARDATA const char *deaths[] = {		/* the array of death */
	"died", "choked", "poisoned", "starvation", "drowning",
	"burning", "dissolving under the heat and pressure",
	"crushed", "turned to stone", "turned into slime",
	"genocided", 
	"disintegrated",
	"panic", "trickery",
#ifdef ASTRAL_ESCAPE
	"quit", "escaped", "defied the gods and escaped", "ascended"
#endif
};

static NEARDATA const char *ends[] = {		/* "when you..." */
	"died", "choked", "were poisoned", "starved", "drowned",
	"burned", "dissolved in the lava",
	"were crushed", "turned to stone", "turned into slime",
	"were genocided", 
	"were disintegrated",
	"panicked", "were tricked",
#ifdef ASTRAL_ESCAPE
	"quit", "escaped", "defied the gods and escaped", "ascended"
#endif
};

extern const char * const killed_by_prefix[];	/* from topten.c */

/*ARGSUSED*/
void
done1(sig_unused)   /* called as signal() handler, so sent at least one arg */
int sig_unused;
{
#ifndef NO_SIGNAL
	(void) signal(SIGINT,SIG_IGN);
#endif
	if(flags.ignintr) {
#ifndef NO_SIGNAL
		(void) signal(SIGINT, (SIG_RET_TYPE) done1);
#endif
		clear_nhwindow(WIN_MESSAGE);
		curs_on_u();
		wait_synch();
		if(multi > 0) nomul(0, 0);
	} else {
		(void)done2();
	}
}


/* "#quit" command or keyboard interrupt */
int
done2()
{
	if (paranoid_yn("Really quit?", iflags.paranoid_quit) == 'n') {
#ifndef NO_SIGNAL
		(void) signal(SIGINT, (SIG_RET_TYPE) done1);
#endif
		clear_nhwindow(WIN_MESSAGE);
		curs_on_u();
		wait_synch();
		if(multi > 0) nomul(0, 0);
		if(multi == 0) {
		    u.uinvulnerable = FALSE;	/* avoid ctrl-C bug -dlc */
		    u.usleep = 0;
		}
		return 0;
	}
#if defined(WIZARD) && (defined(UNIX) || defined(VMS) || defined(LATTICE))
	if(wizard) {
	    int c;
# ifdef VMS
	    const char *tmp = "Enter debugger?";
# else
#  ifdef LATTICE
	    const char *tmp = "Create SnapShot?";
#  else
	    const char *tmp = "Dump core?";
#  endif
# endif
	    if ((c = ynq(tmp)) == 'y') {
		(void) signal(SIGINT, (SIG_RET_TYPE) done1);
		exit_nhwindows((char *)0);
		NH_abort();
	    } else if (c == 'q') done_stopprint++;
	}
#endif
	killer = 0;
	done(QUIT);
	return 0;
}

#ifndef NO_SIGNAL
/*ARGSUSED*/
STATIC_PTR void
done_intr(sig_unused) /* called as signal() handler, so sent at least one arg */
int sig_unused;
{
	done_stopprint++;
	(void) signal(SIGINT, SIG_IGN);
# if defined(UNIX) || defined(VMS)
	(void) signal(SIGQUIT, SIG_IGN);
# endif
	return;
}

# if defined(UNIX) || defined(VMS) || defined(__EMX__)
static void
done_hangup(sig)	/* signal() handler */
int sig;
{
	program_state.done_hup++;
	(void)signal(SIGHUP, SIG_IGN);
	done_intr(sig);
	return;
}
# endif
#endif /* NO_SIGNAL */

void
done_in_by(mtmp)
struct monst *mtmp;
{
	char buf[BUFSZ];
	boolean distorted = (boolean)(Hallucination && canspotmon(mtmp));

	You("die...");
	mark_synch();	/* flush buffered screen output */
	buf[0] = '\0';
	killer_format = KILLED_BY_AN;
	/* "killed by the high priest of Crom" is okay, "killed by the high
	   priest" alone isn't */
	if ((mtmp->data->geno & G_UNIQ) != 0 && !(mtmp->data == &mons[PM_HIGH_PRIEST] && !mtmp->ispriest)) {
	    if (!type_is_pname(mtmp->data))
		Strcat(buf, "the ");
	    killer_format = KILLED_BY;
	}
	/* _the_ <invisible> <distorted> ghost of Dudley */
	if (mtmp->data == &mons[PM_GHOST] && mtmp->mnamelth) {
		Strcat(buf, "the ");
		killer_format = KILLED_BY;
	}
	if (mtmp->minvis)
		Strcat(buf, "invisible ");
	if (distorted)
		Strcat(buf, "hallucinogen-distorted ");

	if(mtmp->data == &mons[PM_GHOST]) {
		Strcat(buf, "ghost");
		if (mtmp->mnamelth) Sprintf(eos(buf), " of %s", NAME(mtmp));
	} else if(mtmp->isshk) {
		Sprintf(eos(buf), "%s %s, the shopkeeper",
			(mtmp->female ? "Ms." : "Mr."), shkname(mtmp));
		killer_format = KILLED_BY;
	} else if (mtmp->ispriest || mtmp->isminion) {
		/* m_monnam() suppresses "the" prefix plus "invisible", and
		   it overrides the effect of Hallucination on priestname() */
		killer = m_monnam(mtmp);
		Strcat(buf, killer);
	} else {
		Strcat(buf, mtmp->data->mname);
		if (mtmp->mnamelth)
		    Sprintf(eos(buf), " called %s", NAME(mtmp));
	}

	if (multi) {
	  if (strlen(multi_txt) > 0)
	    Sprintf(eos(buf), ", while %s", multi_txt);
	  else
	    Strcat(buf, ", while helpless");
	}
	killer = buf;
	if (mtmp->data->mlet == S_WRAITH)
		u.ugrave_arise = PM_WRAITH;
	else if (mtmp->data->mlet == S_MUMMY && urace.mummynum != NON_PM)
		u.ugrave_arise = urace.mummynum;
	else if (is_vampire(mtmp->data) && Race_if(PM_HUMAN))
		u.ugrave_arise = PM_VAMPIRE;
	else if (mtmp->data == &mons[PM_GHOUL])
		u.ugrave_arise = PM_GHOUL;
	if (u.ugrave_arise >= LOW_PM &&
				(mvitals[u.ugrave_arise].mvflags & G_GENOD))
		u.ugrave_arise = NON_PM;
	if (touch_petrifies(mtmp->data))
		done(STONING);
	else
		done(DIED);
	return;
}

#if defined(WIN32)
#define NOTIFY_NETHACK_BUGS
#endif

/*VARARGS1*/
void
panic VA_DECL(const char *, str)
	VA_START(str);
	VA_INIT(str, char *);

	if (program_state.panicking++)
	    NH_abort();	/* avoid loops - this should never happen*/

	if (iflags.window_inited) {
	    raw_print("\r\nOops...");
	    wait_synch();	/* make sure all pending output gets flushed */
	    exit_nhwindows((char *)0);
	    iflags.window_inited = 0; /* they're gone; force raw_print()ing */
	}

	raw_print(program_state.gameover ?
		  "Postgame wrapup disrupted." :
		  !program_state.something_worth_saving ?
		  "Program initialization has failed." :
		  "Suddenly, the dungeon collapses.");
#if defined(WIZARD) && !defined(MICRO)
# if defined(NOTIFY_NETHACK_BUGS)
	if (!wizard)
	    raw_printf("Report the following error to \"%s\".",
			"bulwersator@gmail.com");
	else if (program_state.something_worth_saving)
	    raw_print("\nError save file being written.\n");
# else
	if (!wizard)
	    raw_printf("Report error to \"%s\"%s.",
#  ifdef WIZARD_NAME	/*(KR1ED)*/
			WIZARD_NAME,
#  else
			WIZARD,
#  endif
			!program_state.something_worth_saving ? "" :
			" and it may be possible to rebuild.");
# endif
	if (program_state.something_worth_saving) {
	    set_error_savefile();
	    (void) dosave0();
	}
#endif
	{
	    char buf[BUFSZ];
	    Vsprintf(buf,str,VA_ARGS);
	    raw_print(buf);
	    paniclog("panic", buf);
	}
#ifdef WIN32
	interject(INTERJECT_PANIC);
#endif
#ifdef LIVELOGFILE
	livelog_game_action("panicked");
#endif
#if defined(WIZARD) && (defined(UNIX) || defined(VMS) || defined(LATTICE) || defined(WIN32))
	if (wizard)
	    NH_abort();	/* generate core dump */
#endif
	VA_END();
	done(PANICKED);
}

STATIC_OVL boolean
should_query_disclose_option(category, defquery)
int category;
char *defquery;
{
    int idx;
    char *dop = index(disclosure_options, category);

    if (dop && defquery) {
	idx = dop - disclosure_options;
	if (idx < 0 || idx > (NUM_DISCLOSURE_OPTIONS - 1)) {
	    impossible(
		   "should_query_disclose_option: bad disclosure index %d %c",
		       idx, category);
	    *defquery = DISCLOSE_PROMPT_DEFAULT_YES;
	    return TRUE;
	}
	if (flags.end_disclose[idx] == DISCLOSE_YES_WITHOUT_PROMPT) {
	    *defquery = 'y';
	    return FALSE;
	} else if (flags.end_disclose[idx] == DISCLOSE_NO_WITHOUT_PROMPT) {
	    *defquery = 'n';
	    return FALSE;
	} else if (flags.end_disclose[idx] == DISCLOSE_PROMPT_DEFAULT_YES) {
	    *defquery = 'y';
	    return TRUE;
	} else if (flags.end_disclose[idx] == DISCLOSE_PROMPT_DEFAULT_NO) {
	    *defquery = 'n';
	    return TRUE;
	}
    }
    if (defquery)
	impossible("should_query_disclose_option: bad category %c", category);
    else
	impossible("should_query_disclose_option: null defquery");
    return TRUE;
}

STATIC_OVL void
disclose(how,taken)
int how;
boolean taken;
{
	char	c = 0, defquery;
	char	qbuf[QBUFSZ];
	boolean ask;

	if (invent) {
	    if(taken)
		Sprintf(qbuf,"Do you want to see what you had when you %s?",
			(how == QUIT) ? "quit" : "died");
	    else
		Strcpy(qbuf,"Do you want your possessions identified?");

	    ask = should_query_disclose_option('i', &defquery);
	    if (!done_stopprint) {
		c = ask ? yn_function(qbuf, ynqchars, defquery) : defquery;
	    } else {
		c = 'n';
	    }
	    {
			boolean want_disp = (c == 'y') ? TRUE: FALSE;

			(void) dump_inventory((char *)0, TRUE, want_disp);
			container_contents(invent, TRUE, TRUE, want_disp);
	    }
		if (c == 'q')  done_stopprint++;
	}

	ask = should_query_disclose_option('a', &defquery);
	if (!done_stopprint) {
	    c = ask ? yn_function("Do you want to see your attributes?",
				  ynqchars, defquery) : defquery;
	    if (c == 'q') done_stopprint++;
	}
	enlightenment(how >= PANICKED ? 1 : 2, (c == 'y')); /* final */

	dump_spells();

	ask = should_query_disclose_option('v', &defquery);
#ifdef DUMP_LOG
	do_vanquished(defquery, ask);
#else
	if (!done_stopprint)
	    list_vanquished(defquery, ask);
#endif

	ask = should_query_disclose_option('g', &defquery);
	list_genocided(defquery, ask, !done_stopprint);

	ask = should_query_disclose_option('c', &defquery);
	if (!done_stopprint) {
	    c = ask ? yn_function("Do you want to see your conduct?",
				  ynqchars, defquery) : defquery;
	}
	show_conduct(how >= PANICKED ? 1 : 2, (c == 'y' && !done_stopprint));
	if (c == 'q') done_stopprint++;

	dump_weapon_skill();
}

/* try to get the player back in a viable state after being killed */
STATIC_OVL void
savelife(how)
int how;
{
	u.uswldtim = 0;
	u.uhp = u.uhpmax;
	if (u.uhunger < 500) {
	    u.uhunger = 500;
	    newuhs(FALSE);
	}
	/* cure impending doom of sickness hero won't have time to fix */
	if ((Sick & TIMEOUT) == 1) {
	    u.usick_type = 0;
	    Sick = 0;
	}
	if (how == CHOKING) init_uhunger();
	nomovemsg = "You survived that attempt on your life.";
	flags.move = 0;
	if(multi > 0) multi = 0; else multi = -1;
	if(u.utrap && u.utraptype == TT_LAVA) u.utrap = 0;
	flags.botl = 1;
	u.ugrave_arise = NON_PM;
	HUnchanging = 0L;
	curs_on_u();
}

/*
 * Get valuables from the given list.  Revised code: the list always remains
 * intact.
 */
STATIC_OVL void
get_valuables(list)
struct obj *list;	/* inventory or container contents */
{
    struct obj *obj;
    int i;

    /* find amulets and gems, ignoring all artifacts */
    for (obj = list; obj; obj = obj->nobj)
	if (Has_contents(obj)) {
	    get_valuables(obj->cobj);
	} else if (obj->oartifact) {
	    continue;
	} else if (obj->oclass == AMULET_CLASS) {
	    i = obj->otyp - FIRST_AMULET;
	    if (!amulets[i].count) {
		amulets[i].count = obj->quan;
		amulets[i].typ = obj->otyp;
	    } else amulets[i].count += obj->quan; /* always adds one */
	} else if (obj->oclass == GEM_CLASS && obj->otyp < LUCKSTONE) {
	    i = min(obj->otyp, LAST_GEM + 1) - FIRST_GEM;
	    if (!gems[i].count) {
		gems[i].count = obj->quan;
		gems[i].typ = obj->otyp;
	    } else gems[i].count += obj->quan;
	}
    return;
}

/*
 *  Sort collected valuables, most frequent to least.  We could just
 *  as easily use qsort, but we don't care about efficiency here.
 */
STATIC_OVL void
sort_valuables(list, size)
struct valuable_data list[];
int size;		/* max value is less than 20 */
{
    int i, j;
    struct valuable_data ltmp;

    /* move greater quantities to the front of the list */
    for (i = 1; i < size; i++) {
	if (list[i].count == 0) continue;	/* empty slot */
	ltmp = list[i]; /* structure copy */
	for (j = i; j > 0; --j)
	    if (list[j-1].count >= ltmp.count) break;
	    else {
		list[j] = list[j-1];
	    }
	list[j] = ltmp;
    }
    return;
}

/* called twice; first to calculate total, then to list relevant items */
STATIC_OVL void
artifact_score(list, counting, endwin)
struct obj *list;
boolean counting;	/* true => add up points; false => display them */
winid endwin;
{
    char pbuf[BUFSZ];
    struct obj *otmp;
    long value, points;
    short dummy;	/* object type returned by artifact_name() */

    for (otmp = list; otmp; otmp = otmp->nobj) {
	if (otmp->oartifact ||
			otmp->otyp == BELL_OF_OPENING ||
			otmp->otyp == SPE_BOOK_OF_THE_DEAD ||
			otmp->otyp == CANDELABRUM_OF_INVOCATION) {
	    value = arti_cost(otmp);	/* zorkmid value */
	    points = value * 5 / 2;	/* score value */
	    if (counting) {
		u.urscore += points;
	    } else {
		makeknown(otmp->otyp);
		otmp->known = otmp->dknown = otmp->bknown = otmp->rknown = 1;
		/* assumes artifacts don't have quan > 1 */
		Sprintf(pbuf, "%s%s (worth %ld %s and %ld points),",
			the_unique_obj(otmp) ? "The " : "",
			otmp->oartifact ? artifact_name(xname(otmp), &dummy) :
				OBJ_NAME(objects[otmp->otyp]),
			value, currency(value), points);
#ifdef DUMP_LOG
		dump_line("", pbuf);
		if (endwin != WIN_ERR)
#endif
		putstr(endwin, 0, pbuf);
	    }
	}
	if (Has_contents(otmp))
	    artifact_score(otmp->cobj, counting, endwin);
    }
}

/* Be careful not to call panic from here! */
void
done(how)
int how;
{
	boolean taken;
	char kilbuf[BUFSZ], pbuf[BUFSZ];
	winid endwin = WIN_ERR;
	boolean bones_ok, have_windows = iflags.window_inited;
	struct obj *corpse = (struct obj *)0;
	long umoney;

	if (how == TRICKED) {
	    if (killer) {
		paniclog("trickery", killer);
		killer = 0;
	    }
#ifdef WIZARD
	    if (wizard) {
		You("are a very tricky wizard, it seems.");
		return;
	    }
#endif
	}

	/* kilbuf: used to copy killer in case it comes from something like
	 *	xname(), which would otherwise get overwritten when we call
	 *	xname() when listing possessions
	 * pbuf: holds Sprintf'd output for raw_print and putstr
	 */
	if (how == ASCENDED || (!killer && how == GENOCIDED))
		killer_format = NO_KILLER_PREFIX;
	/* Avoid killed by "a" burning or "a" starvation */
	if (!killer && (how == STARVING || how == BURNING))
		killer_format = KILLED_BY;
	/* Ignore some killer-strings, but use them for QUIT and ASCENDED */
	Strcpy(kilbuf, ((how == PANICKED) || (how == TRICKED) || (how == ESCAPED)
				|| !killer ? deaths[how] : killer));
	killer = kilbuf;

	if (how < PANICKED) u.umortality++;
	if (Lifesaved && (how <= MAX_SURVIVABLE_DEATH)) {
		pline("But wait...");
		makeknown(AMULET_OF_LIFE_SAVING);
		Your("medallion %s!",
		      !Blind ? "begins to glow" : "feels warm");
		/* Keep it blessed! */
		if (uamul && uamul->cursed && rnf(1,4)) {
			pline("But ... the chain on your medallion breaks and it falls to the %s!", surface(u.ux,u.uy));
			You_hear("homeric laughter!"); /* Hah ha! */
			/* It already started to work. Too bad you couldn't hold onto it. */
			useup(uamul);
		} else {
			if (how == CHOKING) You("vomit ...");
			if (how == DISINTEGRATED) You("reconstitute!");
			else You_feel("much better!");
			pline_The("medallion crumbles to dust!");
			if (uamul) useup(uamul);

			(void) adjattrib(A_CON, -1, TRUE);
			if(u.uhpmax <= 0) u.uhpmax = 10;	/* arbitrary */
			savelife(how);
			if ((how == GENOCIDED) && is_playermon_genocided())
				pline("Unfortunately you are still genocided...");
			else {
				killer = 0;
				killer_format = 0;
				return;
			}
		}
	}
	if ((
#ifdef WIZARD
			wizard ||
#endif
			discover) && (how <= MAX_SURVIVABLE_DEATH)) {
		if(yn("Die?") == 'y') goto die;
		pline("OK, so you don't %s.",
			(how == CHOKING) ? "choke" : (how == DISINTEGRATED) ? "disintegrate" : "die");
		if(u.uhpmax <= u.ulevel * 8) u.uhpmax = u.ulevel * 8;	/* arbitrary */
		savelife(how);
		killer = 0;
		killer_format = 0;
		return;
	}

    /*
     *	The game is now over...
     */

die:
	program_state.gameover = 1;
	/* in case of a subsequent panic(), there's no point trying to save */
	program_state.something_worth_saving = 0;

	/* record time of death */
#if defined(BSD) && !defined(POSIX_TYPES)
	(void) time((long *)&u.udeathday);
#else
	(void) time(&u.udeathday);
#endif


#ifdef DUMP_LOG
	/* D: Grab screen dump right here */
	if (dump_fn[0]) {
	  dump_init();
	  Sprintf(pbuf, "%s, %s %s %s %s", plname,
		  aligns[1 - u.ualign.type].adj,
		  genders[flags.female].adj,
		  urace.adj,
		  (flags.female && urole.name.f)?
		   urole.name.f : urole.name.m);
	  dump_header_html(pbuf);
	  dump("", pbuf);
	  /* D: Add a line for clearance from the screen dump */
	  dump("", "");
	  dump_screen();
	}
# ifdef DUMPMSGS
	if (lastmsg >= 0) {
		char tmpbuf[BUFSZ];
		int i,j;
		dump_title("Latest messages");
		dump_blockquote_start();
		for (j = lastmsg + 1; j < DUMPMSGS + lastmsg + 1; j++) {
		  i = j % DUMPMSGS;
		  if (msgs[i] && strcmp(msgs[i], "") ) {
		    if (msgs_count[i] == 1) {
		      dump_line("  ", msgs[i]);
		    } else {
		      Sprintf(tmpbuf, "%s (%dx)", msgs[i], msgs_count[i]);
		      dump_line("  ", tmpbuf);
		    }
		  }
		}
		dump_blockquote_end();
		dump("","");
	}
# endif /* DUMPMSGS */
#endif /* DUMP_LOG */
	/* render vision subsystem inoperative */
	iflags.vision_inited = 0;
	/* might have been killed while using a disposable item, so make sure
	   it's gone prior to inventory disclosure and creation of bones data */
	inven_inuse(TRUE);

#ifdef RECORD_REALTIME
	/* Update the realtime counter to reflect the playtime of the current
	 * game. */
	realtime_data.realtime = get_realtime();
#endif /* RECORD_REALTIME */

	/* Sometimes you die on the first move.  Life's not fair.
	 * On those rare occasions you get hosed immediately, go out
	 * smiling... :-)  -3.
	 */
	if (moves <= 1 && how < PANICKED)	/* You die... --More-- */
	    pline("Do not pass go.  Do not collect 200 %s.", currency(200L));

	if (have_windows) wait_synch();	/* flush screen output */
#ifndef NO_SIGNAL
	(void) signal(SIGINT, (SIG_RET_TYPE) done_intr);
# if defined(UNIX) || defined(VMS) || defined (__EMX__)
	(void) signal(SIGQUIT, (SIG_RET_TYPE) done_intr);
	(void) signal(SIGHUP, (SIG_RET_TYPE) done_hangup);
# endif
#endif /* NO_SIGNAL */

	bones_ok = (how < GENOCIDED) && can_make_bones();

	if (how == TURNED_SLIME)
	    u.ugrave_arise = PM_GREEN_SLIME;

	if (bones_ok && u.ugrave_arise < LOW_PM) {
	    /* corpse gets burnt up too */
	    if (how == BURNING)
		u.ugrave_arise = (NON_PM - 2);	/* leave no corpse */
	    else if (how == STONING)
		u.ugrave_arise = (NON_PM - 1);	/* statue instead of corpse */
	    else if (u.ugrave_arise == NON_PM &&
		     !(mvitals[u.umonnum].mvflags & G_NOCORPSE)) {
		int mnum = u.umonnum;

		if (!Upolyd) {
		    /* Base corpse on race when not poly'd since original
		     * u.umonnum is based on role, and all role monsters
		     * are human.
		     */
		    mnum = (flags.female && urace.femalenum != NON_PM) ?
			urace.femalenum : urace.malenum;
		}
		corpse = mk_named_object(CORPSE, &mons[mnum],
				       u.ux, u.uy, plname);
		Sprintf(pbuf, "%s, %s%s", plname,
			killer_format == NO_KILLER_PREFIX ? "" :
			killed_by_prefix[how],
			killer_format == KILLED_BY_AN ? an(killer) : killer);
		make_grave(u.ux, u.uy, pbuf);
	    }
	}

	if (how == QUIT) {
		killer_format = NO_KILLER_PREFIX;
		if (u.uhp < 1) {
			how = DIED;
			u.umortality++;	/* skipped above when how==QUIT */
			/* note that killer is pointing at kilbuf */
			Strcpy(kilbuf, "quit while already on Charon's boat");
		}
	}
#ifdef ASTRAL_ESCAPE
	if (how == ESCAPED || how == DEFIED || how == PANICKED)
#endif
		killer_format = NO_KILLER_PREFIX;

	if (how != PANICKED) {
	    /* these affect score and/or bones, but avoid them during panic */
	    taken = paybill((how == ESCAPED) ? -1 : (how != QUIT));
	    paygd();
	    clearpriests();
	} else	taken = FALSE;	/* lint; assert( !bones_ok ); */

	clearlocks();

	if (have_windows) display_nhwindow(WIN_MESSAGE, FALSE);

	if (how < PANICKED)
	    check_tutorial_message(QT_T_DEATH);

	if (strcmp(flags.end_disclose, "none") && how != PANICKED) {
		disclose(how, taken);
	}
	/* finish_paybill should be called after disclosure but before bones */
	if (bones_ok && taken) finish_paybill();

	/* calculate score, before creating bones [container gold] */
	{
	    long tmp;
	    int deepest = deepest_lev_reached(FALSE);

#ifndef GOLDOBJ
	    umoney = u.ugold;
	    tmp = u.ugold0;
#else
	    umoney = money_cnt(invent);
	    tmp = u.umoney0;
#endif
	    umoney += hidden_gold();	/* accumulate gold from containers */
	    tmp = umoney - tmp;		/* net gain */

	    if (tmp < 0L)
		tmp = 0L;
	    if (how < PANICKED)
		tmp -= tmp / 10L;
	    u.urscore += tmp;
	    u.urscore += 50L * (long)(deepest - 1);
	    if (deepest > 20)
		u.urscore += 1000L * (long)((deepest > 30) ? 10 : deepest - 20);
#ifdef ASTRAL_ESCAPE
	    if (how == ASCENDED || how == DEFIED) u.urscore *= 2L;
#endif
	}

	if (bones_ok) {
#ifdef WIZARD
	    if (!wizard || paranoid_yn("Save bones?", iflags.paranoid_quit) == 'y')
#endif /* WIZARD */
		savebones(corpse);
	    /* corpse may be invalid pointer now so
		ensure that it isn't used again */
	    corpse = (struct obj *)0;
	}

	/* update gold for the rip output, which can't use hidden_gold()
	   (containers will be gone by then if bones just got saved...) */
#ifndef GOLDOBJ
	u.ugold = umoney;
#else
	done_money = umoney;
#endif

#ifdef DUMP_LOG
	dumpoverview();
	dump("", "");
#endif

	/* dump some time related information */
#define DUMP_DATE_FORMAT "%Y-%m-%d %H:%M:%S"
	dump_title("Game information");
	dump_html("<div class=\"nh_game_information\">\n", "");
	dump_line("  Started: ", get_formatted_time(u.ubirthday, DUMP_DATE_FORMAT));
	dump_line("  Ended:   ", get_formatted_time(u.udeathday, DUMP_DATE_FORMAT));
#ifdef RECORD_REALTIME
	Sprintf(pbuf, "  Play time: %s", iso8601_duration(realtime_data.realtime));
	dump_line(pbuf,"");
#endif
	dump_html("</div>\n", "");
	dump("", "");

	/* clean up unneeded windows */
	if (have_windows) {
	    wait_synch();
	    display_nhwindow(WIN_MESSAGE, TRUE);
	    destroy_nhwindow(WIN_MAP);
	    destroy_nhwindow(WIN_STATUS);
	    destroy_nhwindow(WIN_MESSAGE);
	    WIN_MESSAGE = WIN_STATUS = WIN_MAP = WIN_ERR;

	    if(!done_stopprint || flags.tombstone)
		endwin = create_nhwindow(NHW_TEXT);

	    if (how < GENOCIDED && flags.tombstone && endwin != WIN_ERR)
		outrip(endwin, how);
	} else
	    done_stopprint = 1; /* just avoid any more output */

/* changing kilbuf really changes killer. we do it this way because
   killer is declared a (const char *)
*/
	if (u.uhave.amulet) {
		Strcat(kilbuf, " (with the Amulet)");
		killer_flags |= 0x1;
	}
	else if (how == ESCAPED) {
	    if (Is_astralevel(&u.uz)) {	/* offered Amulet to wrong deity */
		Strcat(kilbuf, " (in celestial disgrace)");
		killer_flags |= 0x2;
	    } else if (carrying(FAKE_AMULET_OF_YENDOR)) {
		Strcat(kilbuf, " (with a fake Amulet)");
		killer_flags |= 0x4;
		/* don't bother counting to see whether it should be plural */
	    }
	}

	    Sprintf(pbuf, "%s %s the %s...", Goodbye(), plname,
		   how != ASCENDED ?
		      (const char *) ((flags.female && urole.name.f) ?
		         urole.name.f : urole.name.m) :
		      (const char *) (flags.female ? "Demigoddess" : "Demigod"));
	if (!done_stopprint) {
	    putstr(endwin, 0, pbuf);
	    putstr(endwin, 0, "");
	}
	dump_html("<h2>Goodbye</h2>\n", "");
	dump_blockquote_start();
	dump_line("", pbuf);

#ifdef ASTRAL_ESCAPE
	if (how == ESCAPED || how == DEFIED || how == ASCENDED) {
#endif
	    struct monst *mtmp;
	    struct obj *otmp;
	    struct val_list *val;
	    int i;

	    for (val = valuables; val->list; val++)
		for (i = 0; i < val->size; i++) {
		    val->list[i].count = 0L;
		}
	    get_valuables(invent);

	    /* add points for collected valuables */
	    for (val = valuables; val->list; val++)
		for (i = 0; i < val->size; i++)
		    if (val->list[i].count != 0L)
			u.urscore += val->list[i].count
				  * (long)objects[val->list[i].typ].oc_cost;

	    /* count the points for artifacts */
	    artifact_score(invent, TRUE, endwin);

	    keepdogs(TRUE);
	    viz_array[0][0] |= IN_SIGHT; /* need visibility for naming */
	    mtmp = mydogs;
	    Strcpy(pbuf, "You");
	    if (mtmp) {
		while (mtmp) {
			Sprintf(eos(pbuf), " and %s", mon_nam(mtmp));
		    if (mtmp->mtame)
			u.urscore += mtmp->mhp;
		    mtmp = mtmp->nmon;
		}
		if (!done_stopprint) putstr(endwin, 0, pbuf);
		dump_line("", pbuf);
		pbuf[0] = '\0';
	    } else {
		if (!done_stopprint) Strcat(pbuf, " ");
	    }
		Sprintf(eos(pbuf), "%s with %ld point%s,",
			how==ASCENDED ? "went to your reward" :
#ifdef ASTRAL_ESCAPE
					(how==DEFIED ?
					"defied the Gods and escaped" :
					"escaped from the dungeon"),
#endif
			u.urscore, plur(u.urscore));
	    dump_line("", pbuf);
	    if (!done_stopprint) {
		putstr(endwin, 0, pbuf);
	    }

	    if (!done_stopprint)
		artifact_score(invent, FALSE, endwin);	/* list artifacts */
#ifdef DUMP_LOG
	    else
		artifact_score(invent, FALSE, WIN_ERR);
#endif
	    /* list valuables here */
	    for (val = valuables; val->list; val++) {
		sort_valuables(val->list, val->size);
		for (i = 0; i < val->size && !done_stopprint; i++) {
		    int typ = val->list[i].typ;
		    long count = val->list[i].count;

		    if (count == 0L) continue;
		    if (objects[typ].oc_class != GEM_CLASS || typ <= LAST_GEM) {
			otmp = mksobj(typ, FALSE, FALSE);
			makeknown(otmp->otyp);
			otmp->known = 1;	/* for fake amulets */
			otmp->dknown = 1;	/* seen it (blindness fix) */
			otmp->onamelth = 0;
			otmp->quan = count;
			Sprintf(pbuf, "%8ld %s (worth %ld %s),",
				count, xname(otmp),
				count * (long)objects[typ].oc_cost, currency(2L));
			obfree(otmp, (struct obj *)0);
		    } else {
			Sprintf(pbuf,
				"%8ld worthless piece%s of colored glass,",
				count, plur(count));
		    }
		    putstr(endwin, 0, pbuf);
		    dump_line("", pbuf);
		}
	    }

	} else {
	    /* did not escape or ascend */
	    if (u.uz.dnum == 0 && u.uz.dlevel <= 0) {
		/* level teleported out of the dungeon; `how' is DIED,
		   due to falling or to "arriving at heaven prematurely" */
		Sprintf(pbuf, "You %s beyond the confines of the dungeon",
			(u.uz.dlevel < 0) ? "passed away" : ends[how]);
	    } else {
		/* more conventional demise */
		const char *where = dungeons[u.uz.dnum].dname;

		if (Is_astralevel(&u.uz)) where = "The Astral Plane";
		Sprintf(pbuf, "You %s in %s", ends[how], where);
		if (!In_endgame(&u.uz) && !Is_knox(&u.uz))
		    Sprintf(eos(pbuf), " on dungeon level %d",
			    In_quest(&u.uz) ? dunlev(&u.uz) : depth(&u.uz));
	    }

	    Sprintf(eos(pbuf), " with %ld point%s,",
		    u.urscore, plur(u.urscore));
	    if (!done_stopprint) putstr(endwin, 0, pbuf);
	    dump_line("", pbuf);
	}

	    Sprintf(pbuf, "and %ld piece%s of gold, after %ld move%s.",
		    umoney, plur(umoney), moves, plur(moves));
	if (!done_stopprint)  putstr(endwin, 0, pbuf);
	dump_line("", pbuf);
	Sprintf(pbuf, "Killer: %s", killer);
	dump_line("", pbuf);
	Sprintf(pbuf,
	     "You were level %d with a maximum of %d hit point%s when you %s.",
		    u.ulevel, u.uhpmax, plur(u.uhpmax), ends[how]);
	if (!done_stopprint) {
	    putstr(endwin, 0, pbuf);
	    putstr(endwin, 0, "");
	}
	dump_line("", pbuf);
	dump_blockquote_end();

	if (!done_stopprint)
	    display_nhwindow(endwin, TRUE);
	if (endwin != WIN_ERR)
	    destroy_nhwindow(endwin);

	/* "So when I die, the first thing I will see in Heaven is a
	 * score list?" */
	if (flags.toptenwin) {
	    topten(how);
	    if (have_windows)
		exit_nhwindows((char *)0);
	} else {
	    if (have_windows)
		exit_nhwindows((char *)0);
	    topten(how);
	}
#ifdef DUMP_LOG
	dump_exit();
#endif

	if(done_stopprint) { raw_print(""); raw_print(""); }
	terminate(EXIT_SUCCESS);
}

void
container_contents(list, identified, all_containers, want_disp)
struct obj *list;
boolean identified, all_containers, want_disp;
{
	struct obj *box, *obj;
#ifdef SORTLOOT
	struct obj **oarray;
	int i,j,n;
	char *invlet;
#endif /* SORTLOOT */
	char buf[BUFSZ];

	for (box = list; box; box = box->nobj) {
	    int saveknown = objects[box->otyp].oc_name_known;
	    objects[box->otyp].oc_name_known = 1;
	    if (Is_container(box) || box->otyp == STATUE) {
		if (box->otyp == BAG_OF_TRICKS && box->spe) {
		    continue;	/* bag of tricks with charges can't contain anything */
		} else if (box->cobj) {
		    winid tmpwin = WIN_ERR;
		    if (want_disp)
			    tmpwin = create_nhwindow(NHW_MENU);
#ifdef SORTLOOT
		    /* count the number of items */
		    for (n = 0, obj = box->cobj; obj; obj = obj->nobj) n++;
		    /* Make a temporary array to store the objects sorted */
		    oarray = (struct obj **) alloc(n*sizeof(struct obj*));

		    /* Add objects to the array */
		    i = 0;
		    invlet = flags.inv_order;
		nextclass:
		    for (obj = box->cobj; obj; obj = obj->nobj) {
                      if (!flags.sortpack || obj->oclass == *invlet) {
			if (iflags.sortloot == 'f'
			    || iflags.sortloot == 'l') {
			  /* Insert object at correct index */
			  for (j = i; j; j--) {
			    if ((sortloot_cmp(obj, oarray[j-1])>0)
			    || (flags.sortpack &&
				oarray[j-1]->oclass != obj->oclass))
			      break;
			    oarray[j] = oarray[j-1];
			  }
			  oarray[j] = obj;
			  i++;
			} else {
			  /* Just add it to the array */
			  oarray[i++] = obj;
			}
		      }
		    } /* for loop */
		    if (flags.sortpack) {
		      if (*++invlet) goto nextclass;
		    }
#endif /* SORTLOOT */
		    Sprintf(buf, "Contents of %s:", the(xname(box)));
		    if (want_disp) {
			    putstr(tmpwin, 0, buf);
			    putstr(tmpwin, 0, "");
		    }
		    dump_subtitle(buf);
		    dump_list_start();
#ifdef SORTLOOT
		    for (i = 0; i < n; i++) {
			obj = oarray[i];
#else
		    for (obj = box->cobj; obj; obj = obj->nobj) {
#endif
			dump_list_item_object(obj);
			if (want_disp)
				putstr(tmpwin, 0, doname(obj));
		    }
		    dump_list_end();
		    dump("","");
		    if (want_disp) {
			    display_nhwindow(tmpwin, TRUE);
			    destroy_nhwindow(tmpwin);
		    }
		    if (all_containers) {
			container_contents(box->cobj, identified, TRUE,
					  want_disp);
		    }
		} else {
		    objects[box->otyp].oc_name_known = 1;
		    if (want_disp) {
			    pline("%s empty.", Tobjnam(box, "are"));
			    display_nhwindow(WIN_MESSAGE, FALSE);
		    }
		    dump_line(The(xname(box)), " is empty.");
		    dump("", "");
		}
	    }
	    objects[box->otyp].oc_name_known = saveknown;
	    if (!all_containers)
		break;
	}
}


/* should be called with either EXIT_SUCCESS or EXIT_FAILURE */
void
terminate(status)
int status;
{
#ifdef MAC
	getreturn("to exit");
#endif
	/* don't bother to try to release memory if we're in panic mode, to
	   avoid trouble in case that happens to be due to memory problems */
	if (!program_state.panicking) {
	    freedynamicdata();
	    dlb_cleanup();
	}

	nethack_exit(status);
}

void		/* showborn patch */
list_vanquished(defquery, ask)
char defquery;
boolean ask;
#ifdef DUMP_LOG
{
  do_vanquished(defquery, ask);
}

void
do_vanquished(defquery, ask)
int defquery;
boolean ask;
#endif
{
    int i, lev;
    int ntypes = 0, max_lev = 0, nkilled;
    long total_killed = 0L;
    char c;
    winid klwin = WIN_ERR;
    char buf[BUFSZ];

    /* get totals first */
    for (i = LOW_PM; i < NUMMONS; i++) {
	if (mvitals[i].died) ntypes++;
	total_killed += (long)mvitals[i].died;
	if (mons[i].mlevel > max_lev) max_lev = mons[i].mlevel;
    }

    /* vanquished creatures list;
     * includes all dead monsters, not just those killed by the player
     */
    if (ntypes != 0) {
	Sprintf(buf, "Vanquished creatures:");
	c = done_stopprint ? 'n': ask ?
	  yn_function("Do you want an account of creatures vanquished?",
			      ynqchars, defquery) : defquery;
	if (c == 'q') done_stopprint++;
	if (c == 'y') {
	    klwin = create_nhwindow(NHW_MENU);
	    putstr(klwin, 0, buf);
	    putstr(klwin, 0, "");
	}
	dump_title(buf);
	dump_list_start();

	    /* countdown by monster "toughness" */
	    for (lev = max_lev; lev >= 0; lev--)
	      for (i = LOW_PM; i < NUMMONS; i++)
		if (mons[i].mlevel == lev && (nkilled = mvitals[i].died) > 0) {
		    if ((mons[i].geno & G_UNIQ) && i != PM_HIGH_PRIEST) {
			Sprintf(buf, "%s%s",
				!type_is_pname(&mons[i]) ? "The " : "",
				mons[i].mname);
			if (nkilled > 1) {
			    switch (nkilled) {
				case 2:  Sprintf(eos(buf)," (twice)");  break;
				case 3:  Sprintf(eos(buf)," (thrice)");  break;
				default: Sprintf(eos(buf)," (%d time%s)",
						 nkilled, plur(nkilled));
					 break;
			    }
			}
		    } else {
			/* trolls or undead might have come back,
			   but we don't keep track of that */
			if (nkilled == 1)
			    Strcpy(buf, an(mons[i].mname));
			else
			    Sprintf(buf, "%d %s",
				    nkilled, makeplural(mons[i].mname));
#ifdef SHOW_BORN
			if (iflags.show_born && nkilled != mvitals[i].born)
			    Sprintf(buf + strlen(buf), " (%d created)",
				    (int) mvitals[i].born);
#endif
		    }
		    if (c == 'y') putstr(klwin, 0, buf);
		    dump_list_item_link(mons[i].mname, buf);
		}
	    dump_list_end();
	    if (Hallucination)
	     putstr(klwin, 0, "and a partridge in a pear tree");
	    if (ntypes > 1) {
		if (c == 'y') putstr(klwin, 0, "");
		Sprintf(buf, "%ld creatures vanquished.", total_killed);
		if (c == 'y') putstr(klwin, 0, buf);
		dump_line("  ", buf);
	    }
	    if (c == 'y') {
	    display_nhwindow(klwin, TRUE);
	    destroy_nhwindow(klwin);
	}
	dump("", "");
    }
}

/* number of monster species which have been genocided */
int
num_genocides()
{
    int i, n = 0;

    for (i = LOW_PM; i < NUMMONS; ++i)
	if (mvitals[i].mvflags & G_GENOD) ++n;

    return n;
}

STATIC_OVL void
list_genocided(defquery, ask, want_disp)
int defquery;
boolean ask;
boolean want_disp;
{
    int i;
    int ngenocided=0;
#ifdef SHOW_EXTINCT
    int nextincted=0;
#endif
    char c;
    winid klwin = (winid)NULL;
    char buf[BUFSZ];

    /* get totals first */
#ifdef SHOW_EXTINCT
    for (i = LOW_PM; i < NUMMONS; i++) {
	if (mvitals[i].mvflags & G_GENOD)
	    ngenocided++;
	else if ( (mvitals[i].mvflags & G_GONE) && !(mons[i].geno & G_UNIQ) )
	    nextincted++;
    }
    ngenocided = num_genocides();
#endif

    /* genocided species list */
    if (ngenocided != 0
#ifdef SHOW_EXTINCT
      || nextincted != 0
#endif
    ) {
#ifdef SHOW_EXTINCT
	if (nextincted != 0)
	  c = ask ?
	  yn_function("Do you want a list of species genocided or extinct?",
		      ynqchars, defquery) : defquery;
       else
#endif
	c = ask ? yn_function("Do you want a list of species genocided?",
			      ynqchars, defquery) : defquery;
	if (c == 'q') done_stopprint++;
#ifdef SHOW_EXTINCT
	Sprintf(buf, "Genocided or extinct species:");
#else
	Sprintf(buf, "Genocided species:");
#endif
	if (c == 'y') {
	    klwin = create_nhwindow(NHW_MENU);
	    putstr(klwin, 0, buf);
	    putstr(klwin, 0, "");
	}
	dump_title(buf);
	dump_list_start();

	    for (i = LOW_PM; i < NUMMONS; i++)
#ifdef SHOW_EXTINCT
	      if (mvitals[i].mvflags & G_GONE && !(mons[i].geno & G_UNIQ) ){
#else
		if (mvitals[i].mvflags & G_GENOD) {
#endif
		    if ((mons[i].geno & G_UNIQ) && i != PM_HIGH_PRIEST)
			Sprintf(buf, "%s%s",
				!type_is_pname(&mons[i]) ? "" : "the ",
				mons[i].mname);
		    else
			Strcpy(buf, makeplural(mons[i].mname));
#ifdef SHOW_EXTINCT
		    if( !(mvitals[i].mvflags & G_GENOD) )
			Strcat(buf, " (extinct)");
#endif
		    if (klwin) putstr(klwin, 0, buf);
		    dump_list_item(buf);
		}
#ifdef SHOW_EXTINCT
	    if (nextincted > 0 && aprilfoolsday()) {
		dump_list_item("ammonites (extinct)");
		nextincted++;
	    }
#endif
	    dump_list_end();

	    if (klwin) putstr(klwin, 0, "");
#ifdef SHOW_EXTINCT
	    if (ngenocided>0) {
#endif
	    Sprintf(buf, "%d species genocided.", ngenocided);
	    if (klwin) putstr(klwin, 0, buf);
	    dump_line("  ", buf);
#ifdef SHOW_EXTINCT
	    }
	    if (nextincted>0) {
	      Sprintf(buf, "%d species extinct.", nextincted);
	      if (klwin) putstr(klwin, 0, buf);
	      dump_line("  ", buf);
	    }
#endif /* SHOW_EXTINCT */
	    dump("", "");

	    if (klwin) {
		    display_nhwindow(klwin, TRUE);
		    destroy_nhwindow(klwin);
	    }
	}
}

/*end.c*/
