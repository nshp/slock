
/* See LICENSE file for license details. */
#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo.h>
#include <time.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

typedef struct {
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long colors[2];
} Lock;

static Lock **locks;
static int nscreens;
static Bool running = True;

static void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

#ifdef __linux__
#include <fcntl.h>

static void
dontkillme(void) {
	int fd;

	fd = open("/proc/self/oom_score_adj", O_WRONLY);
	if (fd < 0 && errno == ENOENT)
		return;
	if (fd < 0 || write(fd, "-1000\n", 6) != 6 || close(fd) != 0)
		die("cannot disable the out-of-memory killer for this process\n");
}
#endif

static double rad(double deg) {
	return deg * (3.141592653589 / 180);
}

static double memory(void) {
	FILE *fp;
	char buf[32];
	char *label;
	char *value;
	double total;
	double active;

	fp = fopen("/proc/meminfo", "r");
	for(int i = 0; i < 7; i++) {
		fgets(buf, 32, fp);
		label = strtok(buf, ":");
		value = strtok(NULL, ":");
		if (strcmp(label, "MemTotal") == 0) {
			total = strtod(value, NULL);
		}
		else if (strcmp(label, "Active") == 0) {
			active = strtod(value, NULL);
		}
	}
	return active/total*100.0;
}

static double battery(void) {
	FILE *fp;
	char buf[10];
	double now;
	double full;

	fp = fopen("/sys/class/power_supply/BAT1/charge_now", "r");
	fgets(buf, 16, fp);
	now = strtod(buf, NULL);
	fclose(fp);

	fp = fopen("/sys/class/power_supply/BAT1/charge_full", "r");
	fgets(buf, 16, fp);
	full = strtod(buf, NULL);
	fclose(fp);

	return now/full*100.0;
}

static Pixmap doodle(Display *dpy, int w, int h, Bool capslock) {
	Pixmap pm = XCreatePixmap(dpy, DefaultRootWindow(dpy), w, h,
					DefaultDepth(dpy, DefaultScreen(dpy)));
	cairo_surface_t *surface = cairo_xlib_surface_create(
		dpy, pm, DefaultVisual(dpy, DefaultScreen(dpy)),
		w, h);
	cairo_t *cr = cairo_create(surface);
	cairo_text_extents_t extents;
	double lw = 8.0;

	double mem_percent = memory();
	double bat_percent = battery();
	double x, y, clockheight;
	time_t epoch = time(NULL);
	char dt[10];
	const char cl[] = "capslock";
	strftime(dt, 10, "%H:%M", localtime(&epoch));

	cairo_set_source_rgba(cr, 0.13, 0.13, 0.13, 1.0);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_fill(cr);

	/*
	 * Troughs for the wheel thingies
	 * Nah. Turn those off here, unless we're sticking an image underneath.
	cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
	cairo_set_line_width(cr, lw);
	cairo_arc(cr, w/2.0, h/2.0, 200, 0, 6.283);
	cairo_stroke(cr);

	cairo_arc(cr, w/2.0, h/2.0, 188, 0, 6.283);
	cairo_stroke(cr);
	*/
	/* cairo_arc(cr, w/2.0, h/2.0, 176, 0, 6.283); */
	/* cairo_stroke(cr); */

	/* Actual percentage bits */
	cairo_set_source_rgba(cr, 0.08, 0.45, 0.82, 0.8);
	cairo_set_line_width(cr, lw - 1.0);
	cairo_arc(cr, w/2.0, h/2.0, 200, rad(90), rad(90 + mem_percent*360/100));
	cairo_stroke(cr);

	cairo_set_source_rgba(cr, 0.45, 0.82, 0.08, 0.8);
	cairo_arc(cr, w/2.0, h/2.0, 188, rad(90), rad(90 + bat_percent*360/100));
	cairo_stroke(cr);

	/* Clock */
	cairo_set_source_rgba(cr, 0.82, 0.08, 0.45, 1.0);
	cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 70.0);
	cairo_text_extents (cr, dt, &extents);
	x = w/2.0-(extents.width/2 + extents.x_bearing);
	y = h/2.0-(extents.height/2 + extents.y_bearing);
	clockheight = extents.height;
	cairo_move_to(cr, x, y);
	cairo_text_path(cr, dt);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 0.03, 0.03, 0.03, 1.0);
	cairo_set_line_width(cr, 2.5);
	cairo_stroke(cr);

	if(capslock) {
		cairo_set_source_rgba(cr, 0.82, 0.08, 0.45, 1.0);
		cairo_set_font_size(cr, 40.0);
		cairo_text_extents(cr, cl, &extents);
		x = w/2.0 -  (extents.width/2 + extents.x_bearing);
		y += clockheight/2 + 20;
		cairo_move_to(cr, x, y);
		cairo_text_path(cr, cl);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 0.03, 0.03, 0.03, 1.0);
		cairo_set_line_width(cr, 2.5);
		cairo_stroke(cr);
	}

	return pm;
}

