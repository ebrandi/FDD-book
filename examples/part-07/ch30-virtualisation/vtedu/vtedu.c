/*
 * vtedu.c - A pedagogical VirtIO character device driver.
 *
 * Chapter 30 Case Study.  Designed for teaching, not for production.
 * The driver:
 *
 *   - Advertises a reserved VirtIO device ID (VIRTIO_ID_EDU).
 *   - Negotiates one feature bit (VTEDU_F_UPPERCASE).
 *   - Allocates a single virtqueue.
 *   - Exposes /dev/vteduN through make_dev(9).
 *   - On write, submits the user-space bytes to the virtqueue.
 *   - On read, returns whatever the backend put back.
 *
 * This is the driver described in Chapter 30's closing case study.
 * A matching backend in bhyve(8) is required for it to do anything
 * useful; without one, probe will not match any device.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sglist.h>
#include <sys/bus.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>

#define	VIRTIO_ID_EDU		0xfff0	/* hypothetical reserved ID */

#define	VTEDU_F_UPPERCASE	(1ULL << 0)
#define	VTEDU_FEATURES		(VIRTIO_F_VERSION_1 | VTEDU_F_UPPERCASE)

#define	VTEDU_BUF_SIZE		256

struct vtedu_softc {
	device_t		 dev;
	struct virtqueue	*vq;
	uint64_t		 features;
	struct mtx		 lock;
	struct cdev		*cdev;
	struct sglist		*sg;
	char			 buf[VTEDU_BUF_SIZE];
	size_t			 buf_len;
	bool			 response_ready;
};

static int	vtedu_probe(device_t);
static int	vtedu_attach(device_t);
static int	vtedu_detach(device_t);

static int	vtedu_negotiate_features(struct vtedu_softc *);
static int	vtedu_alloc_virtqueue(struct vtedu_softc *);
static int	vtedu_submit_locked(struct vtedu_softc *);
static void	vtedu_vq_intr(void *);

static d_open_t		vtedu_open;
static d_read_t		vtedu_read;
static d_write_t	vtedu_write;

static struct virtio_feature_desc vtedu_feature_descs[] = {
	{ VTEDU_F_UPPERCASE, "Uppercase" },
	{ 0, NULL }
};

static struct cdevsw vtedu_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	vtedu_open,
	.d_read =	vtedu_read,
	.d_write =	vtedu_write,
	.d_name =	"vtedu",
};

static device_method_t vtedu_methods[] = {
	DEVMETHOD(device_probe,		vtedu_probe),
	DEVMETHOD(device_attach,	vtedu_attach),
	DEVMETHOD(device_detach,	vtedu_detach),
	DEVMETHOD_END
};

static driver_t vtedu_driver = {
	"vtedu",
	vtedu_methods,
	sizeof(struct vtedu_softc)
};

VIRTIO_SIMPLE_PNPINFO(virtio_edu, VIRTIO_ID_EDU,
    "VirtIO Educational Device");
VIRTIO_DRIVER_MODULE(virtio_edu, vtedu_driver, NULL, NULL);
MODULE_VERSION(virtio_edu, 1);
MODULE_DEPEND(virtio_edu, virtio, 1, 1, 1);

static int
vtedu_probe(device_t dev)
{
	return (VIRTIO_SIMPLE_PROBE(dev, virtio_edu));
}

static int
vtedu_attach(device_t dev)
{
	struct vtedu_softc *sc = device_get_softc(dev);
	int error;

	sc->dev = dev;
	mtx_init(&sc->lock, device_get_nameunit(dev), NULL, MTX_DEF);

	virtio_set_feature_desc(dev, vtedu_feature_descs);

	error = vtedu_negotiate_features(sc);
	if (error != 0)
		goto fail;

	error = vtedu_alloc_virtqueue(sc);
	if (error != 0)
		goto fail;

	error = virtio_setup_intr(dev, INTR_TYPE_MISC);
	if (error != 0)
		goto fail;

	sc->sg = sglist_alloc(2, M_WAITOK);
	sc->cdev = make_dev(&vtedu_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_WHEEL, 0600, "vtedu%d", device_get_unit(dev));
	if (sc->cdev == NULL) {
		error = ENXIO;
		goto fail;
	}
	sc->cdev->si_drv1 = sc;

	device_printf(dev, "attached (features=0x%lx)\n",
	    (unsigned long)sc->features);
	return (0);

fail:
	vtedu_detach(dev);
	return (error);
}

