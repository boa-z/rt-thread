#!/usr/bin/env python3
# -*- coding: utf-8 -*-
#
# SPDX-License-Identifier: Apache-2.0
#
# Shared helpers for the ArtInChip hardware-in-the-loop runners.
#
# This module contains only board-independent building blocks: serial
# console access, report writing, cross-process locks, manifest/image
# selection and the D12p/D21x vendor image trimming helper.  The board
# specific pipeline (safety gates, upgrade entry, flashing backend and
# test selection) stays in d213_hil.py / d126_hil.py.

import datetime
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

try:
    import serial
except ImportError:
    serial = None

ARTICHIP_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RTT_ROOT = os.path.normpath(os.path.join(ARTICHIP_DIR, "..", ".."))
ARTIFACTS = os.path.join(ARTICHIP_DIR, "artifacts")

# With DFS enabled FinSH prints the current directory, e.g. "msh />" or
# "msh /sdcard>".  Accept that form too so USB/DFS images are not
# mistaken for a boot failure.
PROMPT_RE = re.compile(r"msh\s+(?:[^>\s]+\s*)?>")


def _safe_stdout():
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except Exception:
            pass


def stamp():
    return datetime.datetime.now().strftime("%Y%m%d-%H%M%S")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def git(root, *args):
    try:
        return subprocess.check_output(
            ["git"] + list(args), cwd=root, text=True,
            stderr=subprocess.DEVNULL).strip()
    except Exception:
        return "unknown"


def source_state(root):
    """Commit + working-tree state, to detect edits during a build."""
    return (git(root, "rev-parse", "HEAD"), git(root, "status", "--porcelain"))


def run_logged(cmd, log_path, timeout, cwd=None, env=None):
    with open(log_path, "w", encoding="utf-8", errors="replace") as log:
        log.write("+ " + " ".join(cmd) + "\n")
        log.flush()
        try:
            proc = subprocess.run(cmd, cwd=cwd, env=env, stdout=log,
                                  stderr=subprocess.STDOUT, timeout=timeout)
            return proc.returncode, None
        except subprocess.TimeoutExpired:
            log.write("\n[timeout after %s s]\n" % timeout)
            return None, "timeout"


def capture(cmd, timeout=60):
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True,
                              timeout=timeout)
        return proc.returncode, (proc.stdout or "") + (proc.stderr or "")
    except subprocess.TimeoutExpired:
        return None, "timeout"
    except Exception as exc:
        return None, "failed to run %s: %s" % (cmd[0], exc)


def parse_devices(text):
    stages = re.findall(r"Boot stage:\s*(.+)", text)
    if stages:
        return [s.strip() for s in stages]
    return []


def upgcmd_devices(upgcmd, timeout=30):
    rc, text = capture([upgcmd, "-l"], timeout)
    return parse_devices(text), text


def parse_scan_devices(text):
    return [m.strip() for m in re.findall(r"path=(\S+)", text)]


def aicflash_scan(flasher, timeout=20):
    rc, text = capture([flasher, "scan"], timeout)
    return parse_scan_devices(text), text


def parse_image_aicflash(flasher, image):
    """Parse the image locally with the flasher; no USB access."""
    rc, text = capture([flasher, "info", image], 60)
    product = re.search(r"Product:\s*(\S+)", text)
    components = re.findall(r"\]\s+(image\.target\.\S+)\s", text)
    return {
        "product": product.group(1) if product else None,
        "components": components,
        "raw": text,
    }


def plan_os_partition(flasher, image, timeout=60):
    """Return (selected target components, raw plan text).

    Calls "burn --partition os --plan", which only parses the image and
    prints the plan, without touching a device.  None means the installed
    flasher has no --partition support, or the plan could not be parsed;
    callers then fall back to a derived image instead.
    """
    rc, text = capture(
        [flasher, "burn", image, "--partition", "os", "--plan"], timeout)
    if rc != 0:
        return None, text
    selected = []
    for line in text.splitlines():
        match = re.match(
            r"\s*target:\s+(image\.target\.\S+)\s+.*selected=(true|false)",
            line)
        if match and match.group(2) == "true":
            selected.append(match.group(1))
    if not re.search(r"Burn plan:", text):
        return None, text
    return selected, text


