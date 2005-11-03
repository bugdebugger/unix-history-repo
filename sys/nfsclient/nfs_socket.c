/*-
 * Copyright (c) 1989, 1991, 1993, 1995
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)nfs_socket.c	8.5 (Berkeley) 3/30/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Socket operations for use by nfs
 */

#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/protosw.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/vnode.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <rpc/rpcclnt.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfsclient/nfs.h>
#include <nfs/xdr_subs.h>
#include <nfsclient/nfsm_subs.h>
#include <nfsclient/nfsmount.h>
#include <nfsclient/nfsnode.h>

#include <nfs4client/nfs4.h>

#define	TRUE	1
#define	FALSE	0

/*
 * Estimate rto for an nfs rpc sent via. an unreliable datagram.
 * Use the mean and mean deviation of rtt for the appropriate type of rpc
 * for the frequent rpcs and a default for the others.
 * The justification for doing "other" this way is that these rpcs
 * happen so infrequently that timer est. would probably be stale.
 * Also, since many of these rpcs are
 * non-idempotent, a conservative timeout is desired.
 * getattr, lookup - A+2D
 * read, write     - A+4D
 * other           - nm_timeo
 */
#define	NFS_RTO(n, t) \
	((t) == 0 ? (n)->nm_timeo : \
	 ((t) < 3 ? \
	  (((((n)->nm_srtt[t-1] + 3) >> 2) + (n)->nm_sdrtt[t-1] + 1) >> 1) : \
	  ((((n)->nm_srtt[t-1] + 7) >> 3) + (n)->nm_sdrtt[t-1] + 1)))
#define	NFS_SRTT(r)	(r)->r_nmp->nm_srtt[proct[(r)->r_procnum] - 1]
#define	NFS_SDRTT(r)	(r)->r_nmp->nm_sdrtt[proct[(r)->r_procnum] - 1]

/*
 * Defines which timer to use for the procnum.
 * 0 - default
 * 1 - getattr
 * 2 - lookup
 * 3 - read
 * 4 - write
 */
static int proct[NFS_NPROCS] = {
	0, 1, 0, 2, 1, 3, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 0, 0, 0, 0,
};

static int	nfs_realign_test;
static int	nfs_realign_count;
static int	nfs_bufpackets = 4;
static int	nfs_reconnects;

SYSCTL_DECL(_vfs_nfs);

SYSCTL_INT(_vfs_nfs, OID_AUTO, realign_test, CTLFLAG_RW, &nfs_realign_test, 0, "");
SYSCTL_INT(_vfs_nfs, OID_AUTO, realign_count, CTLFLAG_RW, &nfs_realign_count, 0, "");
SYSCTL_INT(_vfs_nfs, OID_AUTO, bufpackets, CTLFLAG_RW, &nfs_bufpackets, 0, "");
SYSCTL_INT(_vfs_nfs, OID_AUTO, reconnects, CTLFLAG_RD, &nfs_reconnects, 0,
    "number of times the nfs client has had to reconnect");


/*
 * There is a congestion window for outstanding rpcs maintained per mount
 * point. The cwnd size is adjusted in roughly the way that:
 * Van Jacobson, Congestion avoidance and Control, In "Proceedings of
 * SIGCOMM '88". ACM, August 1988.
 * describes for TCP. The cwnd size is chopped in half on a retransmit timeout
 * and incremented by 1/cwnd when each rpc reply is received and a full cwnd
 * of rpcs is in progress.
 * (The sent count and cwnd are scaled for integer arith.)
 * Variants of "slow start" were tried and were found to be too much of a
 * performance hit (ave. rtt 3 times larger),
 * I suspect due to the large rtt that nfs rpcs have.
 */
#define	NFS_CWNDSCALE	256
#define	NFS_MAXCWND	(NFS_CWNDSCALE * 32)
#define	NFS_NBACKOFF	8
static int nfs_backoff[NFS_NBACKOFF] = { 2, 4, 8, 16, 32, 64, 128, 256, };
struct callout	nfs_callout;

static int	nfs_msg(struct thread *, const char *, const char *, int);
static int	nfs_realign(struct mbuf **pm, int hsiz);
static int	nfs_reply(struct nfsreq *);
static void	nfs_softterm(struct nfsreq *rep);
static int	nfs_reconnect(struct nfsreq *rep);
static void nfs_clnt_tcp_soupcall(struct socket *so, void *arg, int waitflag);
static void nfs_clnt_udp_soupcall(struct socket *so, void *arg, int waitflag);
static void wakeup_nfsreq(struct nfsreq *req);

extern struct mtx nfs_reqq_mtx;
extern struct mtx nfs_reply_mtx;

/*
 * Initialize sockets and congestion for a new NFS connection.
 * We do not free the sockaddr if error.
 */
