# qublk — quick ublk

A small, fast ublk server in C using [xNVMe](https://xnvme.io) as the storage backend.

## Status

Foundation: single queue, single thread; supports `READ`, `WRITE`, `FLUSH`.

## Build (Linux)

Requires: meson, ninja, a C compiler, liburing (>= 2.3), xnvme (>= 0.7.5), and a
kernel that ships `ublk_drv` and `<linux/ublk_cmd.h>` (>= 6.0).

```sh
meson setup build
meson compile -C build
```

## Run

```sh
sudo modprobe ublk_drv

# NVMe block device:
sudo ./build/src/qublk --dev-uri /dev/nvme0n1 --be io_uring --depth 64

# Userspace NVMe (upcie) — requires device bound to vfio-pci and hugepages set up:
sudo ./build/src/qublk --dev-uri 0000:01:00.0 --be upcie --depth 64
```

While qublk is running, `/dev/ublkb0` is the resulting block device.

`SIGINT` / `SIGTERM` triggers a clean teardown (`STOP_DEV` → `DEL_DEV`).

## Flags

| Flag | Default | Meaning |
| --- | --- | --- |
| `--dev-uri URI` | required | URI passed to `xnvme_dev_open` |
| `--be NAME` | xnvme default | `upcie`, `io_uring`, `io_uring_cmd`, `libaio`, … |
| `--depth N` | 64 | Per-queue depth |
| `--dev-id N` | -1 (auto) | Requested ublk device id |
| `--max-io-bytes N` | min(1MiB, MDTS) | Per-IO buffer size |
