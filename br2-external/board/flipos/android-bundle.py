#!/usr/bin/env python3
"""Move the Android-host QEMU out of the guest rootfs into a self-contained bundle.

QEMU is built by the same Buildroot toolchain (aarch64, glibc) so it is FOSS and
rebuilt from pinned sources like everything else. On the phone it runs DIRECTLY on
Android through the bundled glibc loader:

    bundle/lib/ld-linux-aarch64.so.1 --library-path bundle/lib bundle/bin/qemu-...

Steps (TARGET_DIR is Buildroot's target tree, OUT is images/android-host):
  1. copy qemu-system-aarch64, qemu-img and busybox (for httpd) + their full
     shared-library closure (glibc + loader included) and /usr/share/qemu to OUT;
  2. delete every qemu-* binary and /usr/share/qemu from the guest;
  3. delete the programs/data of packages present only for QEMU (HOST_ONLY_PKGS,
     via Buildroot's packages-file-list.txt);
  4. delete a library from the guest only if it was in that set AND no
     remaining guest ELF file (binaries, .so, dlopen'd driver dirs) still needs
     it - so the guest keeps exactly what it used before QEMU was added.

If the guest no longer contains qemu (TARGET_DIR persists between rebuilds and a
previous run already moved it), the existing bundle is kept; stripping always runs.
"""
import os, shutil, subprocess, sys

T, OUT, BUILD = sys.argv[1], sys.argv[2], sys.argv[3]
# Packages that are in the build only for the Android-host QEMU. Their programs
# and data are removed from the guest; their libraries are removed unless some
# remaining guest program still needs them.
HOST_ONLY_PKGS = {"qemu", "libglib2", "libvncserver", "dtc", "slirp", "pcre2", "libffi", "lzo"}
LIBDIRS = ["lib", "usr/lib", "lib64", "usr/lib64"]
QEMU_BINS = ["usr/bin/qemu-system-aarch64", "usr/bin/qemu-img"]
EXTRA_BINS = ["bin/busybox"]          # httpd for noVNC; stays in the guest too


def is_elf(p):
    try:
        with open(p, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def needed(p):
    out = subprocess.run(["readelf", "-dW", p], capture_output=True, text=True).stdout
    res = []
    for line in out.splitlines():
        if "(NEEDED)" in line:
            res.append(line.split("[", 1)[1].split("]", 1)[0])
    return res


def interp(p):
    out = subprocess.run(["readelf", "-lW", p], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if "Requesting program interpreter" in line:
            return line.split(":", 1)[1].strip(" ]")
    return None


def find_lib(name):
    for d in LIBDIRS:
        p = os.path.join(T, d, name)
        if os.path.lexists(p):
            return p
    return None


def closure(paths):
    """Real (symlink-resolved) target paths of all libs the given ELFs need."""
    seen, todo = {}, list(paths)
    while todo:
        p = todo.pop()
        for n in needed(p):
            if n in seen:
                continue
            lp = find_lib(n)
            if lp is None:
                sys.exit(f"android-bundle: {n} (needed by {p}) not found in target")
            seen[n] = lp
            todo.append(os.path.realpath(lp))
    return seen


def is_lib(path):
    return ".so" in os.path.basename(path)


def bundle():
    bins = [os.path.join(T, b) for b in QEMU_BINS]
    if not os.path.exists(bins[0]):
        if os.path.exists(os.path.join(OUT, "bin", "qemu-system-aarch64")):
            print("android-bundle: guest already stripped of qemu; keeping existing bundle")
            return
        sys.exit("android-bundle: no qemu in target and no bundle - run: make -C buildroot O=$PWD/out qemu-reinstall")

    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    for d in ("bin", "lib", "share"):
        os.makedirs(os.path.join(OUT, d))
    extra = [os.path.join(T, b) for b in EXTRA_BINS]
    libs = closure(bins + extra)
    ld = interp(bins[0])                       # /lib/ld-linux-aarch64.so.1
    libs[os.path.basename(ld)] = os.path.join(T, ld.lstrip("/"))
    for b in bins + extra:
        shutil.copy2(b, os.path.join(OUT, "bin", os.path.basename(b)))
    for name, lp in libs.items():              # copy under the SONAME, dereferenced
        shutil.copy2(os.path.realpath(lp), os.path.join(OUT, "lib", name))
    share = os.path.join(T, "usr/share/qemu")
    if os.path.isdir(share):
        shutil.copytree(share, os.path.join(OUT, "share/qemu"))
    print(f"android-bundle: bundled {len(libs)} libs into {OUT}")


def strip_guest():
    files = {}
    with open(os.path.join(BUILD, "packages-file-list.txt")) as f:
        for line in f:
            pkg, _, path = line.rstrip("\n").partition(",")
            if pkg in HOST_ONLY_PKGS:
                files[os.path.normpath(os.path.join(T, path))] = pkg
    candidates = set()
    for p in files:
        if not os.path.lexists(p):
            continue
        if is_lib(p) and not os.path.islink(p):
            candidates.add(os.path.realpath(p))
        elif not is_lib(p):
            os.remove(p)
    for d in ("usr/bin", "usr/libexec"):       # anything qemu installed outside the list
        dd = os.path.join(T, d)
        for f in os.listdir(dd) if os.path.isdir(dd) else []:
            if f.startswith("qemu-"):
                os.remove(os.path.join(dd, f))
    shutil.rmtree(os.path.join(T, "usr/share/qemu"), ignore_errors=True)

    elfs = []
    for root, _, fs in os.walk(T):
        for f in fs:
            p = os.path.join(root, f)
            if not os.path.islink(p) and is_elf(p) and os.path.realpath(p) not in candidates:
                elfs.append(p)
    keep = {os.path.realpath(lp) for lp in closure(elfs).values()}
    removed, kept = [], []
    for lp in sorted(candidates):
        if lp in keep:
            kept.append(os.path.basename(lp)); continue
        d = os.path.dirname(lp)
        for f in os.listdir(d):                # the file plus symlinks pointing at it
            fp = os.path.join(d, f)
            if fp == lp or (os.path.islink(fp) and os.path.realpath(fp) == lp):
                os.remove(fp)
        removed.append(os.path.basename(lp))
    for root, dirs, fs in os.walk(T, topdown=False):   # dangling links + empty share dirs
        for f in fs:
            p = os.path.join(root, f)
            if os.path.islink(p) and not os.path.exists(p) and p in files:
                os.remove(p)
    print(f"android-bundle: removed from guest: {' '.join(removed) or '(none)'}")
    if kept:
        print(f"android-bundle: still needed by the guest, kept: {' '.join(kept)}")


bundle()
strip_guest()
