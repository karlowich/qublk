#ifndef QUBLK_H
#define QUBLK_H

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>

#include <linux/ublk_cmd.h>
#include <liburing.h>
#include <libxnvme.h>

#define QUBLK_MAX_QUEUE_DEPTH UBLK_MAX_QUEUE_DEPTH

struct qublk_dev;
struct qublk_queue;

struct qublk_io {
	uint16_t tag;
	void *buf;
	const struct ublksrv_io_desc *iod;
	struct qublk_queue *q;
};

struct qublk_queue {
	int q_id;
	uint16_t depth;
	int ublkc_fd;
	struct io_uring ring;
	struct ublksrv_io_desc *iod_arr;
	size_t iod_arr_bytes;
	struct xnvme_queue *xq;
	struct qublk_io *ios;
	struct qublk_dev *dev;
	pthread_t tid;
	int init_rc;
};

struct qublk_dev {
	int ctrl_fd;
	int dev_id;
	uint16_t nr_queues;
	uint16_t depth;
	uint32_t max_io_buf;
	uint64_t flags;
	struct xnvme_dev *xdev;
	const struct xnvme_geo *geo;
	uint8_t lba_shift;
	struct qublk_queue *queues;
	sem_t io_ready;
	volatile sig_atomic_t stop;
};

#endif
