/*
 * mynet.c - a pseudo Ethernet driver.
 *
 * Companion source for Chapter 28 of
 * "FreeBSD Device Drivers: From First Steps to Kernel Mastery".
 *
 * This driver registers a clonable pseudo Ethernet interface called
 * mynet0 (or mynet1, mynet2, ...) with the ifnet framework. It accepts
 * outbound frames, taps BPF, updates counters, and frees them; it also
 * synthesises one ARP request per second on the inbound path so the
 * reader can observe a receive flow without needing real hardware.
 *
 * Compare with /usr/src/sys/net/if_disc.c for a minimal pseudo driver,
 * /usr/src/sys/net/if_epair.c for a clonable pair driver, and
 * /usr/src/sys/net/if_ethersubr.c for the Ethernet helpers this driver
 * relies on (ether_ifattach, ether_ifdetach, ether_input, ether_ioctl).
 *
 * The chapter develops this driver in layers:
 *
 *   Section 3: registration skeleton (clone, softc, ifnet, media).
 *   Section 4: transmit callback.
 *   Section 5: receive callout and if_input.
 *   Section 6: flags, link state, and media.
 *   Section 7: testing with standard tools.
 *   Section 8: clean detach and cloner teardown.
 *
 * The file below is the final unified form, ready to compile.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/callout.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/if_clone.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/bpf.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

static const char mynet_name[] = "mynet";
static MALLOC_DEFINE(M_MYNET, "mynet", "mynet pseudo Ethernet driver");

VNET_DEFINE_STATIC(struct if_clone *, mynet_cloner);
#define V_mynet_cloner	VNET(mynet_cloner)

struct mynet_softc {
	struct ifnet	*ifp;
	struct mtx	 mtx;
	uint8_t		 hwaddr[ETHER_ADDR_LEN];
	struct ifmedia	 media;
	struct callout	 rx_callout;
	int		 rx_interval_hz;
	bool		 running;
};

#define MYNET_LOCK(sc)		mtx_lock(&(sc)->mtx)
#define MYNET_UNLOCK(sc)	mtx_unlock(&(sc)->mtx)
#define MYNET_ASSERT(sc)	mtx_assert(&(sc)->mtx, MA_OWNED)

/* Forward declarations. */
static int	mynet_clone_create(struct if_clone *, int, caddr_t);
static void	mynet_clone_destroy(struct ifnet *);
static int	mynet_create_unit(int unit);
static void	mynet_destroy(struct mynet_softc *);
static void	mynet_init(void *);
static void	mynet_stop(struct mynet_softc *);
static int	mynet_transmit(struct ifnet *, struct mbuf *);
static void	mynet_qflush(struct ifnet *);
static int	mynet_ioctl(struct ifnet *, u_long, caddr_t);
static int	mynet_media_change(struct ifnet *);
static void	mynet_media_status(struct ifnet *, struct ifmediareq *);
static void	mynet_rx_timer(void *);
static void	mynet_rx_fake_arp(struct mynet_softc *);
static int	mynet_modevent(module_t, int, void *);
static void	vnet_mynet_init(const void *);
static void	vnet_mynet_uninit(const void *);

/*
 * Cloner dispatch. These two functions are thin wrappers that
 * delegate the real work. Keeping the cloner callbacks small is a
 * convention worth following because it makes the interesting code
 * in mynet_create_unit and mynet_destroy easy to isolate.
 */
static int
mynet_clone_create(struct if_clone *ifc __unused, int unit,
    caddr_t params __unused)
{

	return (mynet_create_unit(unit));
}

static void
mynet_clone_destroy(struct ifnet *ifp)
{

	mynet_destroy((struct mynet_softc *)ifp->if_softc);
}

/*
 * Per-unit creation. This is where the softc is allocated, the ifnet
 * is filled in, the media table is constructed, and the interface is
 * handed to ether_ifattach for registration with the stack.
 */
