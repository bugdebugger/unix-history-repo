/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * %sccs.include.redist.c%
 */

#ifndef lint
static char copyright[] =
"@(#) Copyright (c) 1990, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#ifndef lint
static char sccsid[] = "@(#)mail.local.c	8.11 (Berkeley) %G%";
#endif /* not lint */

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#if __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#ifndef LOCK_EX
# include <sys/file.h>
#endif

#ifdef BSD4_4
# include "pathnames.h"
#endif

#ifndef __P
# ifdef __STDC__
#  define __P(protos)	protos
# else
#  define __P(protos)	()
#  define const
# endif
#endif
#ifndef __dead
# if defined(__GNUC__) && (__GNUC__ < 2 || __GNUC_MINOR__ < 5) && !defined(__STRICT_ANSI__)
#  define __dead	__volatile
# else
#  define __dead
# endif
#endif

#ifndef BSD4_4
# define _BSD_VA_LIST_	va_list
extern char	*strerror __P((int));
#endif

#ifndef _PATH_LOCTMP
# define _PATH_LOCTMP	"/tmp/local.XXXXXX"
#endif
#ifndef _PATH_MAILDIR
# define _PATH_MAILDIR	"/var/spool/mail"
#endif

#ifndef S_ISLNK
# define S_ISLNK(mode)	(((mode) & _S_IFMT) == S_IFLNK)
#endif

int eval = EX_OK;			/* sysexits.h error value. */

void		deliver __P((int, char *));
void		e_to_sys __P((int));
__dead void	err __P((const char *, ...));
void		notifybiff __P((char *));
int		store __P((char *));
void		usage __P((void));
void		vwarn __P((const char *, _BSD_VA_LIST_));
void		warn __P((const char *, ...));

