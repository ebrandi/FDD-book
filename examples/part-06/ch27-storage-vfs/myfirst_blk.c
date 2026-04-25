/*
 * myfirst_blk.c - a pseudo block device driver.
 *
 * Companion source for Chapter 27 of
 * "FreeBSD Device Drivers: From First Steps to Kernel Mastery".
 *
 * This driver registers a single pseudo disk called myblk0 with
 * the g_disk subsystem. It serves BIO requests out of a flat
 * kernel memory buffer whose size is set by MYBLK_MEDIASIZE.
 * Compare with /usr/src/sys/dev/md/md.c for a production-grade
 * pseudo block device driver.
 *
 * The chapter develops this driver in three steps:
 *
 *   Section 3: registration skeleton (no I/O yet).
 *   Section 4: GEOM-backed provider integration.
 *   Section 5: strategy function with a flat backing store.
 *
 * The file below is the Section 5 version. Earlier sections of
 * the chapter use intermediate forms kept inside the chapter
 * prose; this file is the ready-to-compile final form.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>

#include <geom/geom.h>
#include <geom/geom_disk.h>

#define MYBLK_NAME      "myblk"
#define MYBLK_SECTOR    512
#define MYBLK_MEDIASIZE (32ULL * 1024 * 1024)   /* 32 MiB */

struct myblk_softc {
	struct disk	*disk;
	struct mtx	 lock;
	u_int		 unit;
	uint8_t		*backing;
	size_t		 backing_size;
};

static MALLOC_DEFINE(M_MYBLK, "myblk", "myfirst_blk driver state");

static struct myblk_softc *myblk_unit0;

static void
myblk_strategy(struct bio *bp)
{
	struct myblk_softc *sc;
	off_t offset;
	size_t len;

	sc = bp->bio_disk->d_drv1;
	offset = bp->bio_offset;
	len = bp->bio_bcount;

	/* Defensive range check. GEOM will not normally send us */
	/* out-of-range requests, but a cheap guard prevents any */
	/* upstream bug from corrupting kernel memory.           */
	if (offset < 0 ||
	    (size_t)offset > sc->backing_size ||
	    len > sc->backing_size - (size_t)offset) {
		bp->bio_error = EIO;
		bp->bio_flags |= BIO_ERROR;
		bp->bio_resid = len;
		biodone(bp);
		return;
	}

	switch (bp->bio_cmd) {
	case BIO_READ:
		mtx_lock(&sc->lock);
		memcpy(bp->bio_data, sc->backing + offset, len);
		mtx_unlock(&sc->lock);
		bp->bio_resid = 0;
		break;

	case BIO_WRITE:
		mtx_lock(&sc->lock);
		memcpy(sc->backing + offset, bp->bio_data, len);
		mtx_unlock(&sc->lock);
		bp->bio_resid = 0;
		break;

	case BIO_DELETE:
		mtx_lock(&sc->lock);
		memset(sc->backing + offset, 0, len);
		mtx_unlock(&sc->lock);
		bp->bio_resid = 0;
		break;

	case BIO_FLUSH:
		/*
		 * Backing store is RAM. No ordering work is needed.
		 */
		bp->bio_resid = 0;
		break;

	default:
		bp->bio_error = EOPNOTSUPP;
		bp->bio_flags |= BIO_ERROR;
		bp->bio_resid = len;
		break;
	}

	biodone(bp);
}

static int
myblk_attach_unit(struct myblk_softc *sc)
{

	sc->backing_size = MYBLK_MEDIASIZE;
	sc->backing = malloc(sc->backing_size, M_MYBLK,
	    M_WAITOK | M_ZERO);

	sc->disk = disk_alloc();
	sc->disk->d_name	= MYBLK_NAME;
	sc->disk->d_unit	= sc->unit;
	sc->disk->d_strategy	= myblk_strategy;
	sc->disk->d_sectorsize	= MYBLK_SECTOR;
	sc->disk->d_mediasize	= MYBLK_MEDIASIZE;
	sc->disk->d_maxsize	= MAXPHYS;
	sc->disk->d_flags	= DISKFLAG_CANDELETE |
				  DISKFLAG_CANFLUSHCACHE;
	sc->disk->d_drv1	= sc;

	disk_create(sc->disk, DISK_VERSION);
	return (0);
}

static void
myblk_detach_unit(struct myblk_softc *sc)
{

	/*
	 * Order matters. Destroy the disk first to guarantee no
	 * BIO is in flight, then free the backing store.
	 */
	if (sc->disk != NULL) {
		disk_destroy(sc->disk);
		sc->disk = NULL;
	}
	if (sc->backing != NULL) {
		free(sc->backing, M_MYBLK);
		sc->backing = NULL;
		sc->backing_size = 0;
	}
}

static int
myblk_loader(struct module *m, int what, void *arg)
{
	struct myblk_softc *sc;
	int error;

	switch (what) {
	case MOD_LOAD:
		sc = malloc(sizeof(*sc), M_MYBLK, M_WAITOK | M_ZERO);
		mtx_init(&sc->lock, "myblk lock", NULL, MTX_DEF);
		sc->unit = 0;
		error = myblk_attach_unit(sc);
		if (error != 0) {
			mtx_destroy(&sc->lock);
			free(sc, M_MYBLK);
			return (error);
		}
		myblk_unit0 = sc;
		printf("myblk: loaded, /dev/%s%u size=%jd bytes\n",
		    MYBLK_NAME, sc->unit,
		    (intmax_t)sc->disk->d_mediasize);
		return (0);

	case MOD_UNLOAD:
		sc = myblk_unit0;
		if (sc == NULL)
			return (0);
		myblk_detach_unit(sc);
		mtx_destroy(&sc->lock);
		free(sc, M_MYBLK);
		myblk_unit0 = NULL;
		printf("myblk: unloaded\n");
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t myblk_mod = {
	"myblk",
	myblk_loader,
	NULL
};

DECLARE_MODULE(myblk, myblk_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(myblk, 1);
MODULE_DEPEND(myblk, g_disk, 0, 0, 0);
