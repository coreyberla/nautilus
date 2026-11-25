#!/usr/bin/env python3
"""Fold `perf script` output and print a call tree under a root function.
usage: perf script -i X | tree.py ROOT [max_depth] [min_pct]
Percentages are of all samples of the 'nautilus' main thread."""
import sys, collections, re

root = sys.argv[1]
max_depth = int(sys.argv[2]) if len(sys.argv) > 2 else 6
min_pct = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0

stacks = collections.Counter()
total = 0
cur = None
frames = []
skip = re.compile(r"gtk_list_item_base_update|gtk_column_view_row_widget_update|gtk_list_factory_widget_update|gtk_signal_list_item_factory_update|do_notify|gtk_expression_bind_notify|g_signal_group_set_target|gtk_box_layout_compute|query_size_for|gtk_layout_manager_measure|gtk_bin_layout_measure|^0x[0-9a-f]+$|^\[unknown\]$|_marshal|^g_closure_invoke$|^g_signal_emit|signal_emit_unlocked_R|^g_object_notify|notify_by_pspec|^g_object_dispatch|^g_object_set(v|_property)?$|^g_object_unref$|^object_set_property|^g_cclosure|inlined")

def flush():
    global total
    if cur is None:
        return
    if cur == "nautilus":
        total += 1
        fs = [f for f in reversed(frames)]
        stacks[tuple(fs)] += 1

for line in sys.stdin:
    if not line.strip():
        flush(); cur = None; frames = []
        continue
    if not line.startswith((" ", "\t")):
        cur = line.split()[0]
        continue
    parts = line.split()
    if len(parts) >= 2:
        sym = parts[1].split("+")[0]
        if "(inlined)" in line:
            continue
        frames.append(sym)
flush()

class Node:
    __slots__ = ("n", "kids")
    def __init__(self):
        self.n = 0
        self.kids = collections.defaultdict(Node)

top = Node()
for st, c in stacks.items():
    try:
        i = st.index(root)
    except ValueError:
        continue
    node = top
    node.n += c
    seen = set()
    for f in st[i + 1:]:
        if skip.search(f):
            continue
        node = node.kids[f]
        node.n += c

def pr(node, name, d):
    pct = 100.0 * node.n / total
    if pct < min_pct:
        return
    print("%s%5.1f%% %s" % ("  " * d, pct, name))
    if d >= max_depth:
        return
    for k, v in sorted(node.kids.items(), key=lambda kv: -kv[1].n):
        pr(v, k, d + 1)

pr(top, root, 0)