int
main(argc, argv)
	int argc;
	char *argv[];
{
	struct passwd *pw;
	int ch, fd;
	uid_t uid;
	char *from;
	extern char *optarg;
	extern int optind;

#ifdef LOG_MAIL
	openlog("mail.local", 0, LOG_MAIL);
#else
	openlog("mail.local", 0);
#endif

	from = NULL;
	while ((ch = getopt(argc, argv, "df:r:")) != EOF)
		switch(ch) {
		case 'd':		/* Backward compatible. */
			break;
		case 'f':
		case 'r':		/* Backward compatible. */
			if (from != NULL) {
				warn("multiple -f options");
				usage();
			}
			from = optarg;
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (!*argv)
		usage();

	/*
	 * If from not specified, use the name from getlogin() if the
	 * uid matches, otherwise, use the name from the password file
	 * corresponding to the uid.
	 */
	uid = getuid();
	if (!from && (!(from = getlogin()) ||
	    !(pw = getpwnam(from)) || pw->pw_uid != uid))
		from = (pw = getpwuid(uid)) ? pw->pw_name : "???";

	/*
	 * There is no way to distinguish the error status of one delivery
	 * from the rest of the deliveries.  So, if we failed hard on one
	 * or more deliveries, but had no failures on any of the others, we
	 * return a hard failure.  If we failed temporarily on one or more
	 * deliveries, we return a temporary failure regardless of the other
	 * failures.  This results in the delivery being reattempted later
	 * at the expense of repeated failures and multiple deliveries.
	 */
	for (fd = store(from); *argv; ++argv)
		deliver(fd, *argv);
	exit(eval);
}

int
store(from)
	char *from;
{
	FILE *fp;
	time_t tval;
	int fd, eline;
	char line[2048];
	char tmpbuf[sizeof _PATH_LOCTMP + 1];

	strcpy(tmpbuf, _PATH_LOCTMP);
	if ((fd = mkstemp(tmpbuf)) == -1 || (fp = fdopen(fd, "w+")) == NULL) {
		e_to_sys(errno);
		err("unable to open temporary file");
	}
	(void)unlink(tmpbuf);

	(void)time(&tval);
	(void)fprintf(fp, "From %s %s", from, ctime(&tval));

	line[0] = '\0';
	for (eline = 1; fgets(line, sizeof(line), stdin);) {
		if (line[0] == '\n')
			eline = 1;
		else {
			if (eline && line[0] == 'F' &&
			    !memcmp(line, "From ", 5))
				(void)putc('>', fp);
			eline = 0;
		}
		(void)fprintf(fp, "%s", line);
		if (ferror(fp)) {
			e_to_sys(errno);
			err("temporary file write error");
		}
	}

	/* If message not newline terminated, need an extra. */
	if (!strchr(line, '\n'))
		(void)putc('\n', fp);
	/* Output a newline; note, empty messages are allowed. */
	(void)putc('\n', fp);

	if (fflush(fp) == EOF || ferror(fp)) {
		e_to_sys(errno);
		err("temporary file write error");
	}
	return (fd);
}

void
deliver(fd, name)
	int fd;
	char *name;
{
	struct stat fsb, sb;
	struct passwd *pw;
	int mbfd, nr, nw, off;
	char biffmsg[100], buf[8*1024], path[MAXPATHLEN];
	off_t curoff;

	/*
	 * Disallow delivery to unknown names -- special mailboxes can be
	 * handled in the sendmail aliases file.
	 */
	if (!(pw = getpwnam(name))) {
		if (eval != EX_TEMPFAIL)
			eval = EX_UNAVAILABLE;
		warn("unknown name: %s", name);
		return;
	}

	(void)snprintf(path, sizeof(path), "%s/%s", _PATH_MAILDIR, name);

	/*
	 * If the mailbox is linked or a symlink, fail.  There's an obvious
	 * race here, that the file was replaced with a symbolic link after
	 * the lstat returned, but before the open.  We attempt to detect
	 * this by comparing the original stat information and information
	 * returned by an fstat of the file descriptor returned by the open.
	 *
	 * NB: this is a symptom of a larger problem, that the mail spooling
	 * directory is writeable by the wrong users.  If that directory is
	 * writeable, system security is compromised for other reasons, and
	 * it cannot be fixed here.
	 *
	 * If we created the mailbox, set the owner/group.  If that fails,
	 * just return.  Another process may have already opened it, so we
	 * can't unlink it.  Historically, binmail set the owner/group at
	 * each mail delivery.  We no longer do this, assuming that if the
	 * ownership or permissions were changed there was a reason.
	 *
	 * XXX
	 * open(2) should support flock'ing the file.
	 */
tryagain:
	lockmbox(path);
	if (lstat(path, &sb)) {
		mbfd = open(path,
		    O_APPEND|O_CREAT|O_EXCL|O_WRONLY, S_IRUSR|S_IWUSR);
		if (mbfd == -1) {
			if (errno == EEXIST)
				goto tryagain;
		} else if (fchown(mbfd, pw->pw_uid, pw->pw_gid)) {
			e_to_sys(errno);
			warn("chown %u.%u: %s", pw->pw_uid, pw->pw_gid, name);
			unlockmbox();
			return;
		}
	} else if (sb.st_nlink != 1 || S_ISLNK(sb.st_mode)) {
		e_to_sys(errno);
		warn("%s: linked file", path);
		unlockmbox();
		return;
	} else if (sb.st_uid != pw->pw_uid) {
		warn("%s: wrong ownership (%d)", path, sb.st_uid);
		unlockmbox();
		return;
	} else {
		mbfd = open(path, O_APPEND|O_WRONLY, 0);
		if (mbfd != -1 &&
		    (fstat(mbfd, &fsb) || fsb.st_nlink != 1 ||
		    S_ISLNK(fsb.st_mode) || sb.st_dev != fsb.st_dev ||
		    sb.st_ino != fsb.st_ino) || sb.st_uid != fsb.st_uid) {
			warn("%s: file changed after open", path);
			(void)close(mbfd);
			unlockmbox();
			return;
		}
	}

	if (mbfd == -1) {
		e_to_sys(errno);
		warn("%s: %s", path, strerror(errno));
		unlockmbox();
		return;
	}

	/* Wait until we can get a lock on the file. */
	if (flock(mbfd, LOCK_EX)) {
		e_to_sys(errno);
		warn("%s: %s", path, strerror(errno));
		unlockmbox();
		goto err1;
	}

	/* Get the starting offset of the new message for biff. */
	curoff = lseek(mbfd, (off_t)0, SEEK_END);
	(void)snprintf(biffmsg, sizeof(biffmsg),
		sizeof curoff > sizeof(long) ? "%s@%qd\n" : "%s@%ld\n", 
		name, curoff);

	/* Copy the message into the file. */
	if (lseek(fd, (off_t)0, SEEK_SET) == (off_t)-1) {
		e_to_sys(errno);
		warn("temporary file: %s", strerror(errno));
		goto err1;
	}
	while ((nr = read(fd, buf, sizeof(buf))) > 0)
		for (off = 0; off < nr; nr -= nw, off += nw)
			if ((nw = write(mbfd, buf + off, nr)) < 0) {
				e_to_sys(errno);
				warn("%s: %s", path, strerror(errno));
				goto err2;;
			}
	if (nr < 0) {
		e_to_sys(errno);
		warn("temporary file: %s", strerror(errno));
		goto err2;;
	}

	/* Flush to disk, don't wait for update. */
	if (fsync(mbfd)) {
		e_to_sys(errno);
		warn("%s: %s", path, strerror(errno));
err2:		(void)ftruncate(mbfd, curoff);
err1:		(void)close(mbfd);
		unlockmbox();
		return;
	}
		
	/* Close and check -- NFS doesn't write until the close. */
	if (close(mbfd)) {
		e_to_sys(errno);
		warn("%s: %s", path, strerror(errno));
		unlockmbox();
		return;
	}

	unlockmbox();
	notifybiff(biffmsg);
}

/*
 * user.lock files are necessary for compatibility with other
 * systems, e.g., when the mail spool file is NFS exported.
 * Alas, mailbox locking is more than just a local matter.
 * EPA 11/94.
 */

char	lockname[50];
int	locked = 0;

lockmbox(path)
	char *path;
{
	char locktmp[50];
	int statfailed = 0;

	if (locked)
		return;
	sprintf(lockname, "%s.lock", path);
	sprintf(locktmp, "%s/tmXXXXXX", _PATH_MAILDIR);
	mktemp(locktmp);
	unlink(locktmp);
	for (;; sleep(5)) {
		int fd;
		struct stat st;
		time_t now;

		fd = open(locktmp, O_WRONLY|O_EXCL|O_CREAT, 0);
		if (fd < 0)
			continue;
		close(fd);
		if (link(locktmp, lockname) >= 0) {
			unlink(locktmp);
			locked = 1;
			return;
		}
		unlink(locktmp);
		if (stat(lockname, &st) < 0) {
			if (statfailed++ > 5)
				return;
			continue;
		}
		statfailed = 0;
		time(&now);
		if (now < st.st_ctime + 300)
			continue;
		unlink(lockname);
	}
}

unlockmbox()
{
	if (!locked)
		return;
	unlink(lockname);
	locked = 0;
}

void
notifybiff(msg)
	char *msg;
{
	static struct sockaddr_in addr;
	static int f = -1;
	struct hostent *hp;
	struct servent *sp;
	int len;

	if (!addr.sin_family) {
		/* Be silent if biff service not available. */
		if (!(sp = getservbyname("biff", "udp")))
			return;
		if (!(hp = gethostbyname("localhost"))) {
			warn("localhost: %s", strerror(errno));
			return;
		}
		addr.sin_family = hp->h_addrtype;
		memcpy(&addr.sin_addr, hp->h_addr, hp->h_length);
		addr.sin_port = sp->s_port;
	}
	if (f < 0 && (f = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		warn("socket: %s", strerror(errno));
		return;
	}
	len = strlen(msg) + 1;
	if (sendto(f, msg, len, 0, (struct sockaddr *)&addr, sizeof(addr))
	    != len)
		warn("sendto biff: %s", strerror(errno));
}

void
usage()
{
	eval = EX_USAGE;
	err("usage: mail.local [-f from] user ...");
}

#if __STDC__
void
err(const char *fmt, ...)
#else
void
err(fmt, va_alist)
	const char *fmt;
	va_dcl
#endif
{
	va_list ap;

#if __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	vwarn(fmt, ap);
	va_end(ap);

	exit(eval);
}

void
#if __STDC__
warn(const char *fmt, ...)
#else
warn(fmt, va_alist)
	const char *fmt;
	va_dcl
#endif
{
	va_list ap;

#if __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	vwarn(fmt, ap);
	va_end(ap);
}

void
vwarn(fmt, ap)
	const char *fmt;
	_BSD_VA_LIST_ ap;
{
	/*
	 * Log the message to stderr.
	 *
	 * Don't use LOG_PERROR as an openlog() flag to do this,
	 * it's not portable enough.
	 */
	if (eval != EX_USAGE)
		(void)fprintf(stderr, "mail.local: ");
	(void)vfprintf(stderr, fmt, ap);
	(void)fprintf(stderr, "\n");

#ifndef ultrix
	/* Log the message to syslog. */
	vsyslog(LOG_ERR, fmt, ap);
#else
	{
		char fmtbuf[10240];

		(void) sprintf(fmtbuf, fmt, ap);
		syslog(LOG_ERR, "%s", fmtbuf);
	}
#endif
}

/*
 * e_to_sys --
 *	Guess which errno's are temporary.  Gag me.
 */
void
e_to_sys(num)
	int num;
{
	/* Temporary failures override hard errors. */
	if (eval == EX_TEMPFAIL)
		return;

	switch(num) {		/* Hopefully temporary errors. */
#ifdef EAGAIN
	case EAGAIN:		/* Resource temporarily unavailable */
#endif
#ifdef EDQUOT
	case EDQUOT:		/* Disc quota exceeded */
#endif
#ifdef EBUSY
	case EBUSY:		/* Device busy */
#endif
#ifdef EPROCLIM
	case EPROCLIM:		/* Too many processes */
#endif
#ifdef EUSERS
	case EUSERS:		/* Too many users */
#endif
#ifdef ECONNABORTED
	case ECONNABORTED:	/* Software caused connection abort */
#endif
#ifdef ECONNREFUSED
	case ECONNREFUSED:	/* Connection refused */
#endif
#ifdef ECONNRESET
	case ECONNRESET:	/* Connection reset by peer */
#endif
#ifdef EDEADLK
	case EDEADLK:		/* Resource deadlock avoided */
#endif
#ifdef EFBIG
	case EFBIG:		/* File too large */
#endif
#ifdef EHOSTDOWN
	case EHOSTDOWN:		/* Host is down */
#endif
#ifdef EHOSTUNREACH
	case EHOSTUNREACH:	/* No route to host */
#endif
#ifdef EMFILE
	case EMFILE:		/* Too many open files */
#endif
#ifdef ENETDOWN
	case ENETDOWN:		/* Network is down */
#endif
#ifdef ENETRESET
	case ENETRESET:		/* Network dropped connection on reset */
#endif
#ifdef ENETUNREACH
	case ENETUNREACH:	/* Network is unreachable */
#endif
#ifdef ENFILE
	case ENFILE:		/* Too many open files in system */
#endif
#ifdef ENOBUFS
	case ENOBUFS:		/* No buffer space available */
#endif
#ifdef ENOMEM
	case ENOMEM:		/* Cannot allocate memory */
#endif
#ifdef ENOSPC
	case ENOSPC:		/* No space left on device */
#endif
#ifdef EROFS
	case EROFS:		/* Read-only file system */
#endif
#ifdef ESTALE
	case ESTALE:		/* Stale NFS file handle */
#endif
#ifdef ETIMEDOUT
	case ETIMEDOUT:		/* Connection timed out */
#endif
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN && EWOULDBLOCK != EDEADLK
	case EWOULDBLOCK:	/* Operation would block. */
#endif
		eval = EX_TEMPFAIL;
		break;
	default:
		eval = EX_UNAVAILABLE;
		break;
	}
}

#ifndef BSD4_4

char *
strerror(eno)
	int eno;
{
	extern int sys_nerr;
	extern char *sys_errlist[];
	static char ebuf[60];

	if (eno >= 0 && eno <= sys_nerr)
		return sys_errlist[eno];
	(void) sprintf(ebuf, "Error %d", eno);
	return ebuf;
}

#if __STDC__
snprintf(char *buf, int bufsiz, const char *fmt, ...)
#else
snprintf(buf, bufsiz, fmt, va_alist)
	char *buf;
	int bufsiz;
	const char *fmt;
	va_dcl
#endif
{
	va_list ap;

#if __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	vsprintf(buf, fmt, ap);
	va_end(ap);
}

#endif

#ifdef ultrix

int
mkstemp(template)
	char *template;
{
	int fd;

	return open(mktemp(template), O_RDWR|O_CREAT|O_EXCL, 0600);
}

#endif