int
nfs_connect(struct nfsmount *nmp, struct nfsreq *rep)
{
	struct socket *so;
	int error, rcvreserve, sndreserve;
	int pktscale;
	struct sockaddr *saddr;
	struct thread *td = &thread0; /* only used for socreate and sobind */

	NET_ASSERT_GIANT();

	if (nmp->nm_sotype == SOCK_STREAM) {
		mtx_lock(&nmp->nm_nfstcpstate.mtx);
 		nmp->nm_nfstcpstate.flags |= NFS_TCP_EXPECT_RPCMARKER;
 		nmp->nm_nfstcpstate.rpcresid = 0;
		mtx_unlock(&nmp->nm_nfstcpstate.mtx);
 	}	
	nmp->nm_so = NULL;
	saddr = nmp->nm_nam;
	error = socreate(saddr->sa_family, &nmp->nm_so, nmp->nm_sotype,
		nmp->nm_soproto, nmp->nm_mountp->mnt_cred, td);
	if (error)
		goto bad;
	so = nmp->nm_so;
	nmp->nm_soflags = so->so_proto->pr_flags;

	/*
	 * Some servers require that the client port be a reserved port number.
	 */
	if (nmp->nm_flag & NFSMNT_RESVPORT) {
		struct sockopt sopt;
		int ip, ip2, len;
		struct sockaddr_in6 ssin;
		struct sockaddr *sa;

		bzero(&sopt, sizeof sopt);
		switch(saddr->sa_family) {
		case AF_INET:
			sopt.sopt_level = IPPROTO_IP;
			sopt.sopt_name = IP_PORTRANGE;
			ip = IP_PORTRANGE_LOW;
			ip2 = IP_PORTRANGE_DEFAULT;
			len = sizeof (struct sockaddr_in);
			break;
#ifdef INET6
		case AF_INET6:
			sopt.sopt_level = IPPROTO_IPV6;
			sopt.sopt_name = IPV6_PORTRANGE;
			ip = IPV6_PORTRANGE_LOW;
			ip2 = IPV6_PORTRANGE_DEFAULT;
			len = sizeof (struct sockaddr_in6);
			break;
#endif
		default:
			goto noresvport;
		}
		sa = (struct sockaddr *)&ssin;
		bzero(sa, len);
		sa->sa_len = len;
		sa->sa_family = saddr->sa_family;
		sopt.sopt_dir = SOPT_SET;
		sopt.sopt_val = (void *)&ip;
		sopt.sopt_valsize = sizeof(ip);
		error = sosetopt(so, &sopt);
		if (error)
			goto bad;
		error = sobind(so, sa, td);
		if (error)
			goto bad;
		ip = ip2;
		error = sosetopt(so, &sopt);
		if (error)
			goto bad;
	noresvport: ;
	}

	/*
	 * Protocols that do not require connections may be optionally left
	 * unconnected for servers that reply from a port other than NFS_PORT.
	 */
	if (nmp->nm_flag & NFSMNT_NOCONN) {
		if (nmp->nm_soflags & PR_CONNREQUIRED) {
			error = ENOTCONN;
			goto bad;
		}
	} else {
		error = soconnect(so, nmp->nm_nam, td);
		if (error)
			goto bad;

		/*
		 * Wait for the connection to complete. Cribbed from the
		 * connect system call but with the wait timing out so
		 * that interruptible mounts don't hang here for a long time.
		 */
		SOCK_LOCK(so);
		while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
			(void) msleep(&so->so_timeo, SOCK_MTX(so),
			    PSOCK, "nfscon", 2 * hz);
			if ((so->so_state & SS_ISCONNECTING) &&
			    so->so_error == 0 && rep &&
			    (error = nfs_sigintr(nmp, rep, rep->r_td)) != 0) {
				so->so_state &= ~SS_ISCONNECTING;
				SOCK_UNLOCK(so);
				goto bad;
			}
		}
		if (so->so_error) {
			error = so->so_error;
			so->so_error = 0;
			SOCK_UNLOCK(so);
			goto bad;
		}
		SOCK_UNLOCK(so);
	}
	so->so_rcv.sb_timeo = 12 * hz;
	so->so_snd.sb_timeo = 5 * hz;

	/*
	 * Get buffer reservation size from sysctl, but impose reasonable
	 * limits.
	 */
	pktscale = nfs_bufpackets;
	if (pktscale < 2)
		pktscale = 2;
	if (pktscale > 64)
		pktscale = 64;

	if (nmp->nm_sotype == SOCK_DGRAM) {
		sndreserve = (nmp->nm_wsize + NFS_MAXPKTHDR) * pktscale;
		rcvreserve = (max(nmp->nm_rsize, nmp->nm_readdirsize) +
		    NFS_MAXPKTHDR) * pktscale;
	} else if (nmp->nm_sotype == SOCK_SEQPACKET) {
		sndreserve = (nmp->nm_wsize + NFS_MAXPKTHDR) * pktscale;
		rcvreserve = (max(nmp->nm_rsize, nmp->nm_readdirsize) +
		    NFS_MAXPKTHDR) * pktscale;
	} else {
		if (nmp->nm_sotype != SOCK_STREAM)
			panic("nfscon sotype");
		if (so->so_proto->pr_flags & PR_CONNREQUIRED) {
			struct sockopt sopt;
			int val;

			bzero(&sopt, sizeof sopt);
			sopt.sopt_dir = SOPT_SET;
			sopt.sopt_level = SOL_SOCKET;
			sopt.sopt_name = SO_KEEPALIVE;
			sopt.sopt_val = &val;
			sopt.sopt_valsize = sizeof val;
			val = 1;
			sosetopt(so, &sopt);
		}
		if (so->so_proto->pr_protocol == IPPROTO_TCP) {
			struct sockopt sopt;
			int val;

			bzero(&sopt, sizeof sopt);
			sopt.sopt_dir = SOPT_SET;
			sopt.sopt_level = IPPROTO_TCP;
			sopt.sopt_name = TCP_NODELAY;
			sopt.sopt_val = &val;
			sopt.sopt_valsize = sizeof val;
			val = 1;
			sosetopt(so, &sopt);
		}
		sndreserve = (nmp->nm_wsize + NFS_MAXPKTHDR +
		    sizeof (u_int32_t)) * pktscale;
		rcvreserve = (nmp->nm_rsize + NFS_MAXPKTHDR +
		    sizeof (u_int32_t)) * pktscale;
	}
	error = soreserve(so, sndreserve, rcvreserve);
	if (error)
		goto bad;
	SOCKBUF_LOCK(&so->so_rcv);
	so->so_rcv.sb_flags |= SB_NOINTR;
 	so->so_upcallarg = (caddr_t)nmp;
 	if (so->so_type == SOCK_STREAM)
 		so->so_upcall = nfs_clnt_tcp_soupcall;
 	else	
 		so->so_upcall = nfs_clnt_udp_soupcall;
 	so->so_rcv.sb_flags |= SB_UPCALL;
	SOCKBUF_UNLOCK(&so->so_rcv);
	SOCKBUF_LOCK(&so->so_snd);
	so->so_snd.sb_flags |= SB_NOINTR;
	SOCKBUF_UNLOCK(&so->so_snd);

	/* Initialize other non-zero congestion variables */
	nmp->nm_srtt[0] = nmp->nm_srtt[1] = nmp->nm_srtt[2] =
		nmp->nm_srtt[3] = (NFS_TIMEO << 3);
	nmp->nm_sdrtt[0] = nmp->nm_sdrtt[1] = nmp->nm_sdrtt[2] =
		nmp->nm_sdrtt[3] = 0;
	nmp->nm_cwnd = NFS_MAXCWND / 2;	    /* Initial send window */
	nmp->nm_sent = 0;
	nmp->nm_timeouts = 0;
	return (0);

bad:
	nfs_disconnect(nmp);
	return (error);
}

/*
 * Reconnect routine:
 * Called when a connection is broken on a reliable protocol.
 * - clean up the old socket
 * - nfs_connect() again
 * - set R_MUSTRESEND for all outstanding requests on mount point
 * If this fails the mount point is DEAD!
 * nb: Must be called with the nfs_sndlock() set on the mount point.
 */
static int
nfs_reconnect(struct nfsreq *rep)
{
	struct nfsreq *rp;
	struct nfsmount *nmp = rep->r_nmp;
	int error;

	nfs_reconnects++;
	nfs_disconnect(nmp);
	while ((error = nfs_connect(nmp, rep)) != 0) {
		if (error == ERESTART)
			error = EINTR;
		if (error == EIO || error == EINTR)
			return (error);
		(void) tsleep(&lbolt, PSOCK, "nfscon", 0);
	}

  	/*
 	 * Clear the FORCE_RECONNECT flag only after the connect 
 	 * succeeds. To prevent races between multiple processes 
 	 * waiting on the mountpoint where the connection is being
 	 * torn down. The first one to acquire the sndlock will 
 	 * retry the connection. The others block on the sndlock
 	 * until the connection is established successfully, and 
 	 * then re-transmit the request.
 	 */
	mtx_lock(&nmp->nm_nfstcpstate.mtx);
	nmp->nm_nfstcpstate.flags &= ~NFS_TCP_FORCE_RECONNECT;
	mtx_unlock(&nmp->nm_nfstcpstate.mtx);	

	/*
	 * Loop through outstanding request list and fix up all requests
	 * on old socket.
	 */
	mtx_lock(&nfs_reqq_mtx);
	TAILQ_FOREACH(rp, &nfs_reqq, r_chain) {
		if (rp->r_nmp == nmp)
			rp->r_flags |= R_MUSTRESEND;
	}
	mtx_unlock(&nfs_reqq_mtx);
	return (0);
}

/*
 * NFS disconnect. Clean up and unlink.
 */
void
nfs_disconnect(struct nfsmount *nmp)
{
	struct socket *so;

	NET_ASSERT_GIANT();

	if (nmp->nm_so) {
		so = nmp->nm_so;
		nmp->nm_so = NULL;
		SOCKBUF_LOCK(&so->so_rcv);
 		so->so_upcallarg = NULL;
 		so->so_upcall = NULL;
 		so->so_rcv.sb_flags &= ~SB_UPCALL;
		SOCKBUF_UNLOCK(&so->so_rcv);
		soshutdown(so, SHUT_WR);
		soclose(so);
	}
}

