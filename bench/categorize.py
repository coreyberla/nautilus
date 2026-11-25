#!/usr/bin/env python3
"""Categorize main-thread perf samples. usage: perf script -F comm,ip,sym | categorize.py"""
import sys, collections

PHASES = [
    ("Rebind rows (scroll → bind)", "gtk_list_item_manager_ensure_items", [
        ("GtkLabel set_text / set_label", ("gtk_label_set_text", "gtk_label_set_label")),
        ("Nautilus string formatting (date/size)", ("nautilus_file_get_string_attribute_q",)),
        ("Name cell icon/emblems/star update", ("update_icon", "update_emblems", "update_star", "on_file_changed")),
        ("GtkExpression watches (rebuild + eval)", ("gtk_property_expression_watch_expr_notify_cb", "gtk_expression_watch_evaluate",
                                                    "gtk_property_expression_watch_create_closure", "g_weak_ref_get")),
        ("GSignalGroup retarget (connect/disconnect)", ("g_signal_group_set_target",)),
        ("Accessibility updates", ("gtk_accessible_update", "gtk_at_context", "gtk_at_spi")),
        ("CSS restyle of moved rows", ("gtk_css_node", "gtk_widget_reposition_after")),
    ]),
    ("Layout (measure + allocate)", "gtk_window_size_allocate", [
        ("Column natural width: name cells", ("NAMECOL",)),
        ("Column natural width: size/date cells", ("gtk_column_view_column_measure",)),
        ("Row height measure", ("gtk_column_view_row_widget_measure_along", "gtk_list_view_measure")),
        ("Allocate / map rows", ("gtk_list_view_size_allocate", "gtk_column_view_allocate")),
    ]),
    ("Paint (main thread CPU)", "surface_render_cb", [
        ("Snapshot (build render nodes)", ("gtk_widget_snapshot",)),
        ("GSK render submit", ("gsk_renderer_render",)),
    ]),
]

counts = collections.Counter()
total = 0


def classify(stack):
    s = set(stack)
    for phase, root, subs in PHASES:
        if root not in s:
            continue
        for name, keys in subs:
            if keys == ("NAMECOL",):
                if "gtk_column_view_column_measure" in s and "gtk_box_layout_measure" in s:
                    return phase, name
                continue
            for k in keys:
                if k in s or any(f.startswith(k) for f in stack):
                    return phase, name
        return phase, "other"
    return "Other main-thread work / truncated stacks", ""


cur, frames = None, []
for line in sys.stdin:
    if not line.strip():
        if cur == "nautilus":
            total += 1
            counts[classify(frames)] += 1
        cur, frames = None, []
        continue
    if not line.startswith((" ", "\t")):
        cur = line.split()[0]
        continue
    p = line.split()
    if len(p) >= 2:
        frames.append(p[1].split("+")[0])

phase_tot = collections.Counter()
for (ph, sub), c in counts.items():
    phase_tot[ph] += c
order = [p[0] for p in PHASES] + ["Other main-thread work / truncated stacks"]
for ph in order:
    print(f"P|{ph}|{100*phase_tot[ph]/total:.1f}")
    subs = [p[2] for p in PHASES if p[0] == ph]
    names = [n for n, _ in subs[0]] + ["other"] if subs else []
    for n in names:
        c = counts[(ph, n)]
        if c:
            print(f"S|{n}|{100*c/total:.1f}")
print(f"T|{total}")
