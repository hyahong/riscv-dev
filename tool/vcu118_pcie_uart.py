#!/usr/bin/env python3
import argparse
from contextlib import ExitStack, contextmanager
import ctypes
from functools import partial
import math
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
DMA_CHUNK = 8 * 1024 * 1024
PROGRESS_STEP = 256 * 1024 * 1024

MAGIC = 0x00
VERSION = 0x04
CONTROL = 0x08
STATUS = 0x0C
SCRATCH = 0x10
STATUS_HELD = 0x0F
STATUS_RUNNING = 0x0B

_read = ctypes.CDLL(None, use_errno=True).read
_read.argtypes = (ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t)
_read.restype = ctypes.c_ssize_t


# Command-line input

def parse_load_targets(value):
    targets = [target.strip() for target in value.split(",")]
    if any(target not in ("binary", "ddr") for target in targets):
        raise argparse.ArgumentTypeError(
            "load targets must be binary, ddr, or binary,ddr")
    return targets


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--load-target", type=parse_load_targets,
                        default="binary,ddr", metavar="TARGET[,TARGET...]",
                        help="Comma-separated load targets: binary (--image), ddr (--hnsw) "
                             "(default: binary,ddr)")
    parser.add_argument("--image", type=Path,
                        help="Raw program binary linked for 0x80000000 (required for binary)")
    parser.add_argument("--hnsw", type=Path,
                        help="HNSW data file to load at 0xECCCC000 (required for ddr)")
    parser.add_argument("--prefix", default="/dev/xdma0")
    parser.add_argument("--bdf", required=True)
    parser.add_argument("--uart", action="append", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=20,
                        help="UART capture duration in seconds; 0 means until Ctrl+C (default: 20)")
    args = parser.parse_args()
    for target, option in (("binary", "image"), ("ddr", "hnsw")):
        if target in args.load_target and getattr(args, option) is None:
            parser.error("--%s is required when --load-target includes %s" % (option, target))
    if not math.isfinite(args.timeout) or args.timeout < 0:
        parser.error("--timeout must be finite and nonnegative")
    return args


def resolve_device(prefix, bdf):
    device = (Path("/sys/class/xdma") / (Path(prefix).name + "_user") / "device").resolve()
    if device.name != bdf:
        raise RuntimeError("XDMA device number does not match the selected BDF")
    return device


def validate_range(address, size):
    if size <= 0 or address < DDR0 or address + size > DDR_END:
        raise ValueError("Load range [%#x, %#x) must be nonempty and within DDR [%#x, %#x)" %
                         (address, address + size, DDR0, DDR_END))


def validate_loads(loads):
    """Check all input files and ranges before touching the FPGA."""
    ranges = []
    for address, path in loads:
        if not path.is_file():
            raise ValueError("Not a regular input file: %s" % path)
        with path.open("rb") as source:
            size = os.fstat(source.fileno()).st_size
        validate_range(address, size)
        ranges.append((address, address + size, path))
    ranges.sort(key=lambda region: region[0])
    for previous, current in zip(ranges, ranges[1:]):
        if current[0] < previous[1]:
            raise ValueError("Overlapping load ranges: %s and %s" % (previous[2], current[2]))


# Host-control registers and CPU reset (offsets in the _user BAR, not DDR addresses)

def read32(mmio, offset):
    return struct.unpack_from("<I", mmio, offset)[0]


def write32(mmio, offset, value):
    struct.pack_into("<I", mmio, offset, value)


@contextmanager
def open_control(prefix):
    with ExitStack() as stack:
        fd = os.open(prefix + "_user", os.O_RDWR | os.O_SYNC)
        stack.callback(os.close, fd)
        mmio = stack.enter_context(mmap.mmap(fd, 4096))
        try:
            yield mmio
        finally:
            print("Final CONTROL=%#x STATUS=%#x" %
                  (read32(mmio, CONTROL), read32(mmio, STATUS)), flush=True)


def check_reset(mmio, held=True):
    expected_status = STATUS_HELD if held else STATUS_RUNNING
    control, status = read32(mmio, CONTROL), read32(mmio, STATUS)
    if control != int(held) or status & 0x0F != expected_status:
        raise RuntimeError("Unexpected CONTROL/STATUS: %#x/%#x" % (control, status))