void
nfs_safedisconnect(struct nfsmount *nmp)
{
	struct nfsreq dummyreq;

	bzero(&dummyreq, sizeof(dummyreq));
	dummyreq.r_nmp = nmp;
	nfs_disconnect(nmp);
}

/*
 * This is the nfs send routine. For connection based socket types, it
 * must be called with an nfs_sndlock() on the socket.
 * - return EINTR if the RPC is terminated, 0 otherwise
 * - set R_MUSTRESEND if the send fails for any reason
 * - do any cleanup required by recoverable socket errors (?)
 */
int
nfs_send(struct socket *so, struct sockaddr *nam, struct mbuf *top,
    struct nfsreq *rep)
{
	struct sockaddr *sendnam;
	int error, error2, soflags, flags;

	NET_ASSERT_GIANT();

	KASSERT(rep, ("nfs_send: called with rep == NULL"));

	error = nfs_sigintr(rep->r_nmp, rep, rep->r_td);
	if (error) {
		m_freem(top);
		return (error);
	}
	if ((so = rep->r_nmp->nm_so) == NULL) {
		rep->r_flags |= R_MUSTRESEND;
		m_freem(top);
		return (0);
	}
	rep->r_flags &= ~R_MUSTRESEND;
	soflags = rep->r_nmp->nm_soflags;

	if ((soflags & PR_CONNREQUIRED) || (so->so_state & SS_ISCONNECTED))
		sendnam = NULL;
	else
		sendnam = nam;
	if (so->so_type == SOCK_SEQPACKET)
		flags = MSG_EOR;
	else
		flags = 0;

	error = so->so_proto->pr_usrreqs->pru_sosend(so, sendnam, 0, top, 0,
						     flags, curthread /*XXX*/);
	if (error == ENOBUFS && so->so_type == SOCK_DGRAM) {
		error = 0;
		rep->r_flags |= R_MUSTRESEND;
	}

	if (error) {
		/*
		 * Don't report EPIPE errors on nfs sockets.
		 * These can be due to idle tcp mounts which will be closed by
		 * netapp, solaris, etc. if left idle too long.
		 */
		if (error != EPIPE) {
			log(LOG_INFO, "nfs send error %d for server %s\n",
			    error,
			    rep->r_nmp->nm_mountp->mnt_stat.f_mntfromname);
		}
		/*
		 * Deal with errors for the client side.
		 */
		error2 = NFS_SIGREP(rep);
		if (error2)
			error = error2;
		else
			rep->r_flags |= R_MUSTRESEND;

		/*
		 * Handle any recoverable (soft) socket errors here. (?)
		 */
		if (error != EINTR && error != ERESTART && error != EIO &&
			error != EWOULDBLOCK && error != EPIPE)
			error = 0;
	}
	return (error);
}

int
nfs_reply(struct nfsreq *rep)
{
	register struct socket *so;
	register struct mbuf *m;
	int error = 0, sotype, slpflag;

	NET_ASSERT_GIANT();

	sotype = rep->r_nmp->nm_sotype;
	/*
	 * For reliable protocols, lock against other senders/receivers
	 * in case a reconnect is necessary.
	 */
	if (sotype != SOCK_DGRAM) {
		error = nfs_sndlock(rep);
		if (error)
			return (error);
tryagain:
		if (rep->r_mrep) {
			nfs_sndunlock(rep);
			return (0);
		}
		if (rep->r_flags & R_SOFTTERM) {
			nfs_sndunlock(rep);
			return (EINTR);
		}
		so = rep->r_nmp->nm_so;
		mtx_lock(&rep->r_nmp->nm_nfstcpstate.mtx);
		if (!so || 
		    (rep->r_nmp->nm_nfstcpstate.flags & NFS_TCP_FORCE_RECONNECT)) {
			mtx_unlock(&rep->r_nmp->nm_nfstcpstate.mtx);
			error = nfs_reconnect(rep);
			if (error) {
				nfs_sndunlock(rep);
				return (error);
			}
			goto tryagain;
		} else
			mtx_unlock(&rep->r_nmp->nm_nfstcpstate.mtx);
		while (rep->r_flags & R_MUSTRESEND) {
			m = m_copym(rep->r_mreq, 0, M_COPYALL, M_WAIT);
			nfsstats.rpcretries++;
			error = nfs_send(so, rep->r_nmp->nm_nam, m, rep);
			if (error) {
				if (error == EINTR || error == ERESTART ||
				    (error = nfs_reconnect(rep)) != 0) {
					nfs_sndunlock(rep);
					return (error);
				}
				goto tryagain;
			}
		}
		nfs_sndunlock(rep);
	}
	slpflag = 0;
	if (rep->r_nmp->nm_flag & NFSMNT_INT)
		slpflag = PCATCH;
	mtx_lock(&nfs_reply_mtx);
	while ((rep->r_mrep == NULL) && (error == 0) && 
	       ((rep->r_flags & R_SOFTTERM) == 0) &&
	       ((sotype == SOCK_DGRAM) || ((rep->r_flags & R_MUSTRESEND) == 0)))
		error = msleep((caddr_t)rep, &nfs_reply_mtx, 
			       slpflag | (PZERO - 1), "nfsreq", 0);
	mtx_unlock(&nfs_reply_mtx);
	if (error == EINTR || error == ERESTART)
		/* NFS operations aren't restartable. Map ERESTART to EINTR */
		return (EINTR);
	if (rep->r_flags & R_SOFTTERM)
		/* Request was terminated because we exceeded the retries (soft mount) */
		return (ETIMEDOUT);
	if (sotype == SOCK_STREAM) {
		mtx_lock(&rep->r_nmp->nm_nfstcpstate.mtx);
		if (((rep->r_nmp->nm_nfstcpstate.flags & NFS_TCP_FORCE_RECONNECT) || 
		     (rep->r_flags & R_MUSTRESEND))) {
			mtx_unlock(&rep->r_nmp->nm_nfstcpstate.mtx);	
			error = nfs_sndlock(rep);
			if (error)
				return (error);
			goto tryagain;
		} else
			mtx_unlock(&rep->r_nmp->nm_nfstcpstate.mtx);
	}
	return (error);
}

/*
 * XXX TO DO
 * Make nfs_realign() non-blocking. Also make nfsm_dissect() nonblocking.
 */
static void
nfs_clnt_match_xid(struct socket *so, 
		   struct nfsmount *nmp, 
		   struct mbuf *mrep)
{
	struct mbuf *md;
	caddr_t dpos;
	u_int32_t rxid, *tl;
	struct nfsreq *rep;
	register int32_t t1;
	int error;
	
	/*
	 * Search for any mbufs that are not a multiple of 4 bytes long
	 * or with m_data not longword aligned.
	 * These could cause pointer alignment problems, so copy them to
	 * well aligned mbufs.
	 */
	if (nfs_realign(&mrep, 5 * NFSX_UNSIGNED) == ENOMEM) {
		m_freem(mrep);
		nfsstats.rpcinvalid++;
		return;
	}
	
	/*
	 * Get the xid and check that it is an rpc reply
	 */
	md = mrep;
	dpos = mtod(md, caddr_t);
	tl = nfsm_dissect_nonblock(u_int32_t *, 2*NFSX_UNSIGNED);
	rxid = *tl++;
	if (*tl != rpc_reply) {
		m_freem(mrep);
nfsmout:
		nfsstats.rpcinvalid++;
		return;
	}

