#!/usr/bin/env python3
"""Fails the build when the plugin needs more from the MPC's system libraries than they have.

1.6.0 did not load on the MPC: the build's newer libstdc++ headers made the .so ask for GLIBCXX_3.4.29, which the
MPC's libstdc++ does not have. This reads the .so's version needs (readelf, any architecture) and its NEEDED
libraries, and stops the release when one is past what a build known to load on MPC OS 2.15.1 asked for.

    python3 tools/check_abi.py build/deathcoast.so
"""
import re, subprocess, sys

# the newest of each that 1.6.1 (loads on MPC OS 2.15.1) needed
MAX = {"GLIBC": (2, 29), "GLIBCXX": (3, 4, 21), "CXXABI": (1, 3, 9), "GCC": (3, 5)}
LIBS = {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "libstdc++.so.6", "libgcc_s.so.1"}


def main(path):
    ver = subprocess.run(["readelf", "-V", "--wide", path], capture_output=True, text=True, check=True).stdout
    dyn = subprocess.run(["readelf", "-d", "--wide", path], capture_output=True, text=True, check=True).stdout
    bad = []
    for name, num in set(re.findall(r"Name: (GLIBCXX|GLIBC|CXXABI|GCC)_([0-9.]+)", ver)):
        v = tuple(int(x) for x in num.split("."))
        if v > MAX[name]:
            bad.append("%s_%s (the MPC has up to %s_%s)" % (name, num, name, ".".join(map(str, MAX[name]))))
    for lib in re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", dyn):
        if lib not in LIBS:
            bad.append("library %s (not on the MPC)" % lib)
    if bad:
        print("check_abi: %s would not load on the MPC:\n  " % path + "\n  ".join(sorted(bad)))
        print("Define the missing function in the plugin (as src/dsp/deathcoast.cpp does for "
              "std::__throw_bad_array_new_length) or avoid the call that needs it.")
        return 1
    print("check_abi: %s loads on the MPC (versions and libraries within 1.6.1's)" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
