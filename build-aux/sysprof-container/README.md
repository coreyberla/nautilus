# Profiling Nautilus with Sysprof in a container

Builds Nautilus and records a Sysprof capture of it running under a headless
mutter, with debug symbols for Nautilus itself (debugoptimized build, frame
pointers), for the GNOME stack (distro debug packages) and for anything else
via debuginfod.

Two images are provided:

- `Containerfile`: Fedora rawhide, the same base as upstream CI.
- `Containerfile.ubuntu`: Ubuntu 26.10, for when the Fedora registries and
  mirrors are not reachable. Its GTK 4.24 / libadwaita 1.10 satisfy
  `meson.build`.

```sh
# From the repository root
podman build -f build-aux/sysprof-container/Containerfile -t nautilus-sysprof .
# or
podman build -f build-aux/sysprof-container/Containerfile.ubuntu -t nautilus-sysprof .

# --privileged is needed for perf_event_open; on the host you may also need
#   sudo sysctl kernel.perf_event_paranoid=-1
podman run --rm --privileged -v "$PWD:/src:z" nautilus-sysprof \
    build-aux/sysprof-container/run-profile.sh _profile/nautilus.syscap
```

The capture lands in `_profile/nautilus.syscap`; open it with `sysprof` on the
host. `-e PROFILE_SECONDS=60` changes how long it records, and arguments after
`--` are passed to nautilus (e.g. `-- /usr/share`).

For the Fedora image, `--build-arg GNOME_FROM_GIT=1` rebuilds glib, gtk and
libadwaita from git like `.gitlab/image-script.sh` does, for when rawhide lags
behind what `meson.build` requires.

## Notes

- The libgxdp subproject is fetched from gitlab.gnome.org on first build. If
  that host is unreachable, copy `subprojects/libgxdp` from a release tarball
  (e.g. Ubuntu's `nautilus_*.orig.tar.xz`) and the build uses it as is.
- Nautilus refuses to run as root, so the script runs the session as an
  unprivileged `profiler` user and lets it use sysprofd through a polkit rule.
- Sysprof resolves paths through the container's overlay layers, which only
  exist on the host, and expects a non-standard `.build-id` layout. The script
  adds symlinks inside the container so both resolve.
- If debuginfod is configured but unreachable, Sysprof writes no symbols at
  all. Pass `-e DEBUGINFOD_URLS=` in that case.
- On machines without a hardware PMU (most VMs), Sysprof falls back to the
  software CPU clock; stacks are still complete.
- Without a GPU, GTK renders through llvmpipe, so rendering is much more
  expensive than on real hardware. Account for that when reading the profile.
