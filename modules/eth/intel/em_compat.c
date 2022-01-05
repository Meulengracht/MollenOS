/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 */
/*$FreeBSD$*/

#include "em_compat.h"

/* Backward compatibility for pre-11 */
#if __FreeBSD_version < 1100000

uint64_t
if_setbaudrate(struct ifnet *ifp, uint64_t baudrate)
{
	uint64_t oldbrate;

	oldbrate = ifp->if_baudrate;
	ifp->if_baudrate = baudrate;
	return (oldbrate);
}

uint64_t
if_getbaudrate(if_t ifp)
{
	return (((struct ifnet *)ifp)->if_baudrate);
}

int
if_setcapabilities(if_t ifp, int capabilities)
{
	((struct ifnet *)ifp)->if_capabilities = capabilities;
	return (0);
}

int
if_setcapabilitiesbit(if_t ifp, int setbit, int clearbit)
{
	((struct ifnet *)ifp)->if_capabilities |= setbit;
	((struct ifnet *)ifp)->if_capabilities &= ~clearbit;

	return (0);
}

int
if_getcapabilities(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_capabilities;
}

int 
if_setcapenable(if_t ifp, int capabilities)
{
	((struct ifnet *)ifp)->if_capenable = capabilities;
	return (0);
}

int 
if_setcapenablebit(if_t ifp, int setcap, int clearcap)
{
	if (setcap) 
		((struct ifnet *)ifp)->if_capenable |= setcap;
	if (clearcap)
		((struct ifnet *)ifp)->if_capenable &= ~clearcap;

	return (0);
}

const char *
if_getdname(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_dname;
}

int 
if_togglecapenable(if_t ifp, int togglecap)
{
	((struct ifnet *)ifp)->if_capenable ^= togglecap;
	return (0);
}

int
if_getcapenable(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_capenable;
}

int
if_setdev(if_t ifp, void *dev)
{
	return (0);
}

int
if_setdrvflagbits(if_t ifp, int set_flags, int clear_flags)
{
	((struct ifnet *)ifp)->if_drv_flags |= set_flags;
	((struct ifnet *)ifp)->if_drv_flags &= ~clear_flags;

	return (0);
}

int
if_getdrvflags(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_drv_flags;
}
 
int
if_setdrvflags(if_t ifp, int flags)
{
	((struct ifnet *)ifp)->if_drv_flags = flags;
	return (0);
}

int
if_setflags(if_t ifp, int flags)
{
	((struct ifnet *)ifp)->if_flags = flags;
	return (0);
}

int
if_setflagbits(if_t ifp, int set, int clear)
{
	((struct ifnet *)ifp)->if_flags |= set;
	((struct ifnet *)ifp)->if_flags &= ~clear;

	return (0);
}

int
if_getflags(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_flags;
}

int
if_clearhwassist(if_t ifp)
{
	((struct ifnet *)ifp)->if_hwassist = 0;
	return (0);
}

int
if_sethwassistbits(if_t ifp, int toset, int toclear)
{
	((struct ifnet *)ifp)->if_hwassist |= toset;
	((struct ifnet *)ifp)->if_hwassist &= ~toclear;

	return (0);
}

int
if_sethwassist(if_t ifp, int hwassist_bit)
{
	((struct ifnet *)ifp)->if_hwassist = hwassist_bit;
	return (0);
}

int
if_gethwassist(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_hwassist;
}

int
if_setmtu(if_t ifp, int mtu)
{
	((struct ifnet *)ifp)->if_mtu = mtu;
	return (0);
}

int
if_getmtu(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_mtu;
}

int
if_setsoftc(if_t ifp, void *softc)
{
	((struct ifnet *)ifp)->if_softc = softc;
	return (0);
}

void *
if_getsoftc(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_softc;
}

void 
if_setrcvif(struct mbuf *m, if_t ifp)
{
	m->m_pkthdr.rcvif = (struct ifnet *)ifp;
}

void 
if_setvtag(struct mbuf *m, uint16_t tag)
{
	m->m_pkthdr.ether_vtag = tag;	
}

uint16_t
if_getvtag(struct mbuf *m)
{
	return (m->m_pkthdr.ether_vtag);
}

int
if_sendq_empty(if_t ifp)
{
	return IFQ_DRV_IS_EMPTY(&((struct ifnet *)ifp)->if_snd);
}

struct ifaddr *
if_getifaddr(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_addr;
}

int
if_getamcount(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_amcount;
}

int
if_setsendqready(if_t ifp)
{
	IFQ_SET_READY(&((struct ifnet *)ifp)->if_snd);
	return (0);
}

int
if_setsendqlen(if_t ifp, int tx_desc_count)
{
	IFQ_SET_MAXLEN(&((struct ifnet *)ifp)->if_snd, tx_desc_count);
	((struct ifnet *)ifp)->if_snd.ifq_drv_maxlen = tx_desc_count;

	return (0);
}

int
if_vlantrunkinuse(if_t ifp)
{
	return ((struct ifnet *)ifp)->if_vlantrunk != NULL?1:0;
}

