/*
 * Copyright (c) 1996 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static const char rcsid[] = "$Id: nul_ng.c,v 1.7 1997/12/04 04:58:01 halley Exp $";
#endif

/*
 * nul_ng.c - the netgroup accessor null map
 */

#include "port_before.h"

#include <sys/types.h>
#include <netinet/in.h>

#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>

#include <irs.h>

#include "port_after.h"

#include "irs_p.h"
#include "hesiod.h"
#include "dns_p.h"

/* Forward. */

static void 		ng_close(struct irs_ng *);
static int		ng_next(struct irs_ng *, char **, char **, char **);
static int		ng_test(struct irs_ng *,
 				const char *, const char *,
				const char *, const char *);
static void		ng_rewind(struct irs_ng *, const char *);
static void		ng_minimize(struct irs_ng *);

/* Public. */

struct irs_ng *
irs_nul_ng(struct irs_acc *this) {
	struct irs_ng *ng;

	if (!(ng = malloc(sizeof *ng))) {
		errno = ENOMEM;
		return (NULL);
	}
	memset(ng, 0x5e, sizeof *ng);
	ng->private = NULL;
	ng->close = ng_close;
	ng->next = ng_next;
	ng->test = ng_test;
	ng->rewind = ng_rewind;
	ng->minimize = ng_minimize;
	return (ng);
}

/* Methods. */

static void
ng_close(struct irs_ng *this) {
	free(this);
}

/* ARGSUSED */
static int
ng_next(struct irs_ng *this, char **host, char **user, char **domain) {
	errno = ENOENT;
	return (-1);
}

static int
ng_test(struct irs_ng *this, const char *name,
	const char *user, const char *host, const char *domain)
{
	errno = ENODEV;
	return (-1);
}

static void
ng_rewind(struct irs_ng *this, const char *netgroup) {
	/* NOOP */
}

static void
ng_minimize(struct irs_ng *this) {
	/* NOOP */
}
