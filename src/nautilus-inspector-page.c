/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* A page in the GTK Inspector showing statistics about Files. The view
 * statistics follow the view of the active tab in the most recently focused
 * window, or the view containing the widget picked in the Objects page.
 * Statistics are only collected while the inspector is shown, see
 * nautilus-inspector-stats.h. */

#define G_LOG_DOMAIN "nautilus-inspector-page"

#include "nautilus-inspector-page.h"

#include "nautilus-files-view.h"
#include "nautilus-inspector-stats.h"
#include "nautilus-window.h"
#include "nautilus-window-slot.h"

#include <gtk/gtk.h>

#define REFRESH_INTERVAL_MS 250

struct _NautilusInspectorPage
{
    AdwBin parent_instance;

    GObject *object;
    guint refresh_id;
    GtkWidget *inspector_window;

    /* View */
    AdwPreferencesGroup *view_group;
    GtkLabel *location_label;
    GtkLabel *state_label;
    GtkLabel *items_label;
    GtkLabel *loads_label;

    AdwPreferencesGroup *load_group;
    GtkLabel *attributes_label;
    GtkLabel *first_items_label;
    GtkLabel *total_time_label;
    GtkLabel *batches_label;
    GtkLabel *sort_label;
    GtkLabel *splice_label;
};

enum
{
    PROP_0,
    PROP_TITLE,
    PROP_OBJECT,
    N_PROPS,
};

G_DEFINE_FINAL_TYPE (NautilusInspectorPage, nautilus_inspector_page, ADW_TYPE_BIN)

static GtkLabel *
add_row (AdwPreferencesGroup *group,
         const char          *title)
{
    GtkWidget *row = adw_action_row_new ();
    GtkWidget *label = gtk_label_new (NULL);

    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), title);
    gtk_label_set_selectable (GTK_LABEL (label), TRUE);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_add_css_class (label, "monospace");
    adw_action_row_add_suffix (ADW_ACTION_ROW (row), label);
    adw_preferences_group_add (group, row);

    return GTK_LABEL (label);
}

static void
set_row_subtitle (GtkLabel   *label,
                  const char *subtitle)
{
    GtkWidget *row = gtk_widget_get_ancestor (GTK_WIDGET (label), ADW_TYPE_ACTION_ROW);

    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);
}

static AdwPreferencesGroup *
add_group (AdwPreferencesPage *page,
           const char         *title,
           const char         *description)
{
    GtkWidget *group = adw_preferences_group_new ();

    adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), title);
    adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (group), description);
    adw_preferences_page_add (page, ADW_PREFERENCES_GROUP (group));

    return ADW_PREFERENCES_GROUP (group);
}

static AdwPreferencesPage *
add_page (AdwViewStack *stack,
          const char   *name,
          const char   *title,
          const char   *icon_name)
{
    GtkWidget *page = adw_preferences_page_new ();

    adw_view_stack_add_titled_with_icon (stack, page, name, title, icon_name);

    return ADW_PREFERENCES_PAGE (page);
}

static void
set_label_uint (GtkLabel *label,
                guint64   value)
{
    g_autofree char *text = g_strdup_printf ("%" G_GUINT64_FORMAT, value);

    gtk_label_set_label (label, text);
}

/* While running and end is not reached yet, shows the time so far with an
 * ellipsis, so it keeps counting up. */
static void
set_label_duration (GtkLabel *label,
                    gint64    start,
                    gint64    end,
                    gboolean  running)
{
    if (start == 0 || (end == 0 && !running))
    {
        gtk_label_set_label (label, "—");
    }
    else
    {
        gboolean reached = end != 0;
        g_autofree char *text = g_strdup_printf ("%.1f ms%s",
                                                 ((reached ? end : g_get_monotonic_time ()) - start) / 1000.0,
                                                 reached ? "" : "…");

        gtk_label_set_label (label, text);
    }
}

static void
set_label_ms (GtkLabel *label,
              gint64    duration)
{
    g_autofree char *text = g_strdup_printf ("%.1f ms", duration / 1000.0);

    gtk_label_set_label (label, text);
}

static NautilusFilesView *
get_picked_view (NautilusInspectorPage *self)
{
    if (!GTK_IS_WIDGET (self->object))
    {
        return NULL;
    }

    GtkWidget *view = gtk_widget_get_ancestor (GTK_WIDGET (self->object), NAUTILUS_TYPE_FILES_VIEW);

    return view != NULL ? NAUTILUS_FILES_VIEW (view) : NULL;
}

