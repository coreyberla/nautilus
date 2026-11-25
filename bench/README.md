# List view scroll benchmark (not for upstream)

`src/nautilus-scroll-bench.c` is hooked into `nautilus_list_view_init()`. With
`NAUTILUS_SCROLL_BENCH=<px>` set, it scrolls the list view by that many pixels
every frame (bouncing at the ends) and prints frame timings, split by frame
clock phase, then exits:

- `update`: before layout; this is where GtkListView rebinds rows when the
  scroll position changes
- `layout`: measure + allocate
- `paint`: snapshot + render (mostly GPU wait with a software renderer)
- `fps`: wall-clock frames per second

Environment:

| Variable | Meaning |
|---|---|
| `NAUTILUS_SCROLL_BENCH` | pixels per frame (3000 ≈ a full page at the smallest zoom) |
| `NAUTILUS_SCROLL_BENCH_FRAMES` | frames to measure (default 600) |
| `BENCH_WARMUP` | frames to skip first (default 60) |
| `BENCH_DELAY` | ms to wait for the folder to load (default 4000) |
| `BENCH_ONEWAY` | stop at the bottom instead of bouncing (cold-cache test) |
| `BENCH_WIDTHS` | report how often column widths changed |
| `BENCH_SHOT=<png>` | save a screenshot of the window and exit |

The scripts were used in an Ubuntu 26.10 container with headless weston
(`--idle-time=0`, or it stops sending frames after 5 minutes), GTK 4.24.1
built with debug info and libadwaita main, running Nautilus as a normal
user with a11y enabled, Adwaita Sans and the smallest list zoom:

- `run.sh <px> <frames> [ENV=val...]`: one run (`NAUTILUS_BIN`, `BENCH_DIR`, `BENCH_TREE`)
- `matrix.sh`, `cmp.sh`, `ab.sh`: paired comparisons between builds
- `mkpngs.py`, `mkcold.py`, `mkdata.py`: test folders (1000 PNGs; 30000 unique names)
- `tree.py`, `categorize.py`, `incl.py`, `other.py`: `perf script` analysis
