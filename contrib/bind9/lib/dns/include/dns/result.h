/*
 * Copyright (C) 2004-2008, 2010, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

#ifndef DNS_RESULT_H
#define DNS_RESULT_H 1

/*! \file dns/result.h */

#include <isc/lang.h>
#include <isc/resultclass.h>

#include <dns/types.h>

/*
 * Nothing in this file truly depends on <isc/result.h>, but the
 * DNS result codes are considered to be publicly derived from
 * the ISC result codes, so including this file buys you the ISC_R_
 * namespace too.
 */
#include <isc/result.h>		/* Contractual promise. */

/*
 * DNS library result codes
 */
#define DNS_R_LABELTOOLONG		(ISC_RESULTCLASS_DNS + 0)
#define DNS_R_BADESCAPE			(ISC_RESULTCLASS_DNS + 1)
/*
 * Since we dropped the support of bitstring labels, deprecate the related
 * result codes too.

#define DNS_R_BADBITSTRING		(ISC_RESULTCLASS_DNS + 2)
#define DNS_R_BITSTRINGTOOLONG		(ISC_RESULTCLASS_DNS + 3)
*/
#define DNS_R_EMPTYLABEL		(ISC_RESULTCLASS_DNS + 4)
#define DNS_R_BADDOTTEDQUAD		(ISC_RESULTCLASS_DNS + 5)
#define DNS_R_INVALIDNS			(ISC_RESULTCLASS_DNS + 6)
#define DNS_R_UNKNOWN			(ISC_RESULTCLASS_DNS + 7)
#define DNS_R_BADLABELTYPE		(ISC_RESULTCLASS_DNS + 8)
#define DNS_R_BADPOINTER		(ISC_RESULTCLASS_DNS + 9)
#define DNS_R_TOOMANYHOPS		(ISC_RESULTCLASS_DNS + 10)
#define DNS_R_DISALLOWED		(ISC_RESULTCLASS_DNS + 11)
#define DNS_R_EXTRATOKEN		(ISC_RESULTCLASS_DNS + 12)
#define DNS_R_EXTRADATA			(ISC_RESULTCLASS_DNS + 13)
#define DNS_R_TEXTTOOLONG		(ISC_RESULTCLASS_DNS + 14)
#define DNS_R_NOTZONETOP		(ISC_RESULTCLASS_DNS + 15)
#define DNS_R_SYNTAX			(ISC_RESULTCLASS_DNS + 16)
#define DNS_R_BADCKSUM			(ISC_RESULTCLASS_DNS + 17)
#define DNS_R_BADAAAA			(ISC_RESULTCLASS_DNS + 18)
#define DNS_R_NOOWNER			(ISC_RESULTCLASS_DNS + 19)
#define DNS_R_NOTTL			(ISC_RESULTCLASS_DNS + 20)
#define DNS_R_BADCLASS			(ISC_RESULTCLASS_DNS + 21)
#define DNS_R_NAMETOOLONG		(ISC_RESULTCLASS_DNS + 22)
#define DNS_R_PARTIALMATCH		(ISC_RESULTCLASS_DNS + 23)
#define DNS_R_NEWORIGIN			(ISC_RESULTCLASS_DNS + 24)
#define DNS_R_UNCHANGED			(ISC_RESULTCLASS_DNS + 25)
#define DNS_R_BADTTL			(ISC_RESULTCLASS_DNS + 26)
#define DNS_R_NOREDATA			(ISC_RESULTCLASS_DNS + 27)
#define DNS_R_CONTINUE			(ISC_RESULTCLASS_DNS + 28)
#define DNS_R_DELEGATION		(ISC_RESULTCLASS_DNS + 29)
#define DNS_R_GLUE			(ISC_RESULTCLASS_DNS + 30)
#define DNS_R_DNAME			(ISC_RESULTCLASS_DNS + 31)
#define DNS_R_CNAME			(ISC_RESULTCLASS_DNS + 32)
#define DNS_R_BADDB			(ISC_RESULTCLASS_DNS + 33)
#define DNS_R_ZONECUT			(ISC_RESULTCLASS_DNS + 34)
#define DNS_R_BADZONE			(ISC_RESULTCLASS_DNS + 35)
#define DNS_R_MOREDATA			(ISC_RESULTCLASS_DNS + 36)
#define DNS_R_UPTODATE			(ISC_RESULTCLASS_DNS + 37)
#define DNS_R_TSIGVERIFYFAILURE		(ISC_RESULTCLASS_DNS + 38)
#define DNS_R_TSIGERRORSET		(ISC_RESULTCLASS_DNS + 39)
#define DNS_R_SIGINVALID		(ISC_RESULTCLASS_DNS + 40)
#define DNS_R_SIGEXPIRED		(ISC_RESULTCLASS_DNS + 41)
#define DNS_R_SIGFUTURE			(ISC_RESULTCLASS_DNS + 42)
#define DNS_R_KEYUNAUTHORIZED		(ISC_RESULTCLASS_DNS + 43)
#define DNS_R_INVALIDTIME		(ISC_RESULTCLASS_DNS + 44)
#define DNS_R_EXPECTEDTSIG		(ISC_RESULTCLASS_DNS + 45)
#define DNS_R_UNEXPECTEDTSIG		(ISC_RESULTCLASS_DNS + 46)
#define DNS_R_INVALIDTKEY		(ISC_RESULTCLASS_DNS + 47)
#define DNS_R_HINT			(ISC_RESULTCLASS_DNS + 48)
#define DNS_R_DROP			(ISC_RESULTCLASS_DNS + 49)
#define DNS_R_NOTLOADED			(ISC_RESULTCLASS_DNS + 50)
#define DNS_R_NCACHENXDOMAIN		(ISC_RESULTCLASS_DNS + 51)
#define DNS_R_NCACHENXRRSET		(ISC_RESULTCLASS_DNS + 52)
#define DNS_R_WAIT			(ISC_RESULTCLASS_DNS + 53)
#define DNS_R_NOTVERIFIEDYET		(ISC_RESULTCLASS_DNS + 54)
#define DNS_R_NOIDENTITY		(ISC_RESULTCLASS_DNS + 55)
#define DNS_R_NOJOURNAL			(ISC_RESULTCLASS_DNS + 56)
#define DNS_R_ALIAS			(ISC_RESULTCLASS_DNS + 57)
#define DNS_R_USETCP			(ISC_RESULTCLASS_DNS + 58)
#define DNS_R_NOVALIDSIG		(ISC_RESULTCLASS_DNS + 59)
#define DNS_R_NOVALIDNSEC		(ISC_RESULTCLASS_DNS + 60)
#define DNS_R_NOTINSECURE		(ISC_RESULTCLASS_DNS + 61)
#define DNS_R_UNKNOWNSERVICE		(ISC_RESULTCLASS_DNS + 62)
#define DNS_R_RECOVERABLE		(ISC_RESULTCLASS_DNS + 63)
#define DNS_R_UNKNOWNOPT		(ISC_RESULTCLASS_DNS + 64)
#define DNS_R_UNEXPECTEDID		(ISC_RESULTCLASS_DNS + 65)
#define DNS_R_SEENINCLUDE		(ISC_RESULTCLASS_DNS + 66)
#define DNS_R_NOTEXACT			(ISC_RESULTCLASS_DNS + 67)
#define DNS_R_BLACKHOLED		(ISC_RESULTCLASS_DNS + 68)
#define DNS_R_BADALG			(ISC_RESULTCLASS_DNS + 69)
#define DNS_R_METATYPE			(ISC_RESULTCLASS_DNS + 70)
#define DNS_R_CNAMEANDOTHER		(ISC_RESULTCLASS_DNS + 71)
#define DNS_R_SINGLETON			(ISC_RESULTCLASS_DNS + 72)
#define DNS_R_HINTNXRRSET		(ISC_RESULTCLASS_DNS + 73)
#define DNS_R_NOMASTERFILE		(ISC_RESULTCLASS_DNS + 74)
#define DNS_R_UNKNOWNPROTO		(ISC_RESULTCLASS_DNS + 75)
#define DNS_R_CLOCKSKEW			(ISC_RESULTCLASS_DNS + 76)
#define DNS_R_BADIXFR			(ISC_RESULTCLASS_DNS + 77)
#define DNS_R_NOTAUTHORITATIVE		(ISC_RESULTCLASS_DNS + 78)
#define DNS_R_NOVALIDKEY		(ISC_RESULTCLASS_DNS + 79)
#define DNS_R_OBSOLETE			(ISC_RESULTCLASS_DNS + 80)
#define DNS_R_FROZEN			(ISC_RESULTCLASS_DNS + 81)
#define DNS_R_UNKNOWNFLAG		(ISC_RESULTCLASS_DNS + 82)
#define DNS_R_EXPECTEDRESPONSE		(ISC_RESULTCLASS_DNS + 83)
#define DNS_R_NOVALIDDS			(ISC_RESULTCLASS_DNS + 84)
#define DNS_R_NSISADDRESS		(ISC_RESULTCLASS_DNS + 85)
#define DNS_R_REMOTEFORMERR		(ISC_RESULTCLASS_DNS + 86)
#define DNS_R_TRUNCATEDTCP		(ISC_RESULTCLASS_DNS + 87)
#define DNS_R_LAME			(ISC_RESULTCLASS_DNS + 88)
#define DNS_R_UNEXPECTEDRCODE		(ISC_RESULTCLASS_DNS + 89)
#define DNS_R_UNEXPECTEDOPCODE		(ISC_RESULTCLASS_DNS + 90)
#define DNS_R_CHASEDSSERVERS		(ISC_RESULTCLASS_DNS + 91)
#define DNS_R_EMPTYNAME			(ISC_RESULTCLASS_DNS + 92)
#define DNS_R_EMPTYWILD			(ISC_RESULTCLASS_DNS + 93)
#define DNS_R_BADBITMAP			(ISC_RESULTCLASS_DNS + 94)
#define DNS_R_FROMWILDCARD		(ISC_RESULTCLASS_DNS + 95)
#define DNS_R_BADOWNERNAME		(ISC_RESULTCLASS_DNS + 96)
#define DNS_R_BADNAME			(ISC_RESULTCLASS_DNS + 97)
#define DNS_R_DYNAMIC			(ISC_RESULTCLASS_DNS + 98)
#define DNS_R_UNKNOWNCOMMAND		(ISC_RESULTCLASS_DNS + 99)
#define DNS_R_MUSTBESECURE		(ISC_RESULTCLASS_DNS + 100)
#define DNS_R_COVERINGNSEC		(ISC_RESULTCLASS_DNS + 101)
#define DNS_R_MXISADDRESS		(ISC_RESULTCLASS_DNS + 102)
#define DNS_R_DUPLICATE			(ISC_RESULTCLASS_DNS + 103)
#define DNS_R_INVALIDNSEC3		(ISC_RESULTCLASS_DNS + 104)
#define DNS_R_NOTMASTER			(ISC_RESULTCLASS_DNS + 105)
#define DNS_R_BROKENCHAIN		(ISC_RESULTCLASS_DNS + 106)

