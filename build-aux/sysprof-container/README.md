# Profiling Nautilus with Sysprof in a container

Builds Nautilus on Fedora rawhide (the same base as upstream CI) and records a
Sysprof capture of it running under a headless mutter, with debug symbols for
Nautilus itself (debugoptimized build), for the GNOME stack (`-debuginfo`
packages) and for anything else via debuginfod.

```sh
# From the repository root
podman build -f build-aux/sysprof-container/Containerfile -t nautilus-sysprof .
# Add --build-arg GNOME_FROM_GIT=1 if rawhide's gtk4/libadwaita are older
# than what meson.build requires; this rebuilds them like .gitlab/image-script.sh.

# --privileged is needed for perf_event_open; on the host you may also need
#   sudo sysctl kernel.perf_event_paranoid=-1
podman run --rm -it --privileged -v "$PWD:/src:z" nautilus-sysprof \
    build-aux/sysprof-container/run-profile.sh _profile/nautilus.syscap
```

The capture lands in `_profile/nautilus.syscap`; open it with `sysprof` on the
host. `PROFILE_SECONDS=60` (via `-e`) changes how long it records, and
arguments after `--` are passed to nautilus (e.g. `-- ~/Pictures`).
