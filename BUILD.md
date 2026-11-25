# Building DeathCoast (for MPC OS, 32-bit ARM)

## A. On your own computer with Docker

You need Docker (Docker Desktop on Windows/Mac), Git and Python 3. On Windows use WSL2 or Git Bash.
On Linux, or on a Mac with an Apple chip, enable 32-bit ARM emulation once if Docker does not already have it:

    docker run --privileged --rm tonistiigi/binfmt --install arm

From the project folder (the one holding `vst.json`):

    git clone -b steve-features https://github.com/saustin2010/mpc-vst-plugins
    export MPC_VST=$PWD/mpc-vst-plugins
    python3 release/fetch-library.py library          # the wavetables (needs internet)
    mkdir -p library/presets && cp presets/deathcoast.tbl library/presets/ && mkdir -p library/wavetables && cp -R wavetables/. library/wavetables/
    bash "$MPC_VST/tools/build_port.sh" vst.json      # -> build/: deathcoast.so, screen, plugin-list entry
    bash "$MPC_VST/tools/test_port.sh" vst.json       # offline test, must print PASSED

If `build_port.sh` fails, send me the last 30 lines of its output.

## B. With GitHub Actions

Put this folder in a repository of your own and run the workflow *Actions → Release (draft) → Run workflow*.
It builds the same thing and attaches a zip to a draft release.

## C. Installing on the MPC

The build produces a zip with `install.sh` (installed over SSH). For MPC OS 2.15.1 the screen has to be converted
(`TUI.json` to the 2.x format, `os_compat` to 2.15.1); the `.so` itself already fits that system (GLIBC <= 2.29).

## Tablas de onda
`wavetables/DeathCoast/` (52 tablas originales, 6 categorías) se copia a `library/wavetables/` en la compilación. Para regenerarlas: `python3 tools/make_wavetables.py`.