	mtx_lock(&nfs_reqq_mtx);
	/*
	 * Loop through the request list to match up the reply
	 * Iff no match, just drop the datagram
	 */
	TAILQ_FOREACH(rep, &nfs_reqq, r_chain) {
		if (rep->r_mrep == NULL && rxid == rep->r_xid) {
			/* Found it.. */
			rep->r_mrep = mrep;
			rep->r_md = md;
			rep->r_dpos = dpos;
			/*
			 * Update congestion window.
			 * Do the additive increase of
			 * one rpc/rtt.
			 */
			if (nmp->nm_cwnd <= nmp->nm_sent) {
				nmp->nm_cwnd +=
					(NFS_CWNDSCALE * NFS_CWNDSCALE +
					 (nmp->nm_cwnd >> 1)) / nmp->nm_cwnd;
				if (nmp->nm_cwnd > NFS_MAXCWND)
					nmp->nm_cwnd = NFS_MAXCWND;
			}	
			if (rep->r_flags & R_SENT) {
				rep->r_flags &= ~R_SENT;
				nmp->nm_sent -= NFS_CWNDSCALE;
			}
			/*
			 * Update rtt using a gain of 0.125 on the mean
			 * and a gain of 0.25 on the deviation.
			 */
			if (rep->r_flags & R_TIMING) {
				/*
				 * Since the timer resolution of
				 * NFS_HZ is so course, it can often
				 * result in r_rtt == 0. Since
				 * r_rtt == N means that the actual
				 * rtt is between N+dt and N+2-dt ticks,
				 * add 1.
				 */
				t1 = rep->r_rtt + 1;
				t1 -= (NFS_SRTT(rep) >> 3);
				NFS_SRTT(rep) += t1;
				if (t1 < 0)
					t1 = -t1;
				t1 -= (NFS_SDRTT(rep) >> 2);
				NFS_SDRTT(rep) += t1;
			}
			nmp->nm_timeouts = 0;
			break;
		}
	}
	/*
	 * If not matched to a request, drop it.
	 * If it's mine, wake up requestor.
	 */
	if (rep == 0) {
		nfsstats.rpcunexpected++;
		m_freem(mrep);
	} else
		wakeup_nfsreq(rep);
	mtx_unlock(&nfs_reqq_mtx);
}

/* 
 * The wakeup of the requestor should be done under the mutex
 * to avoid potential missed wakeups.
 */
static void 
wakeup_nfsreq(struct nfsreq *req)
{
	mtx_lock(&nfs_reply_mtx);
	wakeup((caddr_t)req);
	mtx_unlock(&nfs_reply_mtx);	
}

static void
nfs_mark_for_reconnect(struct nfsmount *nmp)
{
	struct nfsreq *rp;

	mtx_lock(&nmp->nm_nfstcpstate.mtx);
	nmp->nm_nfstcpstate.flags |= NFS_TCP_FORCE_RECONNECT;
	mtx_unlock(&nmp->nm_nfstcpstate.mtx);
	/* 
	 * Wakeup all processes that are waiting for replies 
	 * on this mount point. One of them does the reconnect.
	 */
	mtx_lock(&nfs_reqq_mtx);
	TAILQ_FOREACH(rp, &nfs_reqq, r_chain) {
		if (rp->r_nmp == nmp) {
			rp->r_flags |= R_MUSTRESEND;
			wakeup_nfsreq(rp);
		}
	}
	mtx_unlock(&nfs_reqq_mtx);
}

static int
nfstcp_readable(struct socket *so, int bytes)
{
	int retval;
	
	SOCKBUF_LOCK(&so->so_rcv);
	retval = (so->so_rcv.sb_cc >= (bytes) ||
		  (so->so_state & SBS_CANTRCVMORE) ||
		  so->so_error);
	SOCKBUF_UNLOCK(&so->so_rcv);
	return (retval);
}

#define nfstcp_marker_readable(so)	nfstcp_readable(so, sizeof(u_int32_t))

static void
nfs_clnt_tcp_soupcall(struct socket *so, void *arg, int waitflag)
{
	struct nfsmount *nmp = (struct nfsmount *)arg;
	struct mbuf *mp = NULL;
	struct uio auio;
	int error;
	u_int32_t len;
	int rcvflg;

	/*
	 * Don't pick any more data from the socket if we've marked the 
	 * mountpoint for reconnect.
	 */
	mtx_lock(&nmp->nm_nfstcpstate.mtx);
	if (nmp->nm_nfstcpstate.flags & NFS_TCP_FORCE_RECONNECT) {
		mtx_unlock(&nmp->nm_nfstcpstate.mtx);		
		return;
	} else			
		mtx_unlock(&nmp->nm_nfstcpstate.mtx);
	auio.uio_td = curthread;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	for ( ; ; ) {
		if (nmp->nm_nfstcpstate.flags & NFS_TCP_EXPECT_RPCMARKER) {
			if (!nfstcp_marker_readable(so)) {
				/* Marker is not readable */
				return;
			}
			auio.uio_resid = sizeof(u_int32_t);
			auio.uio_iov = NULL;
			auio.uio_iovcnt = 0;
			mp = NULL;
			rcvflg = (MSG_DONTWAIT | MSG_SOCALLBCK);
			error =  so->so_proto->pr_usrreqs->pru_soreceive
				(so, (struct sockaddr **)0,
				 &auio, &mp, (struct mbuf **)0, &rcvflg);
			/*
			 * We've already tested that the socket is readable. 2 cases 
			 * here, we either read 0 bytes (client closed connection), 
			 * or got some other error. In both cases, we tear down the 
			 * connection.
			 */
			if (error || auio.uio_resid > 0) {
				if (error != ECONNRESET) {
					log(LOG_ERR, 
					    "nfs/tcp clnt: Error %d reading socket, tearing down TCP connection\n",
					    error);
				}
				goto mark_reconnect;
			}
			if (mp == NULL)
				panic("nfs_clnt_tcp_soupcall: Got empty mbuf chain from sorecv\n");
			bcopy(mtod(mp, u_int32_t *), &len, sizeof(len));
			len = ntohl(len) & ~0x80000000;
			m_freem(mp);
			/*
			 * This is SERIOUS! We are out of sync with the sender
			 * and forcing a disconnect/reconnect is all I can do.
			 */
			if (len > NFS_MAXPACKET || len == 0) {
				log(LOG_ERR, "%s (%d) from nfs server %s\n",
				    "impossible packet length",
				    len,
				    nmp->nm_mountp->mnt_stat.f_mntfromname);
				goto mark_reconnect;
			}
			nmp->nm_nfstcpstate.rpcresid = len;
			nmp->nm_nfstcpstate.flags &= ~(NFS_TCP_EXPECT_RPCMARKER);
		}
		/* 
		 * Processed RPC marker or no RPC marker to process. 
		 * Pull in and process data.
		 */
		if (nmp->nm_nfstcpstate.rpcresid > 0) {
			if (!nfstcp_readable(so, nmp->nm_nfstcpstate.rpcresid)) {
				/* All data not readable */
				return;
			}
			auio.uio_resid = nmp->nm_nfstcpstate.rpcresid;
			auio.uio_iov = NULL;
			auio.uio_iovcnt = 0;
			mp = NULL;
			rcvflg = (MSG_DONTWAIT | MSG_SOCALLBCK);
			error =  so->so_proto->pr_usrreqs->pru_soreceive
				(so, (struct sockaddr **)0,
				 &auio, &mp, (struct mbuf **)0, &rcvflg);
			if (error || auio.uio_resid > 0) {
				if (error != ECONNRESET) {
					log(LOG_ERR, 
					    "nfs/tcp clnt: Error %d reading socket, tearing down TCP connection\n",
					    error);
				}
				goto mark_reconnect;				
			}
			if (mp == NULL)
				panic("nfs_clnt_tcp_soupcall: Got empty mbuf chain from sorecv\n");
			nmp->nm_nfstcpstate.rpcresid = 0;
			nmp->nm_nfstcpstate.flags |= NFS_TCP_EXPECT_RPCMARKER;
			/* We got the entire RPC reply. Match XIDs and wake up requestor */
			nfs_clnt_match_xid(so, nmp, mp);
		}
	}

mark_reconnect:
	nfs_mark_for_reconnect(nmp);
}

