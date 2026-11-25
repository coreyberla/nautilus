/* BENCHMARK ONLY - not for upstream.
 *
 * NAUTILUS_SCROLL_BENCH=<px per frame>  scrolls the list view by that many
 * pixels every frame (bouncing at the ends), for NAUTILUS_SCROLL_BENCH_FRAMES
 * frames, then prints frame timing stats and exits.
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>

void nautilus_scroll_bench_attach (GtkWidget *scrolled_window);
#define MAX_FRAMES 4000

static gint64 t_update, t_layout, t_first_layout, t_paint;
static int n_layouts, sum_layouts, max_layouts;
static double update_ms[MAX_FRAMES], layout_ms[MAX_FRAMES], paint_ms[MAX_FRAMES], total_ms[MAX_FRAMES];
static gint64 frame_start[MAX_FRAMES];
static int n_frames, target_frames = 600, warmup = 60;
static gboolean running;
static GHashTable *widths_seen;
static int width_changes;
static void dump_widths (void);
static double step;
static int dir = 1;

static gboolean
hook_update (GSignalInvocationHint *ih, guint n, const GValue *v, gpointer d)
{
    t_update = g_get_monotonic_time ();
    n_layouts = 0;
    return TRUE;
}

static gboolean
hook_layout (GSignalInvocationHint *ih, guint n, const GValue *v, gpointer d)
{
    t_layout = g_get_monotonic_time ();
    if (n_layouts++ == 0)
    {
        t_first_layout = t_layout;
    }
    return TRUE;
}

static gboolean
hook_paint (GSignalInvocationHint *ih, guint n, const GValue *v, gpointer d)
{
    t_paint = g_get_monotonic_time ();
    return TRUE;
}

static int
cmp_double (const void *a, const void *b)
{
    double x = *(const double *) a, y = *(const double *) b;
    return (x > y) - (x < y);
}

static void
report (const char *name, double *arr, int n)
{
    double *s = g_memdup2 (arr, n * sizeof (double));
    double sum = 0;
    for (int i = 0; i < n; i++)
    {
        sum += s[i];
    }
    qsort (s, n, sizeof (double), cmp_double);
    fprintf (stderr, "BENCH %-7s mean %6.2f ms  p50 %6.2f  p90 %6.2f  p99 %6.2f\n",
             name, sum / n, s[n / 2], s[n * 9 / 10], s[n * 99 / 100]);
    g_free (s);
}

static void
after_paint (GdkFrameClock *clock, gpointer data)
{
    gint64 now = g_get_monotonic_time ();

    if (!running)
    {
        return;
    }
    if (warmup > 0)
    {
        warmup--;
        return;
    }
    layout_ms[n_frames] = (t_paint - t_first_layout) / 1000.0;
    update_ms[n_frames] = (t_first_layout - t_update) / 1000.0;
    sum_layouts += n_layouts;
    max_layouts = MAX (max_layouts, n_layouts);
    paint_ms[n_frames] = (now - t_paint) / 1000.0;
    total_ms[n_frames] = (now - t_update) / 1000.0;
    frame_start[n_frames] = t_update;
    n_frames++;

    if (n_frames >= target_frames)
    {
        double wall = (frame_start[n_frames - 1] - frame_start[0]) / 1e6;
        report ("update", update_ms, n_frames);
        report ("layout", layout_ms, n_frames);
        fprintf (stderr, "BENCH layout passes/frame avg %.2f max %d\n", (double) sum_layouts / n_frames, max_layouts);
        report ("paint", paint_ms, n_frames);
        report ("frame", total_ms, n_frames);
        if (widths_seen)
        {
            dump_widths ();
            fprintf (stderr, "BENCH column width changes: %d\n", width_changes);
        }
        fprintf (stderr, "BENCH fps %.1f (wall-clock, %d frames)\n",
                 (n_frames - 1) / wall, n_frames);
        exit (0);
    }
}



static void
collect_titles (GtkWidget *w, int *idx)
{
    for (GtkWidget *c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c))
    {
        if (g_str_equal (G_OBJECT_TYPE_NAME (c), "GtkColumnViewTitle") && gtk_widget_get_mapped (c))
        {
            static int last[16];
            int cw = gtk_widget_get_width (c);
            if (*idx < 16 && last[*idx] != 0 && last[*idx] != cw)
            {
                width_changes++;
            }
            if (*idx < 16)
            {
                last[*idx] = cw;
            }
            g_hash_table_add (widths_seen, GINT_TO_POINTER ((*idx)++ * 10000 + cw));
        }
        collect_titles (c, idx);
    }
}

static int
cmp_int (gconstpointer a, gconstpointer b)
{
    return GPOINTER_TO_INT (a) - GPOINTER_TO_INT (b);
}

static void
dump_widths (void)
{
    GList *keys = g_list_sort (g_hash_table_get_keys (widths_seen), cmp_int);
    int col = -1;
    for (GList *l = keys; l; l = l->next)
    {
        int k = GPOINTER_TO_INT (l->data);
        if (k / 10000 != col)
        {
            col = k / 10000;
            fprintf (stderr, "%sBENCH col %d widths:", col ? "\n" : "", col);
        }
        fprintf (stderr, " %d", k % 10000);
    }
    fprintf (stderr, "\n");
}

static gboolean
tick (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    if (running && g_getenv ("BENCH_WIDTHS"))
    {
        int idx = 0;
        if (!widths_seen)
        {
            widths_seen = g_hash_table_new (NULL, NULL);
        }
        collect_titles (gtk_widget_get_ancestor (widget, GTK_TYPE_WINDOW), &idx);
    }
    GtkAdjustment *adj = data;
    double v = gtk_adjustment_get_value (adj);
    double max = gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj);

    if (!running)
    {
        return G_SOURCE_CONTINUE;
    }
    if (max <= 0)
    {
        return G_SOURCE_CONTINUE;
    }
    v += dir * step;
    if (v >= max && g_getenv ("BENCH_ONEWAY"))
    {
        fprintf (stderr, "BENCH reached the end after %d frames, stopping\n", n_frames);
        target_frames = n_frames;
    }
    if (v >= max)
    {
        v = max;
        dir = -1;
    }
    else if (v <= 0)
    {
        v = 0;
        dir = 1;
    }
    gtk_adjustment_set_value (adj, v);
    return G_SOURCE_CONTINUE;
}

static void
print_titles (GtkWidget *w)
{
    for (GtkWidget *c = gtk_widget_get_first_child (w); c; c = gtk_widget_get_next_sibling (c))
    {
        if (g_str_equal (G_OBJECT_TYPE_NAME (c), "GtkColumnViewTitle") && gtk_widget_get_mapped (c))
        {
            fprintf (stderr, "BENCH column width %d\n", gtk_widget_get_width (c));
        }
        print_titles (c);
    }
}

static void
take_shot (GtkWidget *window, const char *path)
{
    GdkPaintable *paintable = gtk_widget_paintable_new (window);
    GtkSnapshot *snapshot = gtk_snapshot_new ();
    GskRenderNode *node;
    GdkTexture *texture;
    int w = gtk_widget_get_width (window), h = gtk_widget_get_height (window);

    gdk_paintable_snapshot (paintable, snapshot, w, h);
    node = gtk_snapshot_free_to_node (snapshot);
    texture = gsk_renderer_render_texture (gtk_native_get_renderer (GTK_NATIVE (window)), node,
                                           &GRAPHENE_RECT_INIT (0, 0, w, h));
    gdk_texture_save_to_png (texture, path);
    fprintf (stderr, "BENCH screenshot %s\n", path);
    g_object_unref (texture);
    gsk_render_node_unref (node);
    g_object_unref (paintable);
}

static void
start (gpointer data)
{
    if (g_getenv ("BENCH_SHOT"))
    {
        take_shot (gtk_widget_get_ancestor (data, GTK_TYPE_WINDOW), g_getenv ("BENCH_SHOT"));
        exit (0);
    }
    print_titles (gtk_widget_get_ancestor (data, GTK_TYPE_WINDOW));
    GtkWidget *sw = data;
    GdkFrameClock *clock = gtk_widget_get_frame_clock (sw);

    fprintf (stderr, "BENCH start, upper=%.0f\n",
             gtk_adjustment_get_upper (gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (sw))));
    g_signal_connect (clock, "after-paint", G_CALLBACK (after_paint), NULL);
    running = TRUE;
}

static void
on_map (GtkWidget *sw, gpointer data)
{
    static gboolean done;
    GType t = GDK_TYPE_FRAME_CLOCK;

    if (done)
    {
        return;
    }
    done = TRUE;

    g_type_class_ref (t);
    g_signal_add_emission_hook (g_signal_lookup ("update", t), 0, hook_update, NULL, NULL);
    g_signal_add_emission_hook (g_signal_lookup ("layout", t), 0, hook_layout, NULL, NULL);
    g_signal_add_emission_hook (g_signal_lookup ("paint", t), 0, hook_paint, NULL, NULL);

    gtk_widget_add_tick_callback (sw, tick,
                                  gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (sw)),
                                  NULL);
    if (g_getenv ("BENCH_WARMUP"))
    {
        warmup = atoi (g_getenv ("BENCH_WARMUP"));
    }
    g_timeout_add_once (g_getenv ("BENCH_DELAY") ? atoi (g_getenv ("BENCH_DELAY")) : 4000, start, sw);
}

void
nautilus_scroll_bench_attach (GtkWidget *scrolled_window)
{
    const char *s = g_getenv ("NAUTILUS_SCROLL_BENCH");

    if (s == NULL)
    {
        return;
    }
    step = g_ascii_strtod (s, NULL);
    if (g_getenv ("NAUTILUS_SCROLL_BENCH_FRAMES"))
    {
        target_frames = MIN (MAX_FRAMES, atoi (g_getenv ("NAUTILUS_SCROLL_BENCH_FRAMES")));
    }
    g_signal_connect (scrolled_window, "map", G_CALLBACK (on_map), NULL);
}