static NautilusFilesView *
get_active_tab_view (void)
{
    GtkApplication *app = GTK_APPLICATION (g_application_get_default ());

    /* Windows are sorted by most recently focused. */
    for (GList *l = gtk_application_get_windows (app); l != NULL; l = l->next)
    {
        if (NAUTILUS_IS_WINDOW (l->data))
        {
            g_autoptr (NautilusWindowSlot) slot = NULL;

            g_object_get (l->data, "active-slot", &slot, NULL);

            return slot != NULL ? nautilus_window_slot_get_current_view (slot) : NULL;
        }
    }

    return NULL;
}

static void
update_view_stats (NautilusInspectorPage *self,
                   NautilusFilesView     *view,
                   gboolean               picked)
{
    const NautilusFilesViewLoadStats *stats = nautilus_files_view_get_load_stats (view);
    GFile *location = nautilus_files_view_get_location (view);
    g_autofree char *uri = location != NULL ? g_file_get_uri (location) : NULL;
    gboolean loading = nautilus_files_view_is_loading (view);

    adw_preferences_group_set_description (self->view_group,
                                           picked ?
                                           "Showing the view containing the selected object" :
                                           "Showing the view of the active tab");
    gtk_label_set_label (self->location_label, uri != NULL ? uri : "—");
    gtk_label_set_label (self->state_label,
                         loading ? "Loading" :
                                   stats->n_loads == 0 ? "Idle" :
                                                         stats->all_files_seen ? "Done" : "Stopped");
    set_label_uint (self->items_label, nautilus_files_view_get_n_items (view));
    set_label_uint (self->loads_label, stats->n_loads);

    adw_preferences_group_set_description (self->load_group,
                                           stats->n_loads > 0 && stats->start_time == 0 ?
                                           "Not timed, the inspector was hidden when it started" :
                                           "Time since the load started");

    set_label_duration (self->attributes_label, stats->start_time, stats->attributes_time, loading);
    set_label_duration (self->first_items_label, stats->start_time, stats->first_items_time, loading);
    set_label_duration (self->total_time_label, stats->start_time, loading ? 0 : stats->end_time, loading);

    if (stats->start_time != 0)
    {
        set_label_uint (self->batches_label, stats->n_batches);
        set_label_ms (self->sort_label, stats->sort_duration);
        set_label_ms (self->splice_label, stats->splice_duration);
    }
    else
    {
        gtk_label_set_label (self->batches_label, "—");
        gtk_label_set_label (self->sort_label, "—");
        gtk_label_set_label (self->splice_label, "—");
    }
}

static void
clear_view_stats (NautilusInspectorPage *self)
{
    GtkLabel *labels[] =
    {
        self->location_label, self->state_label, self->items_label, self->loads_label,
        self->attributes_label, self->first_items_label, self->total_time_label,
        self->batches_label, self->sort_label, self->splice_label,
    };

    adw_preferences_group_set_description (self->view_group, "No view found");
    for (guint i = 0; i < G_N_ELEMENTS (labels); i++)
    {
        gtk_label_set_label (labels[i], "—");
    }
}

static gboolean
refresh (gpointer user_data)
{
    NautilusInspectorPage *self = user_data;
    NautilusFilesView *picked_view = get_picked_view (self);
    NautilusFilesView *view = picked_view != NULL ? picked_view : get_active_tab_view ();

    if (view != NULL)
    {
        update_view_stats (self, view, picked_view != NULL);
    }
    else
    {
        clear_view_stats (self);
    }

    return G_SOURCE_CONTINUE;
}

static void
build_view_page (NautilusInspectorPage *self,
                 AdwPreferencesPage    *page)
{
    AdwPreferencesGroup *group;

    self->view_group = add_group (page, "View", NULL);
    self->location_label = add_row (self->view_group, "Location");
    self->state_label = add_row (self->view_group, "State");
    self->items_label = add_row (self->view_group, "Items");
    self->loads_label = add_row (self->view_group, "Loads in This View");

    self->load_group = group = add_group (page, "Last Load", NULL);
    self->attributes_label = add_row (group, "Files Listed With Attributes");
    self->first_items_label = add_row (group, "First Items Shown");
    self->total_time_label = add_row (group, "Done");

    group = add_group (page, "Adding Items", "Time spent in the view model during the last load");
    self->batches_label = add_row (group, "Batches");
    self->sort_label = add_row (group, "Sorting Batches");
    self->splice_label = add_row (group, "Splicing Into Model");
    set_row_subtitle (self->splice_label, "Includes re-sorting the view and updating the widgets");
}

static void
on_inspector_visible_changed (NautilusInspectorPage *self)
{
    nautilus_inspector_stats_set_shown (gtk_widget_get_visible (self->inspector_window));
}