int
if_input(if_t ifp, struct mbuf* sendmp)
{
	(*((struct ifnet *)ifp)->if_input)((struct ifnet *)ifp, sendmp);
	return (0);
}

/* XXX */
#ifndef ETH_ADDR_LEN
#define ETH_ADDR_LEN 6
#endif

int 
if_setupmultiaddr(if_t ifp, void *mta, int *cnt, int max)
{
	struct ifmultiaddr *ifma;
	uint8_t *lmta = (uint8_t *)mta;
	int mcnt = 0;

	TAILQ_FOREACH(ifma, &((struct ifnet *)ifp)->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;

		if (mcnt == max)
			break;

		bcopy(LLADDR((struct sockaddr_dl *)ifma->ifma_addr),
		    &lmta[mcnt * ETH_ADDR_LEN], ETH_ADDR_LEN);
		mcnt++;
	}
	*cnt = mcnt;

	return (0);
}

int
if_multiaddr_array(if_t ifp, void *mta, int *cnt, int max)
{
	int error;

	if_maddr_rlock(ifp);
	error = if_setupmultiaddr(ifp, mta, cnt, max);
	if_maddr_runlock(ifp);
	return (error);
}

int
if_multiaddr_count(if_t ifp, int max)
{
	struct ifmultiaddr *ifma;
	int count;

	count = 0;
	if_maddr_rlock(ifp);
	TAILQ_FOREACH(ifma, &((struct ifnet *)ifp)->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		count++;
		if (count == max)
			break;
	}
	if_maddr_runlock(ifp);
	return (count);
}

struct mbuf *
if_dequeue(if_t ifp)
{
	struct mbuf *m;
	IFQ_DRV_DEQUEUE(&((struct ifnet *)ifp)->if_snd, m);

	return (m);
}

int
if_sendq_prepend(if_t ifp, struct mbuf *m)
{
	IFQ_DRV_PREPEND(&((struct ifnet *)ifp)->if_snd, m);
	return (0);
}

int
if_setifheaderlen(if_t ifp, int len)
{
	((struct ifnet *)ifp)->if_hdrlen = len;
	return (0);
}

caddr_t
if_getlladdr(if_t ifp)
{
	return (IF_LLADDR((struct ifnet *)ifp));
}

void *
if_gethandle(u_char type)
{
	return (if_alloc(type));
}

void
if_bpfmtap(if_t ifh, struct mbuf *m)
{
	struct ifnet *ifp = (struct ifnet *)ifh;

	BPF_MTAP(ifp, m);
}

void
if_etherbpfmtap(if_t ifh, struct mbuf *m)
{
	struct ifnet *ifp = (struct ifnet *)ifh;

	ETHER_BPF_MTAP(ifp, m);
}

void
if_vlancap(if_t ifh)
{
	struct ifnet *ifp = (struct ifnet *)ifh;
	VLAN_CAPABILITIES(ifp);
}

void
if_setinitfn(if_t ifp, void (*init_fn)(void *))
{
	((struct ifnet *)ifp)->if_init = init_fn;
}

void
if_setioctlfn(if_t ifp, int (*ioctl_fn)(if_t, u_long, caddr_t))
{
	((struct ifnet *)ifp)->if_ioctl = (void *)ioctl_fn;
}

void
if_setstartfn(if_t ifp, void (*start_fn)(if_t))
{
	((struct ifnet *)ifp)->if_start = (void *)start_fn;
}

#if __FreeBSD_version <= 1001000
/*
 * Adapted from net/if_var.h and net/if.c in post-10.1 releases for
 * compatibility with 10.1 and older versions.
 */
void
if_inc_counter(struct ifnet *ifp, ift_counter cnt, int64_t inc)
{
	switch (cnt) {
	case IFCOUNTER_IPACKETS:
		ifp->if_ipackets += inc;
		break;
	case IFCOUNTER_IERRORS:
		ifp->if_ierrors += inc;
		break;
	case IFCOUNTER_OPACKETS:
		ifp->if_opackets += inc;
		break;
	case IFCOUNTER_OERRORS:
		ifp->if_oerrors += inc;
		break;
	case IFCOUNTER_COLLISIONS:
		ifp->if_collisions += inc;
		break;
	case IFCOUNTER_IBYTES:
		ifp->if_ibytes += inc;
		break;
	case IFCOUNTER_OBYTES:
		ifp->if_obytes += inc;
		break;
	case IFCOUNTER_IMCASTS:
		ifp->if_imcasts += inc;
		break;
	case IFCOUNTER_OMCASTS:
		ifp->if_omcasts += inc;
		break;
	case IFCOUNTER_IQDROPS:
		ifp->if_iqdrops += inc;
		break;
	case IFCOUNTER_NOPROTO:
		ifp->if_noproto += inc;
		break;
	default:
		break;
	}
}

#endif /* __FreeBSD_version <= 1001000 */
#endif /* __FreeBSD_version < 1100000 */

