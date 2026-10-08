#!/usr/bin/env python3
"""Load the pcieboot smoke payload through XDMA and observe it over UART."""
import argparse
from array import array
import contextlib
import ctypes
import mmap
import os
from pathlib import Path
import re
import selectors
import struct
import subprocess
import termios
import time

DDR0 = 0x80000000
DDR1 = 0x100000000
DDR_END = 0x180000000
PATTERN = 0x5043494500000000
_read = ctypes.CDLL(None, use_errno=True).read
_read.argtypes = (ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t)
_read.restype = ctypes.c_ssize_t


def transfer(fd, address, buffer, writing):
    os.lseek(fd, address, os.SEEK_SET)
    if writing:
        actual = os.write(fd, buffer)
    else:
        # Legacy XDMA read_iter is asynchronous; use blocking read, not readv.
        pointer = ctypes.addressof(ctypes.c_char.from_buffer(buffer))
        actual = _read(fd, pointer, len(buffer))
        if actual < 0:
            error = ctypes.get_errno()
            raise OSError(error, "DMA read at %#x: requested=%d: %s" %
                          (address, len(buffer), os.strerror(error)))
    if actual != len(buffer):
        raise RuntimeError("Short DMA %s at %#x: requested=%d actual=%d" %
                           ("write" if writing else "read", address, len(buffer), actual))
    return actual


def compare(address, expected, actual):
    if actual != expected:
        i = next(i for i, pair in enumerate(zip(expected, actual)) if pair[0] != pair[1])
        raise RuntimeError("DMA mismatch at %#x: expected=%#x actual=%#x requested=%d actual_bytes=%d" %
                           (address + i, expected[i], actual[i], len(expected), len(actual)))


def write_verify(h2c, c2h, address, data):
    with mmap.mmap(-1, (len(data) + 4095) & ~4095) as buffer:
        with memoryview(buffer)[:len(data)] as view:
            view[:] = data
            transfer(h2c, address, view, True)
            transfer(c2h, address, view, False)
            compare(address, data, bytes(view))


