/* drm_lists.h -- Buffer list handling routines -*- linux-c -*-
 * Created: Mon Apr 19 20:54:22 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 * $FreeBSD$
 */

#include "dev/drm/drmP.h"

#if __HAVE_DMA_WAITLIST

int DRM(waitlist_create)(drm_waitlist_t *bl, int count)
{
	if (bl->count)
		return DRM_ERR( EINVAL );

	bl->bufs       = DRM(alloc)((bl->count + 2) * sizeof(*bl->bufs),
				    DRM_MEM_BUFLISTS);

	if(!bl->bufs) return DRM_ERR(ENOMEM);

	bzero(bl->bufs, sizeof(*bl->bufs));

	bl->count      = count;
	bl->rp	       = bl->bufs;
	bl->wp	       = bl->bufs;
	bl->end	       = &bl->bufs[bl->count+1];
	DRM_SPININIT( bl->write_lock, "writelock" );
	DRM_SPININIT( bl->read_lock, "readlock" );
	return 0;
}

int DRM(waitlist_destroy)(drm_waitlist_t *bl)
{
	if (bl->rp != bl->wp)
		return DRM_ERR( EINVAL );
	if (bl->bufs) DRM(free)(bl->bufs,
				(bl->count + 2) * sizeof(*bl->bufs),
				DRM_MEM_BUFLISTS);
	bl->count = 0;
	bl->bufs  = NULL;
	bl->rp	  = NULL;
	bl->wp	  = NULL;
	bl->end	  = NULL;
	DRM_SPINUNINIT( bl->write_lock );
	DRM_SPINUNINIT( bl->read_lock );
	return 0;
}

int DRM(waitlist_put)(drm_waitlist_t *bl, drm_buf_t *buf)
{
	int	      left;
	int		s;
	left = DRM_LEFTCOUNT(bl);
	if (!left) {
		DRM_ERROR("Overflow while adding buffer %d from pid %d\n",
			  buf->idx, buf->pid);
		return DRM_ERR( EINVAL );
	}
#if __HAVE_DMA_HISTOGRAM
	getnanotime(&buf->time_queued);
#endif
	buf->list	 = DRM_LIST_WAIT;

	DRM_SPINLOCK(&bl->write_lock);
	s = spldrm();
	*bl->wp = buf;
	if (++bl->wp >= bl->end) bl->wp = bl->bufs;
	splx(s);
	DRM_SPINUNLOCK(&bl->write_lock);

	return 0;
}

drm_buf_t *DRM(waitlist_get)(drm_waitlist_t *bl)
{
	drm_buf_t     *buf;
	int		s;

	DRM_SPINLOCK(&bl->read_lock);
	s = spldrm();
	buf = *bl->rp;
	if (bl->rp == bl->wp) {
		splx(s);
		DRM_SPINUNLOCK(&bl->read_lock);
		return NULL;
	}				     
	if (++bl->rp >= bl->end) bl->rp = bl->bufs;
	splx(s);
	DRM_SPINUNLOCK(&bl->read_lock);
	
	return buf;
}

#endif /* __HAVE_DMA_WAITLIST */


#if __HAVE_DMA_FREELIST

int DRM(freelist_create)(drm_freelist_t *bl, int count)
{
	atomic_set(&bl->count, 0);
	bl->next      = NULL;
	bl->waiting = 0;

	bl->low_mark  = 0;
	bl->high_mark = 0;
	atomic_set(&bl->wfh,   0);
	DRM_SPININIT( bl->lock, "freelistlock" );
	++bl->initialized;
	return 0;
}

int DRM(freelist_destroy)(drm_freelist_t *bl)
{
	atomic_set(&bl->count, 0);
	bl->next = NULL;
	DRM_SPINUNINIT( bl->lock );
	return 0;
}

int DRM(freelist_put)(drm_device_t *dev, drm_freelist_t *bl, drm_buf_t *buf)
{
	drm_device_dma_t *dma  = dev->dma;

	if (!dma) {
		DRM_ERROR("No DMA support\n");
		return 1;
	}

	if (buf->waiting || buf->pending || buf->list == DRM_LIST_FREE) {
		DRM_ERROR("Freed buffer %d: w%d, p%d, l%d\n",
			  buf->idx, buf->waiting, buf->pending, buf->list);
	}
	if (!bl) return 1;
#if __HAVE_DMA_HISTOGRAM
	getnanotime(&buf->time_queued);
	DRM(histogram_compute)(dev, buf);
#endif
	buf->list	= DRM_LIST_FREE;

	DRM_SPINLOCK( &bl->lock );
	buf->next	= bl->next;
	bl->next	= buf;
	DRM_SPINUNLOCK( &bl->lock );

	atomic_inc(&bl->count);
	if (atomic_read(&bl->count) > dma->buf_count) {
		DRM_ERROR("%ld of %d buffers free after addition of %d\n",
			  (unsigned long)atomic_read(&bl->count), 
				dma->buf_count, buf->idx);
		return 1;
	}
				/* Check for high water mark */
	if (atomic_read(&bl->wfh) && atomic_read(&bl->count)>=bl->high_mark) {
		atomic_set(&bl->wfh, 0);
		DRM_WAKEUP_INT((void *)&bl->waiting);
	}
	return 0;
}

static drm_buf_t *DRM(freelist_try)(drm_freelist_t *bl)
{
	drm_buf_t	  *buf;

	if (!bl) return NULL;

				/* Get buffer */
	DRM_SPINLOCK(&bl->lock);
	if (!bl->next) {
		DRM_SPINUNLOCK(&bl->lock);
		return NULL;
	}
	buf	  = bl->next;
	bl->next  = bl->next->next;
	DRM_SPINUNLOCK(&bl->lock);

	atomic_dec(&bl->count);
	buf->next = NULL;
	buf->list = DRM_LIST_NONE;
	if (buf->waiting || buf->pending) {
		DRM_ERROR("Free buffer %d: w%d, p%d, l%d\n",
			  buf->idx, buf->waiting, buf->pending, buf->list);
	}

	return buf;
}

drm_buf_t *DRM(freelist_get)(drm_freelist_t *bl, int block)
{
	drm_buf_t	  *buf	= NULL;
	int	error;

	if (!bl || !bl->initialized) return NULL;

				/* Check for low water mark */
	if (atomic_read(&bl->count) <= bl->low_mark) /* Became low */
		atomic_set(&bl->wfh, 1);
	if (atomic_read(&bl->wfh)) {
		if (block) {
			for (;;) {
				if (!atomic_read(&bl->wfh)
				    && (buf = DRM(freelist_try(bl)))) break;
				error = tsleep((void *)&bl->waiting, PZERO|PCATCH,
					       "drmfg", 0);
				if (error)
					break;
			}
		}
		return buf;
	}

	return DRM(freelist_try)(bl);
}

#endif /* __HAVE_DMA_FREELIST */