static void
nautilus_inspector_page_root (GtkWidget *widget)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (widget);

    GTK_WIDGET_CLASS (nautilus_inspector_page_parent_class)->root (widget);

    /* Closing the inspector only hides its window. */
    self->inspector_window = GTK_WIDGET (gtk_widget_get_root (widget));
    g_signal_connect_object (self->inspector_window, "notify::visible",
                             G_CALLBACK (on_inspector_visible_changed), self,
                             G_CONNECT_SWAPPED);
    on_inspector_visible_changed (self);
}

static void
nautilus_inspector_page_unroot (GtkWidget *widget)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (widget);

    g_signal_handlers_disconnect_by_data (self->inspector_window, self);
    self->inspector_window = NULL;
    nautilus_inspector_stats_set_shown (FALSE);

    GTK_WIDGET_CLASS (nautilus_inspector_page_parent_class)->unroot (widget);
}

static void
nautilus_inspector_page_map (GtkWidget *widget)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (widget);

    GTK_WIDGET_CLASS (nautilus_inspector_page_parent_class)->map (widget);

    /* Only poll while the page is shown. */
    refresh (self);
    self->refresh_id = g_timeout_add (REFRESH_INTERVAL_MS, refresh, self);
}

static void
nautilus_inspector_page_unmap (GtkWidget *widget)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (widget);

    g_clear_handle_id (&self->refresh_id, g_source_remove);

    GTK_WIDGET_CLASS (nautilus_inspector_page_parent_class)->unmap (widget);
}

static void
nautilus_inspector_page_get_property (GObject    *object,
                                      guint       property_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (object);

    switch (property_id)
    {
        case PROP_TITLE:
        {
            g_value_set_string (value, "Files");
        }
        break;

        case PROP_OBJECT:
        {
            g_value_set_object (value, self->object);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
nautilus_inspector_page_set_property (GObject      *object,
                                      guint         property_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (object);

    switch (property_id)
    {
        case PROP_OBJECT:
        {
            g_set_object (&self->object, g_value_get_object (value));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        }
    }
}

static void
nautilus_inspector_page_dispose (GObject *object)
{
    NautilusInspectorPage *self = NAUTILUS_INSPECTOR_PAGE (object);

    g_clear_handle_id (&self->refresh_id, g_source_remove);
    g_clear_object (&self->object);

    G_OBJECT_CLASS (nautilus_inspector_page_parent_class)->dispose (object);
}

static void
nautilus_inspector_page_init (NautilusInspectorPage *self)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    AdwViewStack *stack = ADW_VIEW_STACK (adw_view_stack_new ());
    GtkWidget *switcher = adw_view_switcher_new ();

    build_view_page (self, add_page (stack, "view", "View", "folder-symbolic"));

    adw_view_switcher_set_stack (ADW_VIEW_SWITCHER (switcher), stack);
    adw_view_switcher_set_policy (ADW_VIEW_SWITCHER (switcher), ADW_VIEW_SWITCHER_POLICY_WIDE);
    gtk_widget_set_halign (switcher, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top (switcher, 6);
    gtk_widget_set_margin_bottom (switcher, 6);
    gtk_widget_set_vexpand (GTK_WIDGET (stack), TRUE);

    gtk_box_append (GTK_BOX (box), switcher);
    gtk_box_append (GTK_BOX (box), GTK_WIDGET (stack));
    adw_bin_set_child (ADW_BIN (self), box);
}

static void
nautilus_inspector_page_class_init (NautilusInspectorPageClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->get_property = nautilus_inspector_page_get_property;
    object_class->set_property = nautilus_inspector_page_set_property;
    object_class->dispose = nautilus_inspector_page_dispose;

    widget_class->map = nautilus_inspector_page_map;
    widget_class->unmap = nautilus_inspector_page_unmap;
    widget_class->root = nautilus_inspector_page_root;
    widget_class->unroot = nautilus_inspector_page_unroot;

    /* These are the properties GtkInspector looks for on extension pages. */
    g_object_class_install_property (object_class,
                                     PROP_TITLE,
                                     g_param_spec_string ("title", NULL, NULL,
                                                          NULL,
                                                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (object_class,
                                     PROP_OBJECT,
                                     g_param_spec_object ("object", NULL, NULL,
                                                          G_TYPE_OBJECT,
                                                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

void
nautilus_inspector_page_register (void)
{
    /* The extension point is registered by gtk_init(). */
    if (g_io_extension_point_lookup ("gtk-inspector-page") != NULL)
    {
        g_io_extension_point_implement ("gtk-inspector-page",
                                        NAUTILUS_TYPE_INSPECTOR_PAGE,
                                        "nautilus",
                                        10);
    }
}
