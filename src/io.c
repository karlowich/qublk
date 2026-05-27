#define _GNU_SOURCE
#include "io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <liburing.h>
#include <libxnvme.h>

#ifndef IORING_SETUP_SINGLE_ISSUER
#define IORING_SETUP_SINGLE_ISSUER (1U << 12)
#endif
#ifndef IORING_SETUP_DEFER_TASKRUN
#define IORING_SETUP_DEFER_TASKRUN (1U << 13)
#endif

static int
init_ring(struct qublk_queue *q)
{
	struct io_uring_params p = {0};
	int rc;

	p.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN;
	rc = io_uring_queue_init_params(q->depth, &q->ring, &p);
	if (rc == -EINVAL) {
		memset(&p, 0, sizeof(p));
		rc = io_uring_queue_init_params(q->depth, &q->ring, &p);
	}
	return rc;
}

static void
prep_io_uring_cmd(struct io_uring_sqe *sqe, uint32_t cmd_op, const struct ublksrv_io_cmd *cmd,
		  uint64_t user_data)
{
	io_uring_prep_rw(IORING_OP_URING_CMD, sqe, 0, NULL, 0, 0);
	sqe->flags = IOSQE_FIXED_FILE;
	sqe->cmd_op = cmd_op;
	memcpy(sqe->cmd, cmd, sizeof(*cmd));
	sqe->user_data = user_data;
}

static int
submit_fetch(struct qublk_queue *q, struct qublk_io *io)
{
	struct io_uring_sqe *sqe;
	struct ublksrv_io_cmd cmd = {
		.q_id = q->q_id,
		.tag = io->tag,
		.result = -1,
		.addr = (uint64_t)(uintptr_t)io->buf,
	};

	sqe = io_uring_get_sqe(&q->ring);
	if (!sqe) {
		return -EAGAIN;
	}
	prep_io_uring_cmd(sqe, UBLK_U_IO_FETCH_REQ, &cmd, io->tag);
	return 0;
}

static int
submit_commit_and_fetch(struct qublk_queue *q, struct qublk_io *io, int result)
{
	struct io_uring_sqe *sqe;
	struct ublksrv_io_cmd cmd = {
		.q_id = q->q_id,
		.tag = io->tag,
		.result = result,
		.addr = (uint64_t)(uintptr_t)io->buf,
	};

	sqe = io_uring_get_sqe(&q->ring);
	if (!sqe) {
		return -EAGAIN;
	}
	prep_io_uring_cmd(sqe, UBLK_U_IO_COMMIT_AND_FETCH_REQ, &cmd, io->tag);
	return 0;
}

static void
on_xnvme_complete(struct xnvme_cmd_ctx *ctx, void *opaque)
{
	struct qublk_io *io = opaque;
	struct qublk_queue *q = io->q;
	uint8_t op;
	int result;

	if (xnvme_cmd_ctx_cpl_status(ctx)) {
		result = -EIO;
	} else {
		op = ublksrv_get_op(io->iod);
		if (op == UBLK_IO_OP_READ || op == UBLK_IO_OP_WRITE) {
			result = (int)(io->iod->nr_sectors << 9);
		} else {
			result = 0;
		}
	}

	xnvme_queue_put_cmd_ctx(q->xq, ctx);
	submit_commit_and_fetch(q, io, result);
}

static int
dispatch(struct qublk_queue *q, struct qublk_io *io)
{
	struct qublk_dev *dev = q->dev;
	const struct ublksrv_io_desc *iod = io->iod;
	struct xnvme_cmd_ctx *ctx;
	uint64_t bytes, slba;
	uint32_t nsid = xnvme_dev_get_nsid(dev->xdev);
	uint16_t nlb;
	uint8_t op = ublksrv_get_op(iod);
	uint8_t lba_shift = dev->lba_shift;
	int rc;

	if (op != UBLK_IO_OP_FLUSH) {
		bytes = (uint64_t)iod->nr_sectors << 9;
		if (bytes > dev->max_io_buf) {
			fprintf(stderr, "qublk: tag %u: I/O %lu B exceeds max_io_buf %u\n",
				io->tag, (unsigned long)bytes, dev->max_io_buf);
			return submit_commit_and_fetch(q, io, -EINVAL);
		}
	}

	ctx = xnvme_queue_get_cmd_ctx(q->xq);
	if (!ctx) {
		return -EBUSY;
	}
	xnvme_cmd_ctx_set_cb(ctx, on_xnvme_complete, io);

	switch (op) {
	case UBLK_IO_OP_READ:
		slba = iod->start_sector >> (lba_shift - 9);
		nlb = (uint16_t)(((iod->nr_sectors << 9) >> lba_shift) - 1);
		rc = xnvme_nvm_read(ctx, nsid, slba, nlb, io->buf, NULL);
		break;
	case UBLK_IO_OP_WRITE:
		slba = iod->start_sector >> (lba_shift - 9);
		nlb = (uint16_t)(((iod->nr_sectors << 9) >> lba_shift) - 1);
		rc = xnvme_nvm_write(ctx, nsid, slba, nlb, io->buf, NULL);
		break;
	case UBLK_IO_OP_FLUSH:
		xnvme_prep_nvm(ctx, XNVME_SPEC_NVM_OPC_FLUSH, nsid, 0, 0);
		rc = xnvme_cmd_pass(ctx, NULL, 0, NULL, 0);
		break;
	default:
		xnvme_queue_put_cmd_ctx(q->xq, ctx);
		return submit_commit_and_fetch(q, io, -EOPNOTSUPP);
	}

	if (rc == -EBUSY || rc == -EAGAIN) {
		xnvme_queue_put_cmd_ctx(q->xq, ctx);
		return rc;
	}
	if (rc < 0) {
		xnvme_queue_put_cmd_ctx(q->xq, ctx);
		return submit_commit_and_fetch(q, io, rc);
	}
	return 0;
}