static void
nfs_clnt_udp_soupcall(struct socket *so, void *arg, int waitflag)
{
	struct nfsmount *nmp = (struct nfsmount *)arg;
	struct uio auio;
	struct mbuf *mp = NULL;
	struct mbuf *control = NULL;
	int error, rcvflag;

	auio.uio_resid = 1000000;
	auio.uio_td = curthread;
	rcvflag = MSG_DONTWAIT;
	auio.uio_resid = 1000000000;
	do {
		mp = control = NULL;
		error = so->so_proto->pr_usrreqs->pru_soreceive(so,
					NULL, &auio, &mp,
					&control, &rcvflag);
		if (control)
			m_freem(control);
		if (mp)
			nfs_clnt_match_xid(so, nmp, mp);
	} while (mp && !error);
}

/*
 * nfs_request - goes something like this
 *	- fill in request struct
 *	- links it into list
 *	- calls nfs_send() for first transmit
 *	- calls nfs_receive() to get reply
 *	- break down rpc header and return with nfs reply pointed to
 *	  by mrep or error
 * nb: always frees up mreq mbuf list
 */
/* XXX overloaded before */
#define	NQ_TRYLATERDEL	15	/* Initial try later delay (sec) */

int
nfs_request(struct vnode *vp, struct mbuf *mrest, int procnum,
    struct thread *td, struct ucred *cred, struct mbuf **mrp,
    struct mbuf **mdp, caddr_t *dposp)
{
	struct mbuf *mrep, *m2;
	struct nfsreq *rep;
	u_int32_t *tl;
	int i;
	struct nfsmount *nmp;
	struct mbuf *m, *md, *mheadend;
	time_t waituntil;
	caddr_t dpos;
	int s, error = 0, mrest_len, auth_len, auth_type;
	int trylater_delay = NQ_TRYLATERDEL, trylater_cnt = 0;
	struct timeval now;
	u_int32_t xid;

	/* Reject requests while attempting a forced unmount. */
	if (vp->v_mount->mnt_kern_flag & MNTK_UNMOUNTF) {
		m_freem(mrest);
		return (ESTALE);
	}
	nmp = VFSTONFS(vp->v_mount);
	if ((nmp->nm_flag & NFSMNT_NFSV4) != 0)
		return nfs4_request(vp, mrest, procnum, td, cred, mrp, mdp, dposp);
	MALLOC(rep, struct nfsreq *, sizeof(struct nfsreq), M_NFSREQ, M_WAITOK);
	rep->r_mrep = rep->r_md = NULL;
	rep->r_nmp = nmp;
	rep->r_vp = vp;
	rep->r_td = td;
	rep->r_procnum = procnum;

	getmicrouptime(&now);
	rep->r_lastmsg = now.tv_sec -
	    ((nmp->nm_tprintf_delay) - (nmp->nm_tprintf_initial_delay));
	mrest_len = m_length(mrest, NULL);

	/*
	 * Get the RPC header with authorization.
	 */
	auth_type = RPCAUTH_UNIX;
	if (cred->cr_ngroups < 1)
		panic("nfsreq nogrps");
	auth_len = ((((cred->cr_ngroups - 1) > nmp->nm_numgrps) ?
		nmp->nm_numgrps : (cred->cr_ngroups - 1)) << 2) +
		5 * NFSX_UNSIGNED;
	m = nfsm_rpchead(cred, nmp->nm_flag, procnum, auth_type, auth_len,
	     mrest, mrest_len, &mheadend, &xid);

	/*
	 * For stream protocols, insert a Sun RPC Record Mark.
	 */
	if (nmp->nm_sotype == SOCK_STREAM) {
		M_PREPEND(m, NFSX_UNSIGNED, M_TRYWAIT);
		*mtod(m, u_int32_t *) = htonl(0x80000000 |
			 (m->m_pkthdr.len - NFSX_UNSIGNED));
	}
	rep->r_mreq = m;
	rep->r_xid = xid;
tryagain:
	if (nmp->nm_flag & NFSMNT_SOFT)
		rep->r_retry = nmp->nm_retry;
	else
		rep->r_retry = NFS_MAXREXMIT + 1;	/* past clip limit */
	rep->r_rtt = rep->r_rexmit = 0;
	if (proct[procnum] > 0)
		rep->r_flags = R_TIMING;
	else
		rep->r_flags = 0;
	rep->r_mrep = NULL;

	/*
	 * Do the client side RPC.
	 */
	nfsstats.rpcrequests++;
	/*
	 * Chain request into list of outstanding requests. Be sure
	 * to put it LAST so timer finds oldest requests first.
	 */
	s = splsoftclock();
	mtx_lock(&nfs_reqq_mtx);
	if (TAILQ_EMPTY(&nfs_reqq))
		callout_reset(&nfs_callout, nfs_ticks, nfs_timer, NULL);
	TAILQ_INSERT_TAIL(&nfs_reqq, rep, r_chain);
	mtx_unlock(&nfs_reqq_mtx);

	/*
	 * If backing off another request or avoiding congestion, don't
	 * send this one now but let timer do it. If not timing a request,
	 * do it now.
	 */
	if (nmp->nm_so && (nmp->nm_sotype != SOCK_DGRAM ||
		(nmp->nm_flag & NFSMNT_DUMBTIMR) ||
		nmp->nm_sent < nmp->nm_cwnd)) {
		splx(s);
		error = nfs_sndlock(rep);
		if (!error) {
			m2 = m_copym(m, 0, M_COPYALL, M_TRYWAIT);
			error = nfs_send(nmp->nm_so, nmp->nm_nam, m2, rep);
			nfs_sndunlock(rep);
		}
		mtx_lock(&nfs_reqq_mtx);
		/* 
		 * nfs_timer() could've re-transmitted the request if we ended up
		 * blocking on nfs_send() too long, so check for R_SENT here.
		 */
		if (!error && (rep->r_flags & (R_SENT | R_MUSTRESEND)) == 0) {
			nmp->nm_sent += NFS_CWNDSCALE;
			rep->r_flags |= R_SENT;
		}
		mtx_unlock(&nfs_reqq_mtx);
	} else {
		splx(s);
		rep->r_rtt = -1;
	}

	/*
	 * Wait for the reply from our send or the timer's.
	 */
	if (!error || error == EPIPE)
		error = nfs_reply(rep);

	/*
	 * RPC done, unlink the request.
	 */
	s = splsoftclock();
	mtx_lock(&nfs_reqq_mtx);
	/*
	 * nfs_timer() may be in the process of re-transmitting this request.
	 * nfs_timer() drops the nfs_reqq_mtx before the pru_send() (to avoid LORs).
	 * Wait till nfs_timer() completes the re-transmission. When the reply 
	 * comes back, it will be discarded (since the req struct for it no longer 
	 * exists).
	 */
	while (rep->r_flags & R_REXMIT_INPROG) {
		msleep((caddr_t)&rep->r_flags, &nfs_reqq_mtx, 
		       (PZERO - 1), "nfsrxmt", 0);
	}
	TAILQ_REMOVE(&nfs_reqq, rep, r_chain);
	if (TAILQ_EMPTY(&nfs_reqq))
		callout_stop(&nfs_callout);
	/*
	 * Decrement the outstanding request count.
	 */
	if (rep->r_flags & R_SENT) {
		rep->r_flags &= ~R_SENT;	/* paranoia */
		nmp->nm_sent -= NFS_CWNDSCALE;
	}
	mtx_unlock(&nfs_reqq_mtx);
	splx(s);