static int
vtedu_detach(device_t dev)
{
	struct vtedu_softc *sc = device_get_softc(dev);

	if (sc->cdev != NULL) {
		destroy_dev(sc->cdev);
		sc->cdev = NULL;
	}
	if (sc->sg != NULL) {
		sglist_free(sc->sg);
		sc->sg = NULL;
	}
	virtio_stop(dev);
	if (mtx_initialized(&sc->lock))
		mtx_destroy(&sc->lock);
	return (0);
}

static int
vtedu_negotiate_features(struct vtedu_softc *sc)
{
	uint64_t features = VTEDU_FEATURES;

	sc->features = virtio_negotiate_features(sc->dev, features);
	return (virtio_finalize_features(sc->dev));
}

static int
vtedu_alloc_virtqueue(struct vtedu_softc *sc)
{
	struct vq_alloc_info vq_info;

	VQ_ALLOC_INFO_INIT(&vq_info, 0, vtedu_vq_intr, sc, &sc->vq,
	    "%s request", device_get_nameunit(sc->dev));

	return (virtio_alloc_virtqueues(sc->dev, 0, 1, &vq_info));
}

static int
vtedu_open(struct cdev *dev __unused, int oflags __unused, int devtype __unused,
    struct thread *td __unused)
{
	return (0);
}

static int
vtedu_write(struct cdev *dev, struct uio *uio, int flags __unused)
{
	struct vtedu_softc *sc = dev->si_drv1;
	size_t n;
	int error;

	n = uio->uio_resid;
	if (n == 0 || n > VTEDU_BUF_SIZE)
		return (EINVAL);

	mtx_lock(&sc->lock);
	error = uiomove(sc->buf, n, uio);
	if (error == 0) {
		sc->buf_len = n;
		sc->response_ready = false;
		error = vtedu_submit_locked(sc);
	}
	mtx_unlock(&sc->lock);
	return (error);
}

static int
vtedu_read(struct cdev *dev, struct uio *uio, int flags __unused)
{
	struct vtedu_softc *sc = dev->si_drv1;
	int error;

	mtx_lock(&sc->lock);
	while (!sc->response_ready) {
		error = mtx_sleep(sc, &sc->lock, PCATCH, "vteduR", 0);
		if (error != 0) {
			mtx_unlock(&sc->lock);
			return (error);
		}
	}
	error = uiomove(sc->buf,
	    MIN(sc->buf_len, (size_t)uio->uio_resid), uio);
	sc->buf_len = 0;
	sc->response_ready = false;
	mtx_unlock(&sc->lock);
	return (error);
}

static int
vtedu_submit_locked(struct vtedu_softc *sc)
{
	int error;

	mtx_assert(&sc->lock, MA_OWNED);

	sglist_reset(sc->sg);
	error = sglist_append(sc->sg, sc->buf, sc->buf_len);
	if (error != 0)
		return (error);

	error = virtqueue_enqueue(sc->vq, sc, sc->sg, 1, 1);
	if (error != 0)
		return (error);

	virtqueue_notify(sc->vq);
	return (0);
}

static void
vtedu_vq_intr(void *arg)
{
	struct vtedu_softc *sc = arg;
	void *cookie;
	uint32_t len;

	mtx_lock(&sc->lock);
	while ((cookie = virtqueue_dequeue(sc->vq, &len)) != NULL) {
		sc->buf_len = len;
		sc->response_ready = true;
		wakeup(sc);
	}
	mtx_unlock(&sc->lock);
}