static int
handle_ublk_cqe(struct qublk_queue *q, struct io_uring_cqe *cqe)
{
	struct qublk_io *io;
	uint16_t tag = (uint16_t)cqe->user_data;
	int rc;

	if (tag >= q->depth) {
		fprintf(stderr, "qublk: bogus tag %u in cqe\n", tag);
		return -EINVAL;
	}
	io = &q->ios[tag];

	if (cqe->res == UBLK_IO_RES_OK) {
		rc = dispatch(q, io);
		while (rc == -EBUSY || rc == -EAGAIN) {
			xnvme_queue_poke(q->xq, 0);
			rc = dispatch(q, io);
		}
		return rc;
	}
	if (cqe->res == UBLK_IO_RES_ABORT || cqe->res == -ENODEV) {
		q->dev->stop = 1;
		return 0;
	}
	fprintf(stderr, "qublk: tag %u unexpected fetch res %d\n", tag, cqe->res);
	q->dev->stop = 1;
	return 0;
}

int
qublk_io_init(struct qublk_dev *dev)
{
	struct qublk_queue *q = &dev->q;
	char path[64];
	int rc;

	q->dev = dev;
	q->q_id = 0;
	q->depth = dev->depth;
	q->ublkc_fd = -1;
	q->ios = NULL;
	q->iod_arr = NULL;
	q->xq = NULL;

	snprintf(path, sizeof(path), "/dev/ublkc%d", dev->dev_id);
	q->ublkc_fd = open(path, O_RDWR | O_CLOEXEC);
	if (q->ublkc_fd < 0) {
		fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
		return -errno;
	}

	q->iod_arr_bytes = (size_t)q->depth * sizeof(struct ublksrv_io_desc);
	q->iod_arr = mmap(NULL, q->iod_arr_bytes, PROT_READ, MAP_SHARED, q->ublkc_fd,
			  UBLKSRV_CMD_BUF_OFFSET);
	if (q->iod_arr == MAP_FAILED) {
		fprintf(stderr, "mmap(iod): %s\n", strerror(errno));
		q->iod_arr = NULL;
		rc = -errno;
		goto err;
	}

	q->ios = calloc(q->depth, sizeof(*q->ios));
	if (!q->ios) {
		rc = -ENOMEM;
		goto err;
	}
	for (uint16_t t = 0; t < q->depth; t++) {
		q->ios[t].tag = t;
		q->ios[t].q = q;
		q->ios[t].iod = &q->iod_arr[t];
		q->ios[t].buf = xnvme_buf_alloc(dev->xdev, dev->max_io_buf);
		if (!q->ios[t].buf) {
			fprintf(stderr, "xnvme_buf_alloc(%u): %s\n", dev->max_io_buf,
				strerror(errno));
			rc = -ENOMEM;
			goto err;
		}
	}

	rc = xnvme_queue_init(dev->xdev, q->depth, 0, &q->xq);
	if (rc < 0) {
		fprintf(stderr, "xnvme_queue_init(%u): %s\n", q->depth, strerror(-rc));
		goto err;
	}

	return 0;

err:
	qublk_io_fini(dev);
	return rc;
}

/*
 * Submit initial FETCH_REQs from the I/O thread so that ublk_drv records the
 * I/O thread as the queue's ubq_daemon. Subsequent COMMIT_AND_FETCH commands
 * (also from this thread) then pass the daemon == current check.
 */