	/*
	 * If there was a successful reply and a tprintf msg.
	 * tprintf a response.
	 */
	if (!error) {
		mtx_lock(&Giant);
		nfs_up(rep, nmp, rep->r_td, "is alive again", NFSSTA_TIMEO);
		mtx_unlock(&Giant);
	}
	mrep = rep->r_mrep;
	md = rep->r_md;
	dpos = rep->r_dpos;
	if (error) {
                /*
		 * If we got interrupted by a signal in nfs_reply(), there's
		 * a very small window where the reply could've come in before
		 * this process got scheduled in. To handle that case, we need 
		 * to free the reply if it was delivered.
		 */
		if (rep->r_mrep != NULL)
			m_freem(rep->r_mrep);
		m_freem(rep->r_mreq);
		free((caddr_t)rep, M_NFSREQ);
		return (error);
	}

	if (rep->r_mrep == NULL)
		panic("nfs_request: rep->r_mrep shouldn't be NULL if no error\n");

	/*
	 * break down the rpc header and check if ok
	 */
	tl = nfsm_dissect(u_int32_t *, 3 * NFSX_UNSIGNED);
	if (*tl++ == rpc_msgdenied) {
		if (*tl == rpc_mismatch)
			error = EOPNOTSUPP;
		else
			error = EACCES;
		m_freem(mrep);
		m_freem(rep->r_mreq);
		free((caddr_t)rep, M_NFSREQ);
		return (error);
	}

	/*
	 * Just throw away any verifyer (ie: kerberos etc).
	 */
	i = fxdr_unsigned(int, *tl++);		/* verf type */
	i = fxdr_unsigned(int32_t, *tl);	/* len */
	if (i > 0)
		nfsm_adv(nfsm_rndup(i));
	tl = nfsm_dissect(u_int32_t *, NFSX_UNSIGNED);
	/* 0 == ok */
	if (*tl == 0) {
		tl = nfsm_dissect(u_int32_t *, NFSX_UNSIGNED);
		if (*tl != 0) {
			error = fxdr_unsigned(int, *tl);
			if ((nmp->nm_flag & NFSMNT_NFSV3) &&
				error == NFSERR_TRYLATER) {
				m_freem(mrep);
				error = 0;
				waituntil = time_second + trylater_delay;
				while (time_second < waituntil)
					(void) tsleep(&lbolt,
						PSOCK, "nqnfstry", 0);
				trylater_delay *= nfs_backoff[trylater_cnt];
				if (trylater_cnt < NFS_NBACKOFF - 1)
					trylater_cnt++;
				goto tryagain;
			}

			/*
			 * If the File Handle was stale, invalidate the
			 * lookup cache, just in case.
			 */
			if (error == ESTALE)
				cache_purge(vp);
			if (nmp->nm_flag & NFSMNT_NFSV3) {
				*mrp = mrep;
				*mdp = md;
				*dposp = dpos;
				error |= NFSERR_RETERR;
			} else
				m_freem(mrep);
			m_freem(rep->r_mreq);
			free((caddr_t)rep, M_NFSREQ);
			return (error);
		}

		*mrp = mrep;
		*mdp = md;
		*dposp = dpos;
		m_freem(rep->r_mreq);
		FREE((caddr_t)rep, M_NFSREQ);
		return (0);
	}
	m_freem(mrep);
	error = EPROTONOSUPPORT;
nfsmout:
	m_freem(rep->r_mreq);
	free((caddr_t)rep, M_NFSREQ);
	return (error);
}

/*
 * Nfs timer routine
 * Scan the nfsreq list and retranmit any requests that have timed out
 * To avoid retransmission attempts on STREAM sockets (in the future) make
 * sure to set the r_retry field to 0 (implies nm_retry == 0).
 * 
 * The nfs reqq lock cannot be held while we do the pru_send() because of a
 * lock ordering violation. The NFS client socket callback acquires 
 * inp_lock->nfsreq mutex and pru_send acquires inp_lock. So we drop the 
 * reqq mutex (and reacquire it after the pru_send()). The req structure
 * (for the rexmit) is prevented from being removed by the R_REXMIT_INPROG flag.
 */
void
nfs_timer(void *arg)
{
	struct nfsreq *rep;
	struct mbuf *m;
	struct socket *so;
	struct nfsmount *nmp;
	int timeo;
	int s, error;
	struct timeval now;

	getmicrouptime(&now);
	s = splnet();
	mtx_lock(&Giant);	/* nfs_down -> tprintf */
	mtx_lock(&nfs_reqq_mtx);
	TAILQ_FOREACH(rep, &nfs_reqq, r_chain) {
		nmp = rep->r_nmp;
		if (rep->r_mrep || (rep->r_flags & R_SOFTTERM))
			continue;
		if (nfs_sigintr(nmp, rep, rep->r_td))
			continue;
		if (nmp->nm_tprintf_initial_delay != 0 &&
		    (rep->r_rexmit > 2 || (rep->r_flags & R_RESENDERR)) &&
		    rep->r_lastmsg + nmp->nm_tprintf_delay < now.tv_sec) {
			rep->r_lastmsg = now.tv_sec;
			nfs_down(rep, nmp, rep->r_td, "not responding",
			    0, NFSSTA_TIMEO);
#if 0
			if (!(nmp->nm_state & NFSSTA_MOUNTED)) {
				/* we're not yet completely mounted and */
				/* we can't complete an RPC, so we fail */
				nfsstats.rpctimeouts++;
				nfs_softterm(rep);
				continue;
			}
#endif
		}
		if (rep->r_rtt >= 0) {
			rep->r_rtt++;
			if (nmp->nm_flag & NFSMNT_DUMBTIMR)
				timeo = nmp->nm_timeo;
			else
				timeo = NFS_RTO(nmp, proct[rep->r_procnum]);
			if (nmp->nm_timeouts > 0)
				timeo *= nfs_backoff[nmp->nm_timeouts - 1];
			if (rep->r_rtt <= timeo)
				continue;
			if (nmp->nm_timeouts < NFS_NBACKOFF)
				nmp->nm_timeouts++;
		}
		if (rep->r_rexmit >= rep->r_retry) {	/* too many */
			nfsstats.rpctimeouts++;
			nfs_softterm(rep);
			continue;
		}
		if (nmp->nm_sotype != SOCK_DGRAM) {
			if (++rep->r_rexmit > NFS_MAXREXMIT)
				rep->r_rexmit = NFS_MAXREXMIT;
			/*
			 * For NFS/TCP, setting R_MUSTRESEND and waking up 
			 * the requester will cause the request to be   
			 * retransmitted (in nfs_reply()), re-connecting
			 * if necessary.
			 */
			rep->r_flags |= R_MUSTRESEND;
			wakeup_nfsreq(rep);
			continue;
		}
		if ((so = nmp->nm_so) == NULL)
			continue;
		/*
		 * If there is enough space and the window allows..
		 *	Resend it
		 * Set r_rtt to -1 in case we fail to send it now.
		 */
		rep->r_rtt = -1;
		if (sbspace(&so->so_snd) >= rep->r_mreq->m_pkthdr.len &&
		   ((nmp->nm_flag & NFSMNT_DUMBTIMR) ||
		    (rep->r_flags & R_SENT) ||
		    nmp->nm_sent < nmp->nm_cwnd) &&
		   (m = m_copym(rep->r_mreq, 0, M_COPYALL, M_DONTWAIT))) {
			/*
			 * Mark the request to indicate that a XMIT is in progress
			 * to prevent the req structure being removed in nfs_request().
			 */
			rep->r_flags |= R_REXMIT_INPROG;
			mtx_unlock(&nfs_reqq_mtx);
			NET_LOCK_GIANT();
			if ((nmp->nm_flag & NFSMNT_NOCONN) == 0)
			    error = (*so->so_proto->pr_usrreqs->pru_send)
				    (so, 0, m, NULL, NULL, curthread);
			else
			    error = (*so->so_proto->pr_usrreqs->pru_send)
				    (so, 0, m, nmp->nm_nam, NULL, curthread);
			NET_UNLOCK_GIANT();
			mtx_lock(&nfs_reqq_mtx);
			rep->r_flags &= ~R_REXMIT_INPROG;
			wakeup((caddr_t)&rep->r_flags);
			if (error) {
				if (NFSIGNORE_SOERROR(nmp->nm_soflags, error))
					so->so_error = 0;
				rep->r_flags |= R_RESENDERR;
			} else {
				/*
				 * Iff first send, start timing
				 * else turn timing off, backoff timer
				 * and divide congestion window by 2.
				 */
				rep->r_flags &= ~R_RESENDERR;
				if (rep->r_flags & R_SENT) {
					rep->r_flags &= ~R_TIMING;
					if (++rep->r_rexmit > NFS_MAXREXMIT)
						rep->r_rexmit = NFS_MAXREXMIT;
					nmp->nm_cwnd >>= 1;
					if (nmp->nm_cwnd < NFS_CWNDSCALE)
						nmp->nm_cwnd = NFS_CWNDSCALE;
					nfsstats.rpcretries++;
				} else {
					rep->r_flags |= R_SENT;
					nmp->nm_sent += NFS_CWNDSCALE;
				}
				rep->r_rtt = 0;
			}
		}
	}
	mtx_unlock(&nfs_reqq_mtx);
	mtx_unlock(&Giant);	/* nfs_down -> tprintf */
	splx(s);
	callout_reset(&nfs_callout, nfs_ticks, nfs_timer, NULL);
}

