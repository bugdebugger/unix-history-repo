#ifndef lint
static	char *sccsid = "@(#)main.c	4.10 (Berkeley) 84/02/09";
#endif

#include "defs.h"

/*
 * Remote distribution program.
 */

char	*distfile = "distfile";
char	tmpfile[] = "/tmp/rdistXXXXXX";
char	*tmpname = &tmpfile[5];

int	debug;		/* debugging flag */
int	nflag;		/* NOP flag, just print commands without executing */
int	qflag;		/* Quiet. Don't print messages */
int	options;	/* global options */
int	iamremote;	/* act as remote server for transfering files */

int	filec;		/* number of files to update */
char	**filev;	/* list of files/directories to update */
FILE	*fin = NULL;	/* input file pointer */
int	rem = -1;	/* file descriptor to remote source/sink process */
char	host[32];	/* host name */
int	nerrs;		/* number of errors while sending/receiving */
char	user[10];	/* user's name */
char	homedir[128];	/* user's home directory */
int	userid;		/* user's user ID */
int	groupid;	/* user's group ID */

struct	passwd *pw;	/* pointer to static area used by getpwent */
struct	group *gr;	/* pointer to static area used by getgrent */

main(argc, argv)
	int argc;
	char *argv[];
{
	register char *arg;
	int cmdargs = 0;

	pw = getpwuid(userid = getuid());
	if (pw == NULL) {
		fprintf(stderr, "%s: Who are you?\n", argv[0]);
		exit(1);
	}
	strcpy(user, pw->pw_name);
	strcpy(homedir, pw->pw_dir);
	groupid = pw->pw_gid;
	gethostname(host, sizeof(host));

	while (--argc > 0) {
		if ((arg = *++argv)[0] != '-')
			break;
		if (!strcmp(arg, "-Server"))
			iamremote++;
		else while (*++arg)
			switch (*arg) {
			case 'f':
				if (--argc <= 0)
					usage();
				distfile = *++argv;
				if (distfile[0] == '-' && distfile[1] == '\0')
					fin = stdin;
				break;

			case 'd':
				if (--argc <= 0)
					usage();
				define(*++argv);
				break;

			case 'D':
				debug++;
				break;

			case 'c':
				cmdargs++;
				break;

			case 'n':
				if (options & VERIFY) {
					printf("rdist: -n overrides -v\n");
					options &= ~VERIFY;
				}
				nflag++;
				break;

			case 'q':
				qflag++;
				break;

			case 'b':
				options |= COMPARE;
				break;

			case 'R':
				options |= REMOVE;
				break;

			case 'v':
				if (nflag) {
					printf("rdist: -n overrides -v\n");
					break;
				}
				options |= VERIFY;
				break;

			case 'w':
				options |= WHOLE;
				break;

			case 'y':
				options |= YOUNGER;
				break;

			default:
				usage();
			}
	}

	mktemp(tmpfile);

	if (iamremote) {
		server();
		exit(nerrs);
	}

	if (cmdargs)
		docmdargs(argc, argv);
	else {
		if (fin == NULL && (fin = fopen(distfile, "r")) == NULL) {
			perror(distfile);
			exit(1);
		}
		yyparse();
		if (nerrs == 0)
			docmds(argc, argv);
	}

	exit(nerrs);
}

usage()
{
	printf("Usage: rdist [-nqbrvwyD] [-f distfile] [-d var=value] [file ...]\n");
	printf("or: rdist [-nqbrvwyD] -c source [...] machine[:dest]\n");
	exit(1);
}

/*
 * rcp like interface for distributing files.
 */
docmdargs(nargs, args)
	int nargs;
	char *args[];
{
	register struct namelist *nl, *prev;
	register char *cp;
	struct namelist *files, *hosts;
	struct subcmd *cmds;
	char *dest;
	static struct namelist tnl = { NULL, NULL };
	int i;

	if (nargs < 2)
		usage();

	prev = NULL;
	for (i = 0; i < nargs - 1; i++) {
		nl = makenl(args[i]);
		if (prev == NULL)
			files = prev = nl;
		else {
			prev->n_next = nl;
			prev = nl;
		}
	}

	cp = args[i];
	if ((dest = index(cp, ':')) != NULL)
		*dest++ = '\0';
	tnl.n_name = cp;
	hosts = expand(&tnl, E_ALL);

	if (dest == NULL || *dest == '\0')
		cmds = NULL;
	else {
		cmds = makesubcmd(INSTALL);
		cmds->sc_options = options;
		cmds->sc_name = dest;
	}

	if (debug) {
		printf("docmdargs()\nfiles = ");
		prnames(files);
		printf("hosts = ");
		prnames(hosts);
	}
	insert(files, hosts, cmds);
	docmds(0, NULL);
}

/*
 * Print a list of NAME blocks (mostly for debugging).
 */
prnames(nl)
	register struct namelist *nl;
{
	printf("( ");
	while (nl != NULL) {
		printf("%s ", nl->n_name);
		nl = nl->n_next;
	}
	printf(")\n");
}

/*VARARGS*/
warn(fmt, a1, a2,a3)
	char *fmt;
{
	extern int yylineno;

	fprintf(stderr, "rdist: line %d: Warning: ", yylineno);
	fprintf(stderr, fmt, a1, a2, a3);
	fputc('\n', stderr);
}
