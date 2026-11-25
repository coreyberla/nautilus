#!/usr/bin/env python3
"""Inclusive and self breakdown of rebind samples not matched by categorize.py."""
import sys, collections
sys.path.insert(0, sys.argv[1])
import importlib.util
spec = importlib.util.spec_from_file_location("cat", sys.argv[1] + "/categorize_lib.py")

ROOT = "gtk_list_item_manager_ensure_items"
MATCH = ("gtk_label_set_text", "gtk_label_set_label", "nautilus_file_get_string_attribute_q",
         "update_icon", "update_emblems", "update_star", "on_file_changed",
         "gtk_property_expression_watch_expr_notify_cb", "gtk_expression_watch_evaluate",
         "gtk_property_expression_watch_create_closure", "g_weak_ref_get", "g_signal_group_set_target",
         "gtk_accessible_update", "gtk_at_context", "gtk_at_spi", "gtk_css_node", "gtk_widget_reposition_after")

total = 0
other = 0
incl = collections.Counter()
selfc = collections.Counter()
pairs = collections.Counter()
cur, frames = None, []


def done():
    global total, other
    if cur != "nautilus":
        return
    total += 1
    st = list(reversed(frames))  # root first
    if ROOT not in st:
        return
    if any(f in MATCH or f.startswith(MATCH) for f in st):
        return
    other += 1
    below = st[st.index(ROOT) + 1:]
    for f in set(below):
        incl[f] += 1
    if below:
        selfc[below[-1]] += 1


for line in sys.stdin:
    if not line.strip():
        done(); cur, frames = None, []
        continue
    if not line.startswith((" ", "\t")):
        cur = line.split()[0]
        continue
    p = line.split()
    if len(p) >= 2:
        frames.append(p[1].split("+")[0])

print(f"other = {100*other/total:.1f}% of main thread ({other} samples)")
print("--- inclusive (% of main thread) ---")
for f, c in incl.most_common(45):
    print(f"{100*c/total:5.1f}%  {f}")
print("--- self / leaf (% of main thread) ---")
for f, c in selfc.most_common(30):
    print(f"{100*c/total:5.1f}%  {f}")