/*
 * Mark all of an nfs mount's outstanding requests with R_SOFTTERM and
 * wait for all requests to complete. This is used by forced unmounts
 * to terminate any outstanding RPCs.
 */
int
nfs_nmcancelreqs(nmp)
	struct nfsmount *nmp;
{
	struct nfsreq *req;
	int i, s;

	s = splnet();
	mtx_lock(&nfs_reqq_mtx);
	TAILQ_FOREACH(req, &nfs_reqq, r_chain) {
		if (nmp != req->r_nmp || req->r_mrep != NULL ||
		    (req->r_flags & R_SOFTTERM))
			continue;
		nfs_softterm(req);
	}
	mtx_unlock(&nfs_reqq_mtx);
	splx(s);

	for (i = 0; i < 30; i++) {
		s = splnet();
		mtx_lock(&nfs_reqq_mtx);
		TAILQ_FOREACH(req, &nfs_reqq, r_chain) {
			if (nmp == req->r_nmp)
				break;
		}
		mtx_unlock(&nfs_reqq_mtx);
		splx(s);
		if (req == NULL)
			return (0);
		tsleep(&lbolt, PSOCK, "nfscancel", 0);
	}
	return (EBUSY);
}

/*
 * Flag a request as being about to terminate (due to NFSMNT_INT/NFSMNT_SOFT).
 * The nm_send count is decremented now to avoid deadlocks when the process in
 * soreceive() hasn't yet managed to send its own request.
 */

static void
nfs_softterm(struct nfsreq *rep)
{

	rep->r_flags |= R_SOFTTERM;
	if (rep->r_flags & R_SENT) {
		rep->r_nmp->nm_sent -= NFS_CWNDSCALE;
		rep->r_flags &= ~R_SENT;
	}
	/* 
	 * Request terminated, wakeup the blocked process, so that we
	 * can return EINTR back.
	 */
	wakeup_nfsreq(rep);
}

/*
 * Any signal that can interrupt an NFS operation in an intr mount
 * should be added to this set. SIGSTOP and SIGKILL cannot be masked.
 */
int nfs_sig_set[] = {
	SIGINT,
	SIGTERM,
	SIGHUP,
	SIGKILL,
	SIGSTOP,
	SIGQUIT
};

/*
 * Check to see if one of the signals in our subset is pending on
 * the process (in an intr mount).
 */
static int
nfs_sig_pending(sigset_t set)
{
	int i;
	
	for (i = 0 ; i < sizeof(nfs_sig_set)/sizeof(int) ; i++)
		if (SIGISMEMBER(set, nfs_sig_set[i]))
			return (1);
	return (0);
}
 
/*
 * The set/restore sigmask functions are used to (temporarily) overwrite
 * the process p_sigmask during an RPC call (for example). These are also
 * used in other places in the NFS client that might tsleep().
 */
void
nfs_set_sigmask(struct thread *td, sigset_t *oldset)
{
	sigset_t newset;
	int i;
	struct proc *p;
	
	SIGFILLSET(newset);
	if (td == NULL)
		td = curthread; /* XXX */
	p = td->td_proc;
	/* Remove the NFS set of signals from newset */
	PROC_LOCK(p);
	mtx_lock(&p->p_sigacts->ps_mtx);
	for (i = 0 ; i < sizeof(nfs_sig_set)/sizeof(int) ; i++) {
		/*
		 * But make sure we leave the ones already masked
		 * by the process, ie. remove the signal from the
		 * temporary signalmask only if it wasn't already
		 * in p_sigmask.
		 */
		if (!SIGISMEMBER(td->td_sigmask, nfs_sig_set[i]) &&
		    !SIGISMEMBER(p->p_sigacts->ps_sigignore, nfs_sig_set[i]))
			SIGDELSET(newset, nfs_sig_set[i]);
	}
	mtx_unlock(&p->p_sigacts->ps_mtx);
	PROC_UNLOCK(p);
	kern_sigprocmask(td, SIG_SETMASK, &newset, oldset, 0);
}

void
nfs_restore_sigmask(struct thread *td, sigset_t *set)
{
	if (td == NULL)
		td = curthread; /* XXX */
	kern_sigprocmask(td, SIG_SETMASK, set, NULL, 0);
}

/*
 * NFS wrapper to msleep(), that shoves a new p_sigmask and restores the
 * old one after msleep() returns.
 */
int
nfs_msleep(struct thread *td, void *ident, struct mtx *mtx, int priority, char *wmesg, int timo)
{
	sigset_t oldset;
	int error;
	struct proc *p;
	
	if ((priority & PCATCH) == 0)
		return msleep(ident, mtx, priority, wmesg, timo);
	if (td == NULL)
		td = curthread; /* XXX */
	nfs_set_sigmask(td, &oldset);
	error = msleep(ident, mtx, priority, wmesg, timo);
	nfs_restore_sigmask(td, &oldset);
	p = td->td_proc;
	return (error);
}

/*
 * NFS wrapper to tsleep(), that shoves a new p_sigmask and restores the
 * old one after tsleep() returns.
 */
