# qublk — quick ublk

A small, fast ublk server in C using [xNVMe](https://xnvme.io) as the storage backend.

## Status

Supports `READ`, `WRITE`, `FLUSH`, `REQ_FUA`, and `REQ_PREFLUSH`; multiple ublk hardware
queues via `--nr-queues`.

## Build (Linux)

Requires: meson, ninja, a C compiler, liburing (>= 2.3), xnvme (>= 0.7.5), and a
kernel that ships `ublk_drv` and `<linux/ublk_cmd.h>` (>= 6.0).

```sh
make config
make build
sudo make install
```

## Run

```sh
sudo modprobe ublk_drv

# NVMe block device via io_uring:
sudo qublk /dev/nvme0n1 --be io_uring --depth 64

# Userspace NVMe (upcie) — requires device bound to uio_pci_generic and hugepages set up:
sudo qublk 0000:01:00.0 --be upcie --depth 64

# Multiple hardware queues:
sudo qublk /dev/nvme0n1 --be io_uring --nr-queues 4 --depth 64
```

While qublk is running, `/dev/ublkb0` is the resulting block device.

`SIGINT` / `SIGTERM` triggers a clean teardown (`STOP_DEV` → `DEL_DEV`).

## Flags

| Flag | Default | Meaning |
| --- | --- | --- |
| `--be NAME` | xnvme default | `upcie`, `io_uring`, `io_uring_cmd`, `libaio`, … |
| `--depth N` | 64 | Per-queue depth |
| `--nr-queues N` | 1 | Number of ublk hardware queues |
| `--dev-id N` | -1 (auto) | Requested ublk device id |
| `--max-io-bytes N` | min(1MiB, MDTS) | Per-IO buffer size |