def wait_for_reset(mmio, held, timeout, interval=0.001):
    expected_status = STATUS_HELD if held else STATUS_RUNNING
    deadline = time.monotonic() + timeout
    while read32(mmio, STATUS) & 0x0F != expected_status and time.monotonic() < deadline:
        time.sleep(interval)
    check_reset(mmio, held)


def set_cpu_reset(mmio, held):
    write32(mmio, CONTROL, int(held))
    if read32(mmio, CONTROL) != int(held):
        raise RuntimeError("CPU reset control write was not accepted")
    wait_for_reset(mmio, held, timeout=2)


def check_host_control(mmio):
    registers = (MAGIC, VERSION, CONTROL, STATUS, SCRATCH)
    print("MMIO:", {hex(i): hex(read32(mmio, i)) for i in registers}, flush=True)
    if read32(mmio, MAGIC) != 0x58444D41 or read32(mmio, VERSION) != 1:
        raise RuntimeError("Unexpected host-control interface")
    # previous run leaves the CPU released. stop it before overwriting DDR.
    if read32(mmio, CONTROL) != 1:
        print("Asserting CPU reset before DMA loading", flush=True)
        set_cpu_reset(mmio, held=True)
    wait_for_reset(mmio, held=True, timeout=30, interval=0.05)


# Blocking DMA transport

@contextmanager
def open_dma(prefix):
    with ExitStack() as stack:
        h2c = os.open(prefix + "_h2c_0", os.O_WRONLY)
        stack.callback(os.close, h2c)
        c2h = os.open(prefix + "_c2h_0", os.O_RDONLY)
        stack.callback(os.close, c2h)
        yield h2c, c2h


def transfer(fd, address, buffer, writing):
    os.lseek(fd, address, os.SEEK_SET)
    if writing:
        actual = os.write(fd, buffer)
    else:
        # Keep blocking libc read: readv/read_iter failed with the legacy XDMA driver.
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
    if len(actual) != len(expected):
        raise RuntimeError("DMA length mismatch at %#x: expected=%d actual=%d" %
                           (address, len(expected), len(actual)))
    if actual != expected:
        i = next(i for i, pair in enumerate(zip(expected, actual)) if pair[0] != pair[1])
        raise RuntimeError("DMA mismatch at %#x: expected=%#x actual=%#x" %
                           (address + i, expected[i], actual[i]))


def write_verify(h2c, c2h, address, data):
    # Match host-buffer and FPGA-address page offsets, including unaligned loads.
    offset = address % mmap.PAGESIZE
    with mmap.mmap(-1, offset + len(data)) as buffer:
        with memoryview(buffer)[offset:offset + len(data)] as view:
            view[:] = data
            transfer(h2c, address, view, True)
            transfer(c2h, address, view, False)
            compare(address, data, bytes(view))


# Binary loading

def load_binary(mmio, h2c, c2h, address, path):
    """Copy a file verbatim to a DDR address in bounded chunks, verifying each one.

    The CPU must be held in reset. This copies bytes; it does not parse ELF,
    relocate pointers, initialize BSS, or change the CPU's boot address.
    """
    path = Path(path)
    with path.open("rb") as source:
        size = os.fstat(source.fileno()).st_size
        validate_range(address, size)
        print("Loading %s at %#x: %d bytes" % (path, address, size), flush=True)
        completed = 0
        while completed < size:
            check_reset(mmio)
            expected = min(DMA_CHUNK, size - completed)
            data = source.read(expected)
            if len(data) != expected:
                raise RuntimeError("Input file shortened while loading: %s" % path)
            write_verify(h2c, c2h, address + completed, data)
            completed += len(data)
            if completed % PROGRESS_STEP == 0 or completed == size:
                print("  Loaded and verified %d / %d bytes" % (completed, size), flush=True)
        if source.read(1):
            raise RuntimeError("Input file grew while loading: %s" % path)
    check_reset(mmio)


# UART output capture

@contextmanager
def open_uarts(ports, output_dir):
    with ExitStack() as stack:
        selector = stack.enter_context(selectors.DefaultSelector())
        for port in ports:
            fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
            stack.callback(os.close, fd)
            original = termios.tcgetattr(fd)
            stack.callback(termios.tcsetattr, fd, termios.TCSANOW, original)
            configure_uart(fd)
            log = stack.enter_context((output_dir / (Path(port).name + ".bin")).open("wb"))
            selector.register(fd, selectors.EVENT_READ, (port, log))
        yield selector