def full_ddr(h2c, c2h, check_reset):
    chunk = 8 * 1024 * 1024
    def pattern(address):
        return array("Q", ((address + i) ^ PATTERN for i in range(0, chunk, 8))).tobytes()

    started = time.monotonic()
    written = read = 0
    with mmap.mmap(-1, chunk) as buffer:
        for address in range(DDR0, DDR_END, chunk):
            check_reset()
            buffer[:] = pattern(address)
            written += transfer(h2c, address, buffer, True)
            if written % (256 * 1024 * 1024) == 0:
                print(f"WRITE: {written / 2**30:.2f} / 4.00 GiB", flush=True)
        print("Full 4 GiB write complete: %d bytes" % written, flush=True)
        for address in range(DDR0, DDR_END, chunk):
            check_reset()
            read += transfer(c2h, address, buffer, False)
            compare(address, pattern(address), buffer[:])
            if read % (256 * 1024 * 1024) == 0:
                print(f"READ: {read / 2**30:.2f} / 4.00 GiB", flush=True)
    if written != 0x100000000 or read != 0x100000000:
        raise RuntimeError("Unexpected full-DDR byte totals")
    print("Full 4 GiB write/read: PASS (%.1f s, write=%d read=%d bytes; includes pattern/compare)" %
          (time.monotonic() - started, written, read), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--prefix", default="/dev/xdma0")
    parser.add_argument("--bdf", required=True)
    parser.add_argument("--run-id", required=True)
    parser.add_argument("--uart", action="append", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--full-ddr", action="store_true")
    parser.add_argument("--timeout", type=float, default=20)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]+", args.run_id):
        raise RuntimeError("Use an alphanumeric run ID")
    device = Path("/sys/class/xdma") / (Path(args.prefix).name + "_user") / "device"
    if device.resolve().name != args.bdf:
        raise RuntimeError("XDMA device number does not match the selected BDF")
    image = args.image.read_bytes()
    if not image or len(image) > 2 * 1024 * 1024:
        raise RuntimeError("Expected a nonempty smoke.bin of at most 2 MiB")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    related = {part for part in device.resolve().parts if re.fullmatch(r"[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]", part)}
    kernel_start = float(Path("/proc/uptime").read_text().split()[0])
    (args.output_dir / "kernel-before.txt").write_bytes(subprocess.check_output(["dmesg", "--color=never"]))

    def kernel_check():
        raw = subprocess.check_output(["dmesg", "--color=never"])
        (args.output_dir / "kernel-after.txt").write_bytes(raw)
        new = []
        for line in raw.decode(errors="replace").splitlines():
            stamp = re.match(r"^\[\s*([0-9.]+)\]", line)
            if stamp and float(stamp.group(1)) >= kernel_start:
                new.append(line)
        (args.output_dir / "kernel-new.txt").write_text("\n".join(new) + "\n")
        errors = [line for line in new
                  if (any(bdf in line for bdf in related) or "xdma" in line.lower())
                  and re.search(r"AER:|PCIe Bus Error|xdma.*(error|failed|timeout)", line, re.I)]
        if errors:
            raise RuntimeError("New PCIe/DMA kernel errors: " + "\n".join(errors))

    with contextlib.ExitStack() as stack:
        stack.callback(kernel_check)
        user = os.open(args.prefix + "_user", os.O_RDWR | os.O_SYNC)
        stack.callback(os.close, user)
        mmio = stack.enter_context(mmap.mmap(user, 4096))

        def read(offset):
            return struct.unpack_from("<I", mmio, offset)[0]

        def write(offset, value):
            struct.pack_into("<I", mmio, offset, value)

        def reset_state(expected):
            if read(8) != (1 if expected == 15 else 0) or read(12) & 15 != expected:
                raise RuntimeError("Unexpected CONTROL/STATUS: %#x/%#x" % (read(8), read(12)))

        stack.callback(lambda: print("Final CONTROL=%#x STATUS=%#x" % (read(8), read(12)), flush=True))
        print("MMIO:", {hex(i): hex(read(i)) for i in (0, 4, 8, 12, 16)}, flush=True)
        if read(0) != 0x58444d41 or read(4) != 1:
            raise RuntimeError("Unexpected host-control interface")
        if read(8) != 1:
            raise RuntimeError("CPU must already be held in reset")
        deadline = time.monotonic() + 30
        while read(12) & 15 != 15 and time.monotonic() < deadline:
            time.sleep(0.05)
        reset_state(15)
        scratch = read(16)
        try:
            write(16, 0xa5a55a5a)
            if read(16) != 0xa5a55a5a:
                raise RuntimeError("Scratch MMIO readback failed")
        finally:
            write(16, scratch)
        if read(16) != scratch:
            raise RuntimeError("Scratch restore failed")
        print("Scratch MMIO: PASS", flush=True)
        h2c = os.open(args.prefix + "_h2c_0", os.O_WRONLY)
        c2h = os.open(args.prefix + "_c2h_0", os.O_RDONLY)
        stack.callback(os.close, h2c)
        stack.callback(os.close, c2h)
        regions = {address: b"".join(struct.pack("<Q", (address + i) ^ PATTERN) for i in range(0, 4096, 8))
                   for address in (DDR0, DDR1 - 4096, DDR1, DDR_END - 4096)}
        with mmap.mmap(-1, 4096) as buffer:
            for address, data in regions.items():
                reset_state(15)
                buffer[:] = data
                transfer(h2c, address, buffer, True)
            for address, data in regions.items():
                reset_state(15)
                transfer(c2h, address, buffer, False)
                compare(address, data, buffer[:])
        print("DDR0/DDR1 start/end DMA and alias: PASS", flush=True)
        if args.full_ddr:
            full_ddr(h2c, c2h, lambda: reset_state(15))
        reset_state(15)
        kernel_check()
        # All destructive memory tests finish before loading the executable.
        write_verify(h2c, c2h, DDR0, image)
        write_verify(h2c, c2h, DDR1, b"".join(struct.pack("<Q", PATTERN + i) for i in range(32)))
        reset_state(15)
        print("Payload at %#x (%d bytes), DDR1 input: DMA readback PASS" % (DDR0, len(image)), flush=True)
        selector = stack.enter_context(selectors.DefaultSelector())
        received = {}
        for port in args.uart:
            fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
            stack.callback(os.close, fd)
            old = termios.tcgetattr(fd)
            stack.callback(termios.tcsetattr, fd, termios.TCSANOW, old)
            raw = termios.tcgetattr(fd)
            raw[:6] = [0, 0, termios.CLOCAL | termios.CREAD | termios.CS8, 0,
                       termios.B115200, termios.B115200]
            raw[6][termios.VMIN] = 0
            raw[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, raw)
            termios.tcflush(fd, termios.TCIFLUSH)
            log = stack.enter_context((args.output_dir / (Path(port).name + ".bin")).open("wb"))
            selector.register(fd, selectors.EVENT_READ, (port, log))
            received[port] = bytearray()
        success = False
        released = False
        try:
            # DMA does not access CPU peripherals or synchronize CPU caches.
            write(8, 0)
            released = True
            if read(8) != 0:
                raise RuntimeError("CPU reset release was not accepted")
            reset_deadline = time.monotonic() + 2
            while read(12) & 15 != 11 and time.monotonic() < reset_deadline:
                time.sleep(0.001)
            reset_state(11)
            print("CPU reset released; observing UART at 115200 baud", flush=True)
            deadline = time.monotonic() + args.timeout
            while time.monotonic() < deadline:
                for key, _ in selector.select(min(0.5, max(0, deadline - time.monotonic()))):
                    data = os.read(key.fd, 4096)
                    if not data:
                        continue
                    port, log = key.data
                    log.write(data)
                    log.flush()
                    received[port].extend(data)
                    print("%s: %s" % (port, data.decode("ascii", errors="replace")), end="", flush=True)
                    if b"PCIE_BOOT_FAIL" in received[port] or b"PCIE_BOOT_TRAP" in received[port]:
                        raise RuntimeError("CPU reported smoke failure/trap")
                    if ("PCIE_BOOT_PASS run=" + args.run_id + "\r\n").encode() in received[port]:
                        begin = ("PCIE_BOOT_BEGIN run=" + args.run_id + " hart=0 dtb=0x0000000000010080\r\n").encode()
                        if begin not in received[port] or b"SUM=5050\r\n" not in received[port]:
                            raise RuntimeError("Missing current-run boot arguments or correct integer sum")
                        reset_state(11)
                        kernel_check()
                        success = True
                        print("\nCPU execution: PASS; STATUS=%#x" % read(12), flush=True)
                        return
            raise RuntimeError("No CPU success message before UART timeout")
        finally:
            if released and not success:
                write(8, 1)
                reset_deadline = time.monotonic() + 2
                while read(12) & 15 != 15 and time.monotonic() < reset_deadline:
                    time.sleep(0.001)
                reset_state(15)
                print("CPU reset asserted after failed execution check", flush=True)


if __name__ == "__main__":
    main()