static int
mynet_create_unit(int unit)
{
	struct mynet_softc *sc;
	struct ifnet *ifp;

	sc = malloc(sizeof(*sc), M_MYNET, M_WAITOK | M_ZERO);
	ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		free(sc, M_MYNET);
		return (ENOSPC);
	}
	sc->ifp = ifp;
	mtx_init(&sc->mtx, "mynet", NULL, MTX_DEF);

	arc4rand(sc->hwaddr, ETHER_ADDR_LEN, 0);
	sc->hwaddr[0] = 0x02;	/* locally administered, unicast */

	if_initname(ifp, mynet_name, unit);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	ifp->if_capenable = IFCAP_VLAN_MTU;
	ifp->if_transmit = mynet_transmit;
	ifp->if_qflush = mynet_qflush;
	ifp->if_ioctl = mynet_ioctl;
	ifp->if_init = mynet_init;
	ifp->if_baudrate = IF_Gbps(1);

	ifmedia_init(&sc->media, 0, mynet_media_change, mynet_media_status);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->media, IFM_ETHER | IFM_AUTO);

	callout_init_mtx(&sc->rx_callout, &sc->mtx, 0);
	sc->rx_interval_hz = hz;

	ether_ifattach(ifp, sc->hwaddr);
	return (0);
}

/*
 * Destruction mirrors creation in reverse: stop the activity, drain
 * the callout, detach from the stack, free the ifnet, tear down the
 * media list, destroy the mutex, and release the softc.
 */
static void
mynet_destroy(struct mynet_softc *sc)
{
	struct ifnet *ifp = sc->ifp;

	MYNET_LOCK(sc);
	sc->running = false;
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	MYNET_UNLOCK(sc);

	callout_drain(&sc->rx_callout);

	ether_ifdetach(ifp);
	if_free(ifp);

	ifmedia_removeall(&sc->media);
	mtx_destroy(&sc->mtx);
	free(sc, M_MYNET);
}

/*
 * Init and stop handle the transition between "not running" and
 * "running". Link state changes are notified with the mutex dropped
 * to avoid lock order inversions with the routing subsystem.
 */
static void
mynet_init(void *arg)
{
	struct mynet_softc *sc = arg;

	MYNET_LOCK(sc);
	sc->running = true;
	sc->ifp->if_drv_flags |= IFF_DRV_RUNNING;
	sc->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	callout_reset(&sc->rx_callout, sc->rx_interval_hz,
	    mynet_rx_timer, sc);
	MYNET_UNLOCK(sc);

	if_link_state_change(sc->ifp, LINK_STATE_UP);
}

static void
mynet_stop(struct mynet_softc *sc)
{

	MYNET_LOCK(sc);
	sc->running = false;
	sc->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	callout_stop(&sc->rx_callout);
	MYNET_UNLOCK(sc);

	if_link_state_change(sc->ifp, LINK_STATE_DOWN);
}

/*
 * Transmit callback. Receives an mbuf, taps BPF, updates counters,
 * and frees the mbuf. A real driver would queue the mbuf for hardware
 * DMA; a pseudo driver just drops the frame on the floor after
 * accounting for it.
 */
static int
mynet_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct mynet_softc *sc = ifp->if_softc;
	int len;

	if (m == NULL)
		return (0);
	M_ASSERTPKTHDR(m);

	if (m->m_pkthdr.len >
	    (ifp->if_mtu + sizeof(struct ether_vlan_header))) {
		m_freem(m);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return (E2BIG);
	}

	if ((ifp->if_flags & IFF_UP) == 0 ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    !sc->running) {
		m_freem(m);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		return (ENETDOWN);
	}

	BPF_MTAP(ifp, m);

	len = m->m_pkthdr.len;
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_OBYTES, len);
	if (m->m_flags & (M_BCAST | M_MCAST))
		if_inc_counter(ifp, IFCOUNTER_OMCASTS, 1);

	m_freem(m);
	return (0);
}

static void
mynet_qflush(struct ifnet *ifp __unused)
{

	/*
	 * No internal queue to flush. A real driver would drain any
	 * pending mbufs that had not yet reached hardware.
	 */
}

/*
 * Receive timer. Fires once per second by default, reschedules itself,
 * and synthesises an ARP request to demonstrate the receive path.
 */
static void
mynet_rx_timer(void *arg)
{
	struct mynet_softc *sc = arg;

	MYNET_ASSERT(sc);
	if (!sc->running)
		return;
	callout_reset(&sc->rx_callout, sc->rx_interval_hz,
	    mynet_rx_timer, sc);
	MYNET_UNLOCK(sc);

	mynet_rx_fake_arp(sc);

	MYNET_LOCK(sc);
}

/*
 * Build a broadcast ARP request that looks as if it had arrived on the
 * wire, tap BPF, update counters, and hand it to the stack.
 */
