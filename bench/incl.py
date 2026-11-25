import sys, collections, re
root = sys.argv[2] if len(sys.argv) > 2 else "gtk_list_item_manager_ensure_items"
pat = re.compile(sys.argv[3] if len(sys.argv) > 3 else r"^(nautilus_|update_|on_|bind_|unbind_|get_|date_|g_format_size|ureldatefmt|udat_|g_date_time|g_file_|gtk_tree_expander|gtk_icon_theme|g_icon|g_themed|get_custom_icon|get_mount_icon|is_directory|nautilus)")
total = 0; incl = collections.Counter(); cur, frames = None, []
def done():
    global total
    if cur != "nautilus": return
    total += 1
    st = list(reversed(frames))
    if root not in st: return
    below = st[st.index(root):]
    for f in set(below):
        if pat.search(f): incl[f] += 1
for line in open(sys.argv[1]):
    if not line.strip():
        done(); cur, frames = None, []; continue
    if not line.startswith((" ", "\t")):
        cur = line.split()[0]; continue
    p = line.split()
    if len(p) >= 2: frames.append(p[1].split("+")[0])
for f, c in incl.most_common(60): print(f"{100*c/total:5.2f}%  {f}")