class Console:
    def __init__(self, port, baud, log_path):
        self.port = port
        self.baud = baud
        self.log_path = log_path
        self.ser = None
        self.buffer = ""
        self.log = None

    def open(self):
        if serial is None:
            raise RuntimeError("pyserial is required: pip install pyserial")
        self.log = open(self.log_path, "a", encoding="utf-8",
                        errors="replace")
        self.log.write("# serial %s @ %d, %s\n" %
                       (self.port, self.baud,
                        datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")))
        self.log.flush()
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.1,
                                     write_timeout=1)
        except Exception:
            self.log.close()
            self.log = None
            raise

    def close(self):
        try:
            if self.ser is not None:
                self.ser.close()
        finally:
            if self.log is not None:
                self.log.close()
        self.ser = None

    def read_once(self):
        try:
            data = self.ser.read(4096)
        except Exception:
            return ""
        if not data:
            return ""
        text = data.decode("utf-8", "replace")
        self.buffer += text
        self.log.write(text)
        self.log.flush()
        try:
            sys.stdout.write(text)
            sys.stdout.flush()
        except UnicodeEncodeError:
            sys.stdout.write(text.encode("ascii", "replace").decode("ascii"))
            sys.stdout.flush()
        return text

    def drain(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.read_once()

    def read_until(self, needles, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.read_once()
            for needle in needles:
                if needle in self.buffer:
                    return needle
        return None

    def send(self, line):
        self.ser.write((line + "\r\n").encode("utf-8"))
        self.ser.flush()

    def wait_prompt(self, timeout):
        self.buffer = ""
        deadline = time.monotonic() + timeout
        nudged = False
        while time.monotonic() < deadline:
            self.read_once()
            if "msh >" in self.buffer or PROMPT_RE.search(self.buffer):
                return True
            if not nudged and time.monotonic() - (deadline - timeout) > 2.0:
                self.send("")
                nudged = True
        return False


def now_iso():
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


def open_console(port, baud, log_path, timeout=15):
    console = Console(port, baud, log_path)
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            console.open()
            return console
        except Exception as exc:
            last = exc
            time.sleep(0.5)
    raise RuntimeError(last)


def write_report(report, report_dir):
    os.makedirs(report_dir, exist_ok=True)
    path = os.path.join(report_dir, "report.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
        f.write("\n")
    return path


def stage(report, name, status, **extra):
    entry = {"status": status}
    entry.update(extra)
    report["stages"][name] = entry
    print("[hil] %-14s %s" % (name, status))
    return entry


class FileLock:
    """Cross-process exclusive lock (build / serial / USB target)."""

    def __init__(self, path):
        self.path = path
        self.file = None

    def acquire(self):
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        self.file = open(self.path, "a+")
        try:
            self.file.seek(0, os.SEEK_END)
            if self.file.tell() == 0:
                self.file.write("0")
                self.file.flush()
            self.file.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except OSError:
            self.file.close()
            self.file = None
            return False

    def release(self):
        if not self.file:
            return
        try:
            if os.name == "nt":
                import msvcrt
                self.file.seek(0)
                msvcrt.locking(self.file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(), fcntl.LOCK_UN)
        finally:
            self.file.close()
            self.file = None


def lock_path(artifacts, name):
    return os.path.join(artifacts, ".locks", name)


def read_text(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def load_manifest(artifacts, target):
    path = os.path.join(artifacts, target, "manifest.json")
    if not os.path.isfile(path):
        return None, path
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f), path
    except (OSError, ValueError):
        return None, path


def resolve_image(args, artifacts):
    """Select the image from the build manifest, never from mtime."""
    manifest, manifest_path = load_manifest(artifacts, args.target)
    build_id = (manifest or {}).get("build_id")
    if args.image:
        image = args.image
        if not os.path.isfile(image):
            return None, None, build_id, manifest_path, "image not found: %s" % image
        sha = sha256_file(image)
        expected = ((manifest or {}).get("img") or {}).get("sha256")
        if expected and expected != sha:
            return (image, sha, build_id, manifest_path,
                    "explicit image does not match the build manifest")
        return image, sha, build_id, manifest_path, None
    if not manifest or not manifest.get("img"):
        return None, None, build_id, manifest_path, "no manifest.json; run build-check first"
    entry = manifest["img"]
    image = entry.get("path")
    if not image or not os.path.isfile(image):
        return None, None, build_id, manifest_path, "manifest image missing: %s" % image
    return image, entry.get("sha256"), build_id, manifest_path, None


def _json_object_span(text, key):
    """Return (start, end) of the object value of a top-level key."""
    match = re.search(r'"%s"\s*:\s*\{' % re.escape(key), text)
    if not match:
        return None
    start = match.start()
    depth = 0
    for pos in range(match.end() - 1, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return start, pos + 1
    return None


def vendor_script_env(scripts):
    env = os.environ.copy()
    env["PATH"] = scripts + os.pathsep + env.get("PATH", "")
    env["PYTHONPATH"] = scripts + os.pathsep + env.get("PYTHONPATH", "")
    return env


def make_os_only_image(image, report_dir, sdk):
    """Derive an image whose only flash target is the "os" partition.

    Older artinchip-flash builds have no --partition filter, and the D213
    flow predates that option, so this fallback rewrites only the target
    section of image_cfg.json and re-runs the vendor mk_image.py.  SPL,
    env and the data partitions stay untouched.  Callers verify the
    resulting component list before flashing and prefer the flasher's
    own --partition os selection when it is available.
    """
    if not sdk:
        raise RuntimeError("no --sdk/AIC_SDK_DIR for the vendor image tool")
    src_dir = os.path.dirname(os.path.abspath(image))
    cfg_src = os.path.join(src_dir, "image_cfg.json")
    scripts = os.path.join(sdk, "tools", "scripts")
    mk_image = os.path.join(scripts, "mk_image.py")
    if not os.path.isfile(cfg_src):
        raise RuntimeError("missing image_cfg.json beside %s" % image)
    if not os.path.isfile(mk_image):
        raise RuntimeError("missing vendor mk_image.py: %s" % mk_image)

    work = os.path.join(report_dir, "os-image")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for name in os.listdir(src_dir):
        path = os.path.join(src_dir, name)
        # The previous .img is an output, not an input.
        if os.path.isfile(path) and not name.endswith(".img"):
            shutil.copy2(path, os.path.join(work, name))

    cfg_dst = os.path.join(work, "image_cfg.json")
    with open(cfg_src, encoding="utf-8") as f:
        text = f.read()
    span = _json_object_span(text, "target")
    if not span:
        raise RuntimeError("no target section in image_cfg.json")
    target = text[span[0]:span[1]]
    os_entry = re.search(r'"os"\s*:\s*\{[^{}]*\}', target)
    if not os_entry:
        raise RuntimeError("no os target in image_cfg.json")
    new_target = '"target": {\n            %s\n        }' % os_entry.group(0)
    text = text[:span[0]] + new_target + text[span[1]:]
    with open(cfg_dst, "w", encoding="utf-8") as f:
        f.write(text)

    log = os.path.join(report_dir, "os-image.log")
    rc, timeout = run_logged(
        [sys.executable, mk_image, "-v", "-c", cfg_dst, "-d", work + os.sep],
        log, 180, cwd=work, env=vendor_script_env(scripts))
    if timeout:
        raise RuntimeError("vendor mk_image.py timed out")
    if rc != 0:
        raise RuntimeError("vendor mk_image.py exited %d (see %s)" % (rc, log))

    images = glob.glob(os.path.join(work, "*.img"))
    if len(images) != 1:
        raise RuntimeError("expected one derived image, found %d" % len(images))
    return images[0]


def parse_build_id_output(text):
    match = re.search(r"^build_id:\s*(\S+)\s*$", text, re.MULTILINE)
    return match.group(1) if match else None


def classify_flash_failure(text):
    if "Multiple ArtInChip devices" in text:
        return "DEVICE_AMBIGUOUS"
    if "No ArtInChip device" in text or "No device is connected" in text:
        return "DEVICE_NOT_FOUND"
    if ("Bulk write failed" in text or "device returned CSW" in text
            or "No CSW" in text or "Pipe" in text):
        return "FLASH_INTERRUPTED"
    return "FLASH_FAIL"