static void
mynet_rx_fake_arp(struct mynet_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	struct mbuf *m;
	struct ether_header *eh;
	struct arphdr *ah;
	uint8_t *payload;
	size_t frame_len;

	frame_len = sizeof(*eh) + sizeof(*ah) + 2 * (ETHER_ADDR_LEN + 4);
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
		return;
	}
	m->m_pkthdr.len = m->m_len = frame_len;
	m->m_pkthdr.rcvif = ifp;

	eh = mtod(m, struct ether_header *);
	memset(eh->ether_dhost, 0xff, ETHER_ADDR_LEN);
	memcpy(eh->ether_shost, sc->hwaddr, ETHER_ADDR_LEN);
	eh->ether_type = htons(ETHERTYPE_ARP);

	ah = (struct arphdr *)(eh + 1);
	ah->ar_hrd = htons(ARPHRD_ETHER);
	ah->ar_pro = htons(ETHERTYPE_IP);
	ah->ar_hln = ETHER_ADDR_LEN;
	ah->ar_pln = 4;
	ah->ar_op  = htons(ARPOP_REQUEST);

	payload = (uint8_t *)(ah + 1);
	memcpy(payload, sc->hwaddr, ETHER_ADDR_LEN);
	payload += ETHER_ADDR_LEN;
	memset(payload, 0, 4);
	payload += 4;
	memset(payload, 0, ETHER_ADDR_LEN);
	payload += ETHER_ADDR_LEN;
	memcpy(payload, "\xc0\x00\x02\x63", 4);

	BPF_MTAP(ifp, m);
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	if_inc_counter(ifp, IFCOUNTER_IBYTES, frame_len);
	if_inc_counter(ifp, IFCOUNTER_IMCASTS, 1);

	if_input(ifp, m);
}

/*
 * Ioctl handler. Translates administrative requests (SIOCSIFFLAGS,
 * SIOCSIFMTU, SIOCGIFMEDIA, ...) into driver actions. Unknown ioctls
 * are delegated to ether_ioctl, which handles most of the common ones
 * for Ethernet interfaces.
 */
static int
mynet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct mynet_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		MYNET_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
				MYNET_UNLOCK(sc);
				mynet_init(sc);
				MYNET_LOCK(sc);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				MYNET_UNLOCK(sc);
				mynet_stop(sc);
				MYNET_LOCK(sc);
			}
		}
		MYNET_UNLOCK(sc);
		break;

	case SIOCSIFMTU:
		if (ifr->ifr_mtu < 68 || ifr->ifr_mtu > 9216) {
			error = EINVAL;
			break;
		}
		ifp->if_mtu = ifr->ifr_mtu;
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/*
		 * Real hardware would program a multicast filter here.
		 * Our pseudo driver accepts the request silently.
		 */
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

/*
 * Media callbacks. We always report the maximum advertised media type
 * because a pseudo interface has no real link rate to negotiate.
 */
static int
mynet_media_change(struct ifnet *ifp __unused)
{

	return (0);
}

static void
mynet_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct mynet_softc *sc = ifp->if_softc;

	imr->ifm_status = IFM_AVALID;
	if (sc->running)
		imr->ifm_status |= IFM_ACTIVE;
	imr->ifm_active = IFM_ETHER | IFM_1000_T | IFM_FDX;
}

/*
 * Cloner registration per VNET. Each VNET receives its own cloner, so
 * jails with private network stacks can create their own mynet
 * interfaces independently of the host.
 */
static void
vnet_mynet_init(const void *unused __unused)
{

	V_mynet_cloner = if_clone_simple(mynet_name, mynet_clone_create,
	    mynet_clone_destroy, 0);
}
VNET_SYSINIT(vnet_mynet_init, SI_SUB_PSEUDO, SI_ORDER_ANY,
    vnet_mynet_init, NULL);

static void
vnet_mynet_uninit(const void *unused __unused)
{

	if_clone_detach(V_mynet_cloner);
}
VNET_SYSUNINIT(vnet_mynet_uninit, SI_SUB_INIT_IF, SI_ORDER_ANY,
    vnet_mynet_uninit, NULL);

/*
 * Module lifecycle. All the per-VNET work happens in the SYSINIT and
 * SYSUNINIT pair above; the module handler itself only needs to
 * acknowledge MOD_LOAD and MOD_UNLOAD.
 */
static int
mynet_modevent(module_t mod __unused, int type, void *data __unused)
{

	switch (type) {
	case MOD_LOAD:
	case MOD_UNLOAD:
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t mynet_mod = {
	"mynet",
	mynet_modevent,
	NULL
};

DECLARE_MODULE(mynet, mynet_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_DEPEND(mynet, ether, 1, 1, 1);
MODULE_VERSION(mynet, 1);