#ifndef HAVE_BSD_AUTH
static const char *
getpw(void) { /* only run as root */
	const char *rval;
	struct passwd *pw;

	errno = 0;
	pw = getpwuid(getuid());
	if (!pw) {
		if (errno)
			die("slock: getpwuid: %s\n", strerror(errno));
		else
			die("slock: cannot retrieve password entry (make sure to suid or sgid slock)\n");
	}
	rval =  pw->pw_passwd;

#if HAVE_SHADOW_H
	if (rval[0] == 'x' && rval[1] == '\0') {
		struct spwd *sp;
		sp = getspnam(getenv("USER"));
		if(!sp)
			die("slock: cannot retrieve shadow entry (make sure to suid or sgid slock)\n");
		rval = sp->sp_pwdp;
	}
#endif

	/* drop privileges */
	if (geteuid() == 0
	   && ((getegid() != pw->pw_gid && setgid(pw->pw_gid) < 0) || setuid(pw->pw_uid) < 0))
		die("slock: cannot drop privileges\n");
	return rval;
}
#endif

static void
#ifdef HAVE_BSD_AUTH
readpw(Display *dpy)
#else
readpw(Display *dpy, const char *pws)
#endif
{
	char buf[32], passwd[256];
	int num, screen;
	unsigned int len, llen;
	KeySym ksym;
	XEvent ev;
	struct timespec tim, tim2;
	tim.tv_sec = 0;
        Bool capslock = False;

	len = llen = 0;
	running = True;

	/* As "slock" stands for "Simple X display locker", the DPMS settings
	 * had been removed and you can set it with "xset" or some other
	 * utility. This way the user can easily set a customized DPMS
	 * timeout. */
	while(running && !XNextEvent(dpy, &ev)) {
		if(ev.type == KeyPress) {
			buf[0] = 0;
			num = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);
			if(IsKeypadKey(ksym)) {
				if(ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if(ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if(IsFunctionKey(ksym) || IsKeypadKey(ksym)
					|| IsMiscFunctionKey(ksym) || IsPFKey(ksym)
					|| IsPrivateKeypadKey(ksym))
				continue;
			capslock = (ev.xkey.state & LockMask);
			switch(ksym) {
			case XK_Return:
				passwd[len] = 0;
				/* Delay by a random # of ms to thwart brute-force */
				tim.tv_nsec = 400000000L + (rand() % 50000000L);
				printf("Sleeping for %ld nsec\n", tim.tv_nsec);
				nanosleep(&tim, &tim2);
#ifdef HAVE_BSD_AUTH
				running = !auth_userokay(getlogin(), NULL, "auth-xlock", passwd);
#else
				running = !!strcmp(crypt(passwd, pws), pws);
#endif
				if(running)
					XBell(dpy, 100);
				len = 0;
				break;
			case XK_Escape:
				len = 0;
				break;
			case XK_BackSpace:
				if(len)
					--len;
				break;
			default:
				if(num && !iscntrl((int) buf[0]) && (len + num < sizeof passwd)) {
					memcpy(passwd + len, buf, num);
					len += num;
				}
				break;
			}
			if (len > 0) {
				for(screen = 0; screen < nscreens; screen++) {
					XSetWindowBackgroundPixmap(dpy, locks[screen]->win, doodle(dpy, 1920, 1080, capslock));
					XClearWindow(dpy, locks[screen]->win);
				}
			} else if(llen != 0 && len == 0) {
				for(screen = 0; screen < nscreens; screen++) {
					XSetWindowBackground(dpy, locks[screen]->win, locks[screen]->colors[0]);
					XClearWindow(dpy, locks[screen]->win);
				}
			}
			llen = len;
		}
		else for(screen = 0; screen < nscreens; screen++)
			XRaiseWindow(dpy, locks[screen]->win);
	}
}

static void
unlockscreen(Display *dpy, Lock *lock) {
	if(dpy == NULL || lock == NULL)
		return;

	XUngrabPointer(dpy, CurrentTime);
	XFreeColors(dpy, DefaultColormap(dpy, lock->screen), lock->colors, 2, 0);
	XFreePixmap(dpy, lock->pmap);
	XDestroyWindow(dpy, lock->win);

	free(lock);
}

static Lock *
lockscreen(Display *dpy, int screen) {
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	unsigned int len;
	Lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;

	if(dpy == NULL || screen < 0)
		return NULL;

	lock = malloc(sizeof(Lock));
	if(lock == NULL)
		return NULL;

	lock->screen = screen;

	lock->root = RootWindow(dpy, lock->screen);

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = BlackPixel(dpy, lock->screen);
	lock->win = XCreateWindow(dpy, lock->root, 0, 0, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen),
			0, DefaultDepth(dpy, lock->screen), CopyFromParent,
			DefaultVisual(dpy, lock->screen), CWOverrideRedirect | CWBackPixel, &wa);
	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen), COLOR2, &color, &dummy);
	lock->colors[1] = color.pixel;
	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen), COLOR1, &color, &dummy);
	lock->colors[0] = color.pixel;
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);
	XMapRaised(dpy, lock->win);
	for(len = 1000; len; len--) {
		if(XGrabPointer(dpy, lock->root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
			GrabModeAsync, GrabModeAsync, None, invisible, CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}
	if(running && (len > 0)) {
		for(len = 1000; len; len--) {
			if(XGrabKeyboard(dpy, lock->root, True, GrabModeAsync, GrabModeAsync, CurrentTime)
				== GrabSuccess)
				break;
			usleep(1000);
		}
	}

	running &= (len > 0);
	if(!running) {
		unlockscreen(dpy, lock);
		lock = NULL;
	}
	else 
		XSelectInput(dpy, lock->root, SubstructureNotifyMask);

	return lock;
}

static void
usage(void) {
	fprintf(stderr, "usage: slock [-v]\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv) {
#ifndef HAVE_BSD_AUTH
	const char *pws;
#endif
	Display *dpy;
	int screen;

	srand(time(NULL));

	if((argc == 2) && !strcmp("-v", argv[1]))
		die("slock-%s, © 2006-2012 Anselm R Garbe\n", VERSION);
	else if(argc != 1)
		usage();

#ifdef __linux__
	dontkillme();
#endif

	if(!getpwuid(getuid()))
		die("slock: no passwd entry for you\n");

#ifndef HAVE_BSD_AUTH
	pws = getpw();
#endif

	if(!(dpy = XOpenDisplay(0)))
		die("slock: cannot open display\n");
	/* Get the number of screens in display "dpy" and blank them all. */
	nscreens = ScreenCount(dpy);
	locks = malloc(sizeof(Lock *) * nscreens);
	if(locks == NULL)
		die("slock: malloc: %s\n", strerror(errno));
	int nlocks = 0;
	for(screen = 0; screen < nscreens; screen++) {
		if ( (locks[screen] = lockscreen(dpy, screen)) != NULL)
			nlocks++;
	}
	XSync(dpy, False);

	/* Did we actually manage to lock something? */
	if (nlocks == 0) { // nothing to protect
		free(locks);
		XCloseDisplay(dpy);
		return 1;
	}

	/* Everything is now blank. Now wait for the correct password. */
#ifdef HAVE_BSD_AUTH
	readpw(dpy);
#else
	readpw(dpy, pws);
#endif

	/* Password ok, unlock everything and quit. */
	for(screen = 0; screen < nscreens; screen++)
		unlockscreen(dpy, locks[screen]);

	free(locks);
	XCloseDisplay(dpy);

	return 0;
}