static int
submit_initial_fetches(struct qublk_dev *dev)
{
	struct qublk_queue *q = &dev->q;
	int rc;

	for (uint16_t t = 0; t < q->depth; t++) {
		rc = submit_fetch(q, &q->ios[t]);
		if (rc < 0) {
			fprintf(stderr, "submit_fetch(%u): %s\n", t, strerror(-rc));
			return rc;
		}
	}
	rc = io_uring_submit(&q->ring);
	if (rc < 0) {
		fprintf(stderr, "io_uring_submit(initial FETCHs): %s\n", strerror(-rc));
		return rc;
	}
	return 0;
}

void
qublk_io_fini(struct qublk_dev *dev)
{
	struct qublk_queue *q = &dev->q;

	if (q->xq) {
		xnvme_queue_drain(q->xq);
		xnvme_queue_term(q->xq);
		q->xq = NULL;
	}
	if (q->ios) {
		for (uint16_t t = 0; t < q->depth; t++) {
			if (q->ios[t].buf) {
				xnvme_buf_free(dev->xdev, q->ios[t].buf);
			}
		}
		free(q->ios);
		q->ios = NULL;
	}
	if (q->iod_arr) {
		munmap(q->iod_arr, q->iod_arr_bytes);
		q->iod_arr = NULL;
	}
	if (q->ublkc_fd >= 0) {
		close(q->ublkc_fd);
		q->ublkc_fd = -1;
	}
}

static void
io_loop(struct qublk_dev *dev)
{
	struct qublk_queue *q = &dev->q;
	struct io_uring_cqe *cqe;
	unsigned head, count;
	int xp;

	while (!dev->stop) {
		io_uring_submit_and_get_events(&q->ring);

		count = 0;
		io_uring_for_each_cqe (&q->ring, head, cqe) {
			handle_ublk_cqe(q, cqe);
			count++;
		}
		if (count) {
			io_uring_cq_advance(&q->ring, count);
		}

		xnvme_queue_poke(q->xq, 0);
	}

	/* Drain: keep pumping until xnvme queue empty and no more ublk CQEs. */
	for (int idle = 0; idle < 1024;) {
		io_uring_submit_and_get_events(&q->ring);
		count = 0;
		io_uring_for_each_cqe (&q->ring, head, cqe) {
			(void)cqe;
			count++;
		}
		if (count) {
			io_uring_cq_advance(&q->ring, count);
		}

		xp = xnvme_queue_poke(q->xq, 0);
		if (xnvme_queue_get_outstanding(q->xq) == 0 && count == 0 && xp == 0) {
			idle++;
		} else {
			idle = 0;
		}
	}
}

static void *
io_thread_main(void *arg)
{
	struct qublk_dev *dev = arg;
	struct qublk_queue *q = &dev->q;
	int rc;

	rc = init_ring(q);
	if (rc < 0) {
		fprintf(stderr, "io_uring_queue_init(io): %s\n", strerror(-rc));
		dev->io_init_rc = rc;
		sem_post(&dev->io_ready);
		return NULL;
	}

	rc = io_uring_register_files(&q->ring, &q->ublkc_fd, 1);
	if (rc < 0) {
		fprintf(stderr, "io_uring_register_files: %s\n", strerror(-rc));
		dev->io_init_rc = rc;
		sem_post(&dev->io_ready);
		io_uring_queue_exit(&q->ring);
		return NULL;
	}

	dev->io_init_rc = submit_initial_fetches(dev);
	sem_post(&dev->io_ready);
	if (dev->io_init_rc == 0) {
		io_loop(dev);
	}
	io_uring_queue_exit(&q->ring);
	return NULL;
}

int
qublk_io_thread_start(struct qublk_dev *dev)
{
	int rc;

	if (sem_init(&dev->io_ready, 0, 0) < 0) {
		fprintf(stderr, "sem_init: %s\n", strerror(errno));
		return -errno;
	}
	dev->io_init_rc = 0;
	rc = pthread_create(&dev->io_tid, NULL, io_thread_main, dev);
	if (rc) {
		fprintf(stderr, "pthread_create(io): %s\n", strerror(rc));
		sem_destroy(&dev->io_ready);
		return -rc;
	}
	sem_wait(&dev->io_ready);
	sem_destroy(&dev->io_ready);
	if (dev->io_init_rc < 0) {
		pthread_join(dev->io_tid, NULL);
		dev->io_tid = 0;
		return dev->io_init_rc;
	}
	return 0;
}

void
qublk_io_thread_join(struct qublk_dev *dev)
{
	if (dev->io_tid) {
		pthread_join(dev->io_tid, NULL);
		dev->io_tid = 0;
	}
}