int
nfs_tsleep(struct thread *td, void *ident, int priority, char *wmesg, int timo)
{
	sigset_t oldset;
	int error;
	struct proc *p;
	
	if ((priority & PCATCH) == 0)
		return tsleep(ident, priority, wmesg, timo);
	if (td == NULL)
		td = curthread; /* XXX */
	nfs_set_sigmask(td, &oldset);
	error = tsleep(ident, priority, wmesg, timo);
	nfs_restore_sigmask(td, &oldset);
	p = td->td_proc;
	return (error);
}

/*
 * Test for a termination condition pending on the process.
 * This is used for NFSMNT_INT mounts.
 */
int
nfs_sigintr(struct nfsmount *nmp, struct nfsreq *rep, struct thread *td)
{
	struct proc *p;
	sigset_t tmpset;

	if ((nmp->nm_flag & NFSMNT_NFSV4) != 0)
		return nfs4_sigintr(nmp, rep, td);
	if (rep && (rep->r_flags & R_SOFTTERM))
		return (EIO);
	/* Terminate all requests while attempting a forced unmount. */
	if (nmp->nm_mountp->mnt_kern_flag & MNTK_UNMOUNTF)
		return (EIO);
	if (!(nmp->nm_flag & NFSMNT_INT))
		return (0);
	if (td == NULL)
		return (0);

	p = td->td_proc;
	PROC_LOCK(p);
	tmpset = p->p_siglist;
	SIGSETNAND(tmpset, td->td_sigmask);
	mtx_lock(&p->p_sigacts->ps_mtx);
	SIGSETNAND(tmpset, p->p_sigacts->ps_sigignore);
	mtx_unlock(&p->p_sigacts->ps_mtx);
	if (SIGNOTEMPTY(p->p_siglist) && nfs_sig_pending(tmpset)) {
		PROC_UNLOCK(p);
		return (EINTR);
	}
	PROC_UNLOCK(p);

	return (0);
}

/*
 * Lock a socket against others.
 * Necessary for STREAM sockets to ensure you get an entire rpc request/reply
 * and also to avoid race conditions between the processes with nfs requests
 * in progress when a reconnect is necessary.
 */
int
nfs_sndlock(struct nfsreq *rep)
{
	int *statep = &rep->r_nmp->nm_state;
	struct thread *td;
	int error, slpflag = 0, slptimeo = 0;

	td = rep->r_td;
	if (rep->r_nmp->nm_flag & NFSMNT_INT)
		slpflag = PCATCH;
	while (*statep & NFSSTA_SNDLOCK) {
		error = nfs_sigintr(rep->r_nmp, rep, td);
		if (error)
			return (error);
		*statep |= NFSSTA_WANTSND;
		(void) tsleep(statep, slpflag | (PZERO - 1),
			"nfsndlck", slptimeo);
		if (slpflag == PCATCH) {
			slpflag = 0;
			slptimeo = 2 * hz;
		}
	}
	*statep |= NFSSTA_SNDLOCK;
	return (0);
}

/*
 * Unlock the stream socket for others.
 */
void
nfs_sndunlock(struct nfsreq *rep)
{
	int *statep = &rep->r_nmp->nm_state;

	if ((*statep & NFSSTA_SNDLOCK) == 0)
		panic("nfs sndunlock");
	*statep &= ~NFSSTA_SNDLOCK;
	if (*statep & NFSSTA_WANTSND) {
		*statep &= ~NFSSTA_WANTSND;
		wakeup(statep);
	}
}

/*
 *	nfs_realign:
 *
 *	Check for badly aligned mbuf data and realign by copying the unaligned
 *	portion of the data into a new mbuf chain and freeing the portions
 *	of the old chain that were replaced.
 *
 *	We cannot simply realign the data within the existing mbuf chain
 *	because the underlying buffers may contain other rpc commands and
 *	we cannot afford to overwrite them.
 *
 *	We would prefer to avoid this situation entirely.  The situation does
 *	not occur with NFS/UDP and is supposed to only occassionally occur
 *	with TCP.  Use vfs.nfs.realign_count and realign_test to check this.
 *
 */
static int
nfs_realign(struct mbuf **pm, int hsiz)
{
	struct mbuf *m;
	struct mbuf *n = NULL;
	int off = 0;

	++nfs_realign_test;
	while ((m = *pm) != NULL) {
		if ((m->m_len & 0x3) || (mtod(m, intptr_t) & 0x3)) {
			MGET(n, M_DONTWAIT, MT_DATA);
			if (n == NULL)
				return (ENOMEM);
			if (m->m_len >= MINCLSIZE) {
				MCLGET(n, M_DONTWAIT);
				if (n->m_ext.ext_buf == NULL) {
					m_freem(n);
					return (ENOMEM);
				}
			}
			n->m_len = 0;
			break;
		}
		pm = &m->m_next;
	}
	/*
	 * If n is non-NULL, loop on m copying data, then replace the
	 * portion of the chain that had to be realigned.
	 */
	if (n != NULL) {
		++nfs_realign_count;
		while (m) {
			m_copyback(n, off, m->m_len, mtod(m, caddr_t));
			off += m->m_len;
			m = m->m_next;
		}
		m_freem(*pm);
		*pm = n;
	}
	return (0);
}


static int
nfs_msg(struct thread *td, const char *server, const char *msg, int error)
{
	struct proc *p;

	GIANT_REQUIRED;	/* tprintf */

	p = td ? td->td_proc : NULL;
	if (error) {
		tprintf(p, LOG_INFO, "nfs server %s: %s, error %d\n", server,
		    msg, error);
	} else {
		tprintf(p, LOG_INFO, "nfs server %s: %s\n", server, msg);
	}
	return (0);
}

void
nfs_down(rep, nmp, td, msg, error, flags)
	struct nfsreq *rep;
	struct nfsmount *nmp;
	struct thread *td;
	const char *msg;
	int error, flags;
{

	GIANT_REQUIRED;	/* nfs_msg */

	if (nmp == NULL)
		return;
	if ((flags & NFSSTA_TIMEO) && !(nmp->nm_state & NFSSTA_TIMEO)) {
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESP, 0);
		nmp->nm_state |= NFSSTA_TIMEO;
	}
#ifdef NFSSTA_LOCKTIMEO
	if ((flags & NFSSTA_LOCKTIMEO) && !(nmp->nm_state & NFSSTA_LOCKTIMEO)) {
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESPLOCK, 0);
		nmp->nm_state |= NFSSTA_LOCKTIMEO;
	}
#endif
	if (rep)
		rep->r_flags |= R_TPRINTFMSG;
	nfs_msg(td, nmp->nm_mountp->mnt_stat.f_mntfromname, msg, error);
}

void
nfs_up(rep, nmp, td, msg, flags)
	struct nfsreq *rep;
	struct nfsmount *nmp;
	struct thread *td;
	const char *msg;
	int flags;
{

	GIANT_REQUIRED;	/* nfs_msg */

	if (nmp == NULL)
		return;
	if ((rep == NULL) || (rep->r_flags & R_TPRINTFMSG) != 0)
		nfs_msg(td, nmp->nm_mountp->mnt_stat.f_mntfromname, msg, 0);
	if ((flags & NFSSTA_TIMEO) && (nmp->nm_state & NFSSTA_TIMEO)) {
		nmp->nm_state &= ~NFSSTA_TIMEO;
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESP, 1);
	}
#ifdef NFSSTA_LOCKTIMEO
	if ((flags & NFSSTA_LOCKTIMEO) && (nmp->nm_state & NFSSTA_LOCKTIMEO)) {
		nmp->nm_state &= ~NFSSTA_LOCKTIMEO;
		vfs_event_signal(&nmp->nm_mountp->mnt_stat.f_fsid,
		    VQ_NOTRESPLOCK, 1);
	}
#endif
}