def configure_uart(fd):
    settings = termios.tcgetattr(fd)
    settings[:6] = [0, 0, termios.CLOCAL | termios.CREAD | termios.CS8, 0,
                    termios.B115200, termios.B115200]
    settings[6][termios.VMIN] = 0
    settings[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, settings)
    termios.tcflush(fd, termios.TCIFLUSH)


def observe_uart(selector, timeout):
    """Capture arbitrary output for timeout seconds, or indefinitely when it is zero."""
    OBSERVE_START = time.monotonic()

    first = True
    deadline = time.monotonic() + timeout if timeout else None
    while deadline is None or time.monotonic() < deadline:
        delay = 0.5 if deadline is None else min(0.5, max(0, deadline - time.monotonic()))
        for key, _ in selector.select(delay):
            try:
                data = os.read(key.fd, 4096)
            except BlockingIOError:
                continue
            if not data:
                continue
            port, log = key.data
            log.write(data)
            log.flush()
            if first:
                print("\n[%s]" % port, flush=True)
                first = False
            elapsed_ms = int((time.monotonic() - OBSERVE_START) * 1000)
            print("[%d ms] %s\n" % (elapsed_ms, data.decode("ascii", errors="replace")),
                  end="", flush=True)


def run_program(mmio, selector, timeout, check_kernel):
    completed = False
    try:
        set_cpu_reset(mmio, held=False)
        print("CPU reset released. observing UART at 115200 baud", flush=True)
        try:
            observe_uart(selector, timeout)
        except KeyboardInterrupt:
            print("\nUART capture interrupted", flush=True)
        check_reset(mmio, held=False)
        check_kernel()
        completed = True
        print("\nUART capture ended; CPU remains released. Program completion was not checked.",
              flush=True)
    finally:
        if not completed:
            set_cpu_reset(mmio, held=True)
            print("CPU reset asserted after an execution or I/O error", flush=True)


# Kernel diagnostics

def read_kernel_log():
    return subprocess.check_output(["dmesg", "--color=never"])


def check_kernel_log(output_dir, related_bdfs, started):
    raw = read_kernel_log()
    (output_dir / "kernel-after.txt").write_bytes(raw)
    lines = []
    for line in raw.decode(errors="replace").splitlines():
        stamp = re.match(r"^\[\s*([0-9.]+)\]", line)
        if stamp and float(stamp.group(1)) >= started:
            lines.append(line)
    (output_dir / "kernel-new.txt").write_text("\n".join(lines) + "\n")
    errors = [line for line in lines
              if (any(bdf in line for bdf in related_bdfs) or "xdma" in line.lower())
              and re.search(r"AER:|PCIe Bus Error|xdma.*(error|failed|timeout)", line, re.I)]
    if errors:
        raise RuntimeError("New PCIe/DMA kernel errors: " + "\n".join(errors))


@contextmanager
def monitor_kernel(output_dir, device):
    related = {part for part in device.parts
               if re.fullmatch(r"[0-9a-f]{4}:[0-9a-f]{2}:[0-9a-f]{2}\.[0-7]", part)}
    started = float(Path("/proc/uptime").read_text().split()[0])
    (output_dir / "kernel-before.txt").write_bytes(read_kernel_log())
    check = partial(check_kernel_log, output_dir, related, started)
    try:
        yield check
    finally:
        check()


def main():
    args = parse_args()
    loads = []
    if "binary" in args.load_target:
        loads.append((DDR0, args.image))
    if "ddr" in args.load_target:
        loads.append((0xECCCC000, args.hnsw))
    validate_loads(loads)
    device = resolve_device(args.prefix, args.bdf)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    with monitor_kernel(args.output_dir, device) as check_kernel:
        with open_control(args.prefix) as mmio:
            check_host_control(mmio)

            with open_dma(args.prefix) as (h2c, c2h):
                for address, path in loads:
                    load_binary(mmio, h2c, c2h, address, path)
                check_kernel()

                # Open and flush every UART before allowing the CPU to run.
                with open_uarts(args.uart, args.output_dir) as selector:
                    run_program(mmio, selector, args.timeout, check_kernel)

if __name__ == "__main__":
    main()


