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

## Profiling only a folder load

`PROFILE_OPEN=/some/folder` lets nautilus start up first (`PROFILE_SETTLE`,
default 8 seconds), then has the running instance open that folder and keeps
recording for `PROFILE_SECONDS` (default 5). To look at just the load:

1. Apply `load-timing.patch`. It logs the time from starting to load a folder
   until all its files are in the view, with the process id and
   `g_get_monotonic_time ()` stamps, and when the enumeration finished.
2. Record with `-e PROFILE_OPEN=/some/folder` and note the `LOAD-TIMING` line.
3. Build `syscap-window.c` in the container (`apt-get install
   libsysprof-6-dev` on Ubuntu) and run it with the capture, the pid and the
   two stamps from that line. `TID=<pid>` restricts it to the main thread,
   `HIST=10` shows what the main thread and the workers did over time.

## Profiling scrolling

`scroll-timing.patch` makes the view scroll itself when `NAUTILUS_AUTOSCROLL`
is set to a speed in pixels per second. After `NAUTILUS_AUTOSCROLL_DELAY`
seconds (default 10) it scrolls to the bottom and back up, for
`NAUTILUS_AUTOSCROLL_PASSES` one-way passes (default 4). After each pass it
logs an `AUTOSCROLL` line with the frame rate, the worst frame, the number of
frames over 20 ms, and the pid and stamps for `syscap-window`.

```sh
git apply build-aux/sysprof-container/scroll-timing.patch
docker run ... -v /path/to/big/folder:/bigdir:ro -e PROFILE_OPEN=/bigdir \
    -e PROFILE_SECONDS=80 -e NAUTILUS_AUTOSCROLL=6000 -e NAUTILUS_AUTOSCROLL_PASSES=1 \
    nautilus-sysprof:ubuntu build-aux/sysprof-container/run-profile.sh /src/_profile/scroll.syscap
```

Each pass takes the same time whatever the frame rate. Pick a speed and folder
size that make it fit in `PROFILE_SECONDS`.

`GSK_RENDERER=cairo` (via `-e`) avoids llvmpipe compiling shaders during the
load, which otherwise dominates profiles in VMs without a GPU.

## Claude Code cloud sessions

In the cloud sandbox, Fedora hosts and gitlab.gnome.org are blocked, Docker
Hub is rate-limited, and HTTPS goes through a proxy. This sequence works there:

```sh
# 1. Start Docker
dockerd >/tmp/dockerd.log 2>&1 &

# 2. Make the Ubuntu 26.10 base image without Docker Hub
apt-get update && apt-get install -y debootstrap
ln -sf gutsy /usr/share/debootstrap/scripts/tenacious
debootstrap --variant=minbase --components=main,universe tenacious /tmp/rootfs \
    http://archive.ubuntu.com/ubuntu
tar -C /tmp/rootfs -c . | docker import - ubuntu:26.10

# 3. Build the image (plain HTTP to the Ubuntu archive goes out directly)
docker build --network host -f build-aux/sysprof-container/Containerfile.ubuntu \
    -t nautilus-sysprof:ubuntu build-aux/sysprof-container

# 4. Supply libgxdp from a release tarball
curl -O http://archive.ubuntu.com/ubuntu/pool/main/n/nautilus/nautilus_51.0.1.orig.tar.xz
tar xf nautilus_51.0.1.orig.tar.xz nautilus-51.0.1/subprojects/libgxdp
cp -r nautilus-51.0.1/subprojects/libgxdp subprojects/

# 5. Record, routing debuginfod through the proxy
docker run --rm --privileged --network host -v "$PWD:/src" -e PROFILE_SECONDS=30 \
    -e https_proxy="$HTTPS_PROXY" -e CURL_CA_BUNDLE=/ca.crt \
    -v /root/.ccr/ca-bundle.crt:/ca.crt:ro \
    nautilus-sysprof:ubuntu \
    build-aux/sysprof-container/run-profile.sh /src/_profile/nautilus.syscap
```

If the tarball in step 4 is gone from the archive, use the current
`nautilus_*.orig.tar.xz` from the same directory. Move `_profile/` out of the
tree afterwards so it is not left untracked.

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