#define DNS_R_NRESULTS			107	/*%< Number of results */

/*
 * DNS wire format rcodes.
 *
 * By making these their own class we can easily convert them into the
 * wire-format rcode value simply by masking off the resultclass.
 */
#define DNS_R_NOERROR			(ISC_RESULTCLASS_DNSRCODE + 0)
#define DNS_R_FORMERR			(ISC_RESULTCLASS_DNSRCODE + 1)
#define DNS_R_SERVFAIL			(ISC_RESULTCLASS_DNSRCODE + 2)
#define DNS_R_NXDOMAIN			(ISC_RESULTCLASS_DNSRCODE + 3)
#define DNS_R_NOTIMP			(ISC_RESULTCLASS_DNSRCODE + 4)
#define DNS_R_REFUSED			(ISC_RESULTCLASS_DNSRCODE + 5)
#define DNS_R_YXDOMAIN			(ISC_RESULTCLASS_DNSRCODE + 6)
#define DNS_R_YXRRSET			(ISC_RESULTCLASS_DNSRCODE + 7)
#define DNS_R_NXRRSET			(ISC_RESULTCLASS_DNSRCODE + 8)
#define DNS_R_NOTAUTH			(ISC_RESULTCLASS_DNSRCODE + 9)
#define DNS_R_NOTZONE			(ISC_RESULTCLASS_DNSRCODE + 10)
#define DNS_R_BADVERS			(ISC_RESULTCLASS_DNSRCODE + 16)

#define DNS_R_NRCODERESULTS		17	/*%< Number of rcode results */

#define DNS_RESULT_ISRCODE(result) \
	(ISC_RESULTCLASS_INCLASS(ISC_RESULTCLASS_DNSRCODE, (result)))

ISC_LANG_BEGINDECLS

const char *
dns_result_totext(isc_result_t);

void
dns_result_register(void);

dns_rcode_t
dns_result_torcode(isc_result_t result);

ISC_LANG_ENDDECLS

#endif /* DNS_RESULT_H */
