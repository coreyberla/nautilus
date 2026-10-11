/*
 * SPDX-FileCopyrightText: 2022 The GNOME project contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-name-cell.h"

#include <adwaita.h>
#include <glib/gi18n.h>

#include "nautilus-directory.h"
#include "nautilus-file.h"
#include "nautilus-file-utilities.h"
#include "nautilus-image.h"
#include "nautilus-ui-utilities.h"
#include "nautilus-view-item.h"

#define SPINNER_DELAY_MS 200

struct _NautilusNameCell
{
    NautilusViewCell parent_instance;

    GSignalGroup *item_signal_group;

    GQuark path_attribute_q;
    GFile *file_path_base_location;

    GtkWidget *expander;
    GtkWidget *content;
    GtkWidget *labels_grid;
    GtkWidget *fixed_height_box;
    GtkWidget *spinner;
    GtkWidget *icon;
    GtkWidget *emblems_box;
    GtkWidget *snippet_button;
    GtkLabel *snippet;
    GtkWidget *path;

    gboolean show_snippet;
    gboolean in_file_change;
    guint loading_timeout_id;
};

G_DEFINE_FINAL_TYPE (NautilusNameCell, nautilus_name_cell, NAUTILUS_TYPE_VIEW_CELL)

static gchar *
get_path_text (NautilusFile *file,
               GQuark        path_attribute_q,
               GFile        *base_location)
{
    if (path_attribute_q == 0)
    {
        return NULL;
    }

    g_autofree gchar *path = nautilus_file_get_string_attribute_q (file, path_attribute_q);
    g_autoptr (GFile) dir_location = g_file_new_for_commandline_arg (path);

    if (base_location != NULL && g_file_equal (base_location, dir_location))
    {
        /* Only occurs when search result is
         * a direct child of the base location
         */
        return NULL;
    }

    g_autoptr (GFile) home_location = g_file_new_for_path (g_get_home_dir ());

    if (g_file_equal (dir_location, home_location))
    {
        return nautilus_compute_title_for_location (home_location);
    }

    g_autoptr (GFile) root_location = g_file_new_for_path ("/");
    GFile *relative_location_base = base_location;

    if (relative_location_base == NULL)
    {
        /* Only occurs in Recent, Starred and Trash. */
        relative_location_base = home_location;
    }

    if (!g_file_equal (relative_location_base, root_location) &&
        g_file_has_prefix (dir_location, relative_location_base))
    {
        g_autofree gchar *relative_path = NULL;
        g_autofree gchar *display_name = NULL;

        relative_path = g_file_get_relative_path (relative_location_base, dir_location);
        display_name = g_filename_display_name (relative_path);

        /* Ensure a trailing slash to emphasize it is a directory */
        if (g_str_has_suffix (display_name, G_DIR_SEPARATOR_S))
        {
            return g_steal_pointer (&display_name);
        }

        return g_strconcat (display_name, G_DIR_SEPARATOR_S, NULL);
    }

    return g_steal_pointer (&path);
}

static void popover_show_cb (NautilusNameCell *self);

/* Layout of labels_grid */
enum
{
    NAME_COLUMN,
    EMBLEMS_COLUMN,
    SPINNER_COLUMN,
    N_COLUMNS,
};
enum
{
    NAME_ROW,
    PATH_ROW,
};

static PangoAttrList *
new_no_hyphens_attributes (void)
{
    PangoAttrList *attributes = pango_attr_list_new ();

    pango_attr_list_insert (attributes, pango_attr_insert_hyphens_new (FALSE));

    return attributes;
}

/* The path, snippet and spinner are only needed in some cases, e.g. search,
 * so only create them when needed. Every row of the list view creates a cell,
 * and they are recreated whenever another folder is loaded. */
static void
ensure_path (NautilusNameCell *self)
{
    g_autoptr (PangoAttrList) attributes = NULL;

    if (self->path != NULL)
    {
        return;
    }

    attributes = new_no_hyphens_attributes ();
    self->path = gtk_label_new (NULL);
    gtk_label_set_ellipsize (GTK_LABEL (self->path), PANGO_ELLIPSIZE_START);
    gtk_label_set_justify (GTK_LABEL (self->path), GTK_JUSTIFY_LEFT);
    gtk_label_set_xalign (GTK_LABEL (self->path), 0.0);
    gtk_label_set_attributes (GTK_LABEL (self->path), attributes);
    gtk_widget_set_halign (self->path, GTK_ALIGN_FILL);
    gtk_widget_add_css_class (self->path, "caption");
    gtk_widget_add_css_class (self->path, "dim-label");
    gtk_grid_attach (GTK_GRID (self->labels_grid), self->path,
                     NAME_COLUMN, PATH_ROW, N_COLUMNS, 1);
}

static void
ensure_snippet (NautilusNameCell *self)
{
    GtkWidget *popover;

    if (self->snippet_button != NULL)
    {
        return;
    }

    self->snippet = GTK_LABEL (gtk_label_new (NULL));
    gtk_label_set_ellipsize (self->snippet, PANGO_ELLIPSIZE_NONE);
    gtk_label_set_justify (self->snippet, GTK_JUSTIFY_LEFT);
    gtk_label_set_max_width_chars (self->snippet, 65);
    gtk_label_set_lines (self->snippet, 10);
    gtk_label_set_wrap (self->snippet, TRUE);
    gtk_label_set_wrap_mode (self->snippet, PANGO_WRAP_WORD);

    popover = gtk_popover_new ();
    gtk_popover_set_child (GTK_POPOVER (popover), GTK_WIDGET (self->snippet));
    g_signal_connect_swapped (popover, "show", G_CALLBACK (popover_show_cb), self);

    self->snippet_button = gtk_menu_button_new ();
    gtk_widget_set_tooltip_text (self->snippet_button, _("Full Text Match"));
    gtk_menu_button_set_icon_name (GTK_MENU_BUTTON (self->snippet_button), "quotation-symbolic");
    gtk_menu_button_set_popover (GTK_MENU_BUTTON (self->snippet_button), popover);
    gtk_widget_set_valign (self->snippet_button, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (self->snippet_button, "fts-snippet");
    gtk_box_append (GTK_BOX (self->content), self->snippet_button);
}

static void
ensure_spinner (NautilusNameCell *self)
{
    if (self->spinner != NULL)
    {
        return;
    }

    self->spinner = adw_spinner_new ();
    gtk_widget_set_halign (self->spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (self->spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_visible (self->spinner, FALSE);
    gtk_grid_attach (GTK_GRID (self->labels_grid), self->spinner, SPINNER_COLUMN, NAME_ROW, 1, 1);
}

static void
ensure_emblems_box (NautilusNameCell *self)
{
    if (self->emblems_box != NULL)
    {
        return;
    }

    self->emblems_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign (self->emblems_box, GTK_ALIGN_START);
    gtk_widget_add_css_class (self->emblems_box, "dim-label");
    gtk_grid_attach (GTK_GRID (self->labels_grid), self->emblems_box, EMBLEMS_COLUMN, NAME_ROW, 1, 1);
}

static void
update_labels (NautilusNameCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;
    g_autofree gchar *path_text = NULL;
    const gchar *fts_snippet = NULL;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);

    path_text = get_path_text (file,
                               self->path_attribute_q,
                               self->file_path_base_location);
    if (self->show_snippet)
    {
        fts_snippet = nautilus_file_get_search_fts_snippet (file);
    }

    if (path_text != NULL)
    {
        ensure_path (self);
    }
    if (self->path != NULL)
    {
        gtk_label_set_text (GTK_LABEL (self->path), path_text);
        gtk_widget_set_visible (self->path, (path_text != NULL));
    }

    if (fts_snippet != NULL)
    {
        ensure_snippet (self);
    }
    if (self->snippet_button != NULL)
    {
        if (fts_snippet != NULL &&
            !g_str_equal (gtk_label_get_text (self->snippet), fts_snippet))
        {
            gtk_label_set_markup (self->snippet, fts_snippet);
        }
        gtk_widget_set_visible (self->snippet_button, (fts_snippet != NULL));
    }
}

static void
update_icon (NautilusNameCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    gboolean is_cut;

    g_return_if_fail (item != NULL);

    g_object_get (item, "is-cut", &is_cut, NULL);

    if (is_cut)
    {
        gtk_widget_set_visible (self->icon, FALSE);
        gtk_widget_remove_css_class (self->icon, "hidden-file");

        return;
    }

    guint icon_size;
    g_autoptr (GdkPaintable) icon_paintable = NULL;
    NautilusFile *file = nautilus_view_item_get_file (item);
    gint scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));
    NautilusFileIconFlags flags = NAUTILUS_FILE_ICON_FLAGS_NONE;
    gboolean show_thumbnail;

    g_object_get (self, "icon-size", &icon_size, NULL);
    icon_paintable = nautilus_file_get_icon_paintable (file, icon_size, scale_factor, flags);
    show_thumbnail = icon_size >= NAUTILUS_THUMBNAIL_MINIMUM_ICON_SIZE &&
                     nautilus_file_should_show_thumbnail (file);

    gtk_widget_set_visible (self->icon, TRUE);
    nautilus_image_set_size (NAUTILUS_IMAGE (self->icon), icon_size);
    nautilus_image_set_fallback (NAUTILUS_IMAGE (self->icon), icon_paintable);

    if (self->in_file_change ||
        !show_thumbnail)
    {
        nautilus_image_set_source (NAUTILUS_IMAGE (self->icon), NULL);
    }

    if (show_thumbnail)
    {
        g_autoptr (GFile) location = nautilus_file_get_location (file);

        nautilus_image_set_source (NAUTILUS_IMAGE (self->icon), location);
    }

    if (nautilus_file_is_hidden_file (file))
    {
        gtk_widget_add_css_class (self->icon, "hidden-file");
    }
    else
    {
        gtk_widget_remove_css_class (self->icon, "hidden-file");
    }
}

static void
update_emblems (NautilusNameCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;
    GtkWidget *child;
    GtkIconTheme *theme;
    g_autolist (GIcon) emblems = NULL;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);

    /* Remove old emblems. */
    while (self->emblems_box != NULL &&
           (child = gtk_widget_get_first_child (self->emblems_box)) != NULL)
    {
        gtk_box_remove (GTK_BOX (self->emblems_box), child);
    }

    theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
    emblems = nautilus_file_get_emblem_icons (file);
    if (emblems != NULL)
    {
        ensure_emblems_box (self);
    }
    for (GList *l = emblems; l != NULL; l = l->next)
    {
        if (!gtk_icon_theme_has_gicon (theme, l->data))
        {
            g_autofree gchar *icon_string = g_icon_to_string (l->data);
            g_warning ("Failed to add emblem. “%s” not found in the icon theme",
                       icon_string);
            continue;
        }

        gtk_box_append (GTK_BOX (self->emblems_box),
                        gtk_image_new_from_gicon (l->data));
    }
}

static void
on_file_changed (NautilusNameCell *self)
{
    self->in_file_change = TRUE;

    update_icon (self);
    update_labels (self);
    update_emblems (self);

    self->in_file_change = FALSE;
}

static void
on_icon_size_changed (NautilusNameCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));

    if (item == NULL)
    {
        /* Cell is not bound to an item yet. Do nothing. */
        return;
    }

    update_icon (self);
    gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
on_item_drag_accept_changed (NautilusNameCell *self)
{
    gboolean drag_accept;
    g_autoptr (NautilusViewItem) item = NULL;
    GtkWidget *list_row = gtk_widget_get_parent (gtk_widget_get_parent (GTK_WIDGET (self)));

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_object_get (item, "drag-accept", &drag_accept, NULL);
    if (drag_accept)
    {
        gtk_widget_set_state_flags (list_row, GTK_STATE_FLAG_DROP_ACTIVE, FALSE);
    }
    else
    {
        gtk_widget_unset_state_flags (list_row, GTK_STATE_FLAG_DROP_ACTIVE);
    }
}

static void
on_loading_timeout (gpointer user_data)
{
    NautilusNameCell *self = NAUTILUS_NAME_CELL (user_data);
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    gboolean is_loading = nautilus_view_item_get_loading (item);

    self->loading_timeout_id = 0;

    if (is_loading)
    {
        ensure_spinner (self);
        gtk_widget_set_visible (self->spinner, TRUE);
    }
}

static void
on_item_is_loading_changed (NautilusNameCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    gboolean is_loading = nautilus_view_item_get_loading (item);

    g_clear_handle_id (&self->loading_timeout_id, g_source_remove);

    if (is_loading)
    {
        self->loading_timeout_id = g_timeout_add_once (SPINNER_DELAY_MS,
                                                       on_loading_timeout,
                                                       self);
    }
    else
    {
        g_autoptr (NautilusDirectory) directory = nautilus_directory_get_for_file (nautilus_view_item_get_file (item));

        gtk_tree_expander_set_hide_expander (GTK_TREE_EXPANDER (self->expander),
                                             !nautilus_directory_is_not_empty (directory));

        if (self->spinner != NULL)
        {
            gtk_widget_set_visible (self->spinner, FALSE);
        }
    }
}

static void
popover_show_cb (NautilusNameCell *self)
{
    const char *label = gtk_label_get_label (self->snippet);

    gtk_accessible_announce (GTK_ACCESSIBLE (self),
                             label,
                             GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
}

static gboolean
on_label_query_tooltip (GtkWidget  *widget,
                        int         x,
                        int         y,
                        gboolean    keyboard_tip,
                        GtkTooltip *tooltip,
                        gpointer    user_data)
{
    GtkLabel *label = GTK_LABEL (widget);

    if (pango_layout_is_ellipsized (gtk_label_get_layout (label)))
    {
        gtk_tooltip_set_text (tooltip, gtk_label_get_text (label));

        return TRUE;
    }

    return FALSE;
}

static void
nautilus_name_cell_init (NautilusNameCell *self)
{
    gtk_widget_init_template (GTK_WIDGET (self));

    g_signal_connect (self, "notify::icon-size",
                      G_CALLBACK (on_icon_size_changed), NULL);
    g_signal_connect (self, "notify::scale-factor", G_CALLBACK (on_icon_size_changed), NULL);

    /* Connect automatically to an item. */
    self->item_signal_group = g_signal_group_new (NAUTILUS_TYPE_VIEW_ITEM);
    g_signal_group_connect_swapped (self->item_signal_group, "notify::drag-accept",
                                    (GCallback) on_item_drag_accept_changed, self);
    g_signal_group_connect_swapped (self->item_signal_group, "notify::is-cut",
                                    (GCallback) update_icon, self);
    g_signal_group_connect_swapped (self->item_signal_group, "notify::loading",
                                    (GCallback) on_item_is_loading_changed, self);
    g_signal_group_connect_swapped (self->item_signal_group, "file-changed",
                                    (GCallback) on_file_changed, self);
    g_signal_connect_object (self->item_signal_group, "bind",
                             (GCallback) on_file_changed, self,
                             G_CONNECT_SWAPPED);

    g_object_bind_property (self, "item",
                            self->item_signal_group, "target",
                            G_BINDING_SYNC_CREATE);
}

static void
nautilus_name_cell_dispose (GObject *object)
{
    NautilusNameCell *self = (NautilusNameCell *) object;

    g_clear_handle_id (&self->loading_timeout_id, g_source_remove);
    g_clear_object (&self->item_signal_group);

    gtk_widget_dispose_template (GTK_WIDGET (self), NAUTILUS_TYPE_NAME_CELL);

    G_OBJECT_CLASS (nautilus_name_cell_parent_class)->dispose (object);
}

static void
nautilus_name_cell_finalize (GObject *object)
{
    NautilusNameCell *self = (NautilusNameCell *) object;

    g_clear_object (&self->file_path_base_location);
    G_OBJECT_CLASS (nautilus_name_cell_parent_class)->finalize (object);
}

static void
snapshot (GtkWidget   *widget,
          GtkSnapshot *snapshot)
{
    NautilusNameCell *self = NAUTILUS_NAME_CELL (widget);
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    gboolean is_cut;

    g_object_get (item, "is-cut", &is_cut, NULL);

    if (is_cut)
    {
        graphene_rect_t dash_bounds;

        if (gtk_widget_compute_bounds (self->fixed_height_box, widget, &dash_bounds))
        {
            AdwStyleManager *style_manager = adw_style_manager_get_default ();
            gboolean is_high_contrast = adw_style_manager_get_high_contrast (style_manager);
            guint icon_size;
            GdkRGBA color, dashed_border_color, icon_color;
            gboolean use_small_icon;
            gchar *resource;
            g_autoptr (GtkSvg) svg = NULL;
            const double border_opacity = is_high_contrast ? 0.5 : 0.15;
            const double dim_opacity = is_high_contrast ? 0.9 : 0.55;
            graphene_rect_t icon_bounds = dash_bounds;

            g_object_get (self, "icon-size", &icon_size, NULL);
            use_small_icon = icon_size <= NAUTILUS_LIST_ICON_SIZE_MEDIUM;
            resource = use_small_icon
                        ? "/org/gnome/nautilus/icons/scalable/actions/cut-symbolic.svg"
                        : "/org/gnome/nautilus/icons/scalable/actions/cut-large-symbolic.svg";
            svg = gtk_svg_new_from_resource (resource);
            gtk_widget_get_color (widget, &color);

            if (icon_size >= NAUTILUS_THUMBNAIL_MINIMUM_ICON_SIZE)
            {
                dashed_border_color = color;
                dashed_border_color.alpha *= border_opacity;
                nautilus_ui_draw_icon_dashed_border (snapshot, &dash_bounds, dashed_border_color);

                graphene_rect_inset_r (&dash_bounds,
                                       0.2 * dash_bounds.size.width,
                                       0.2 * dash_bounds.size.height,
                                       &icon_bounds);
            }

            icon_color = color;
            icon_color.alpha *= dim_opacity;
            nautilus_ui_draw_svg (snapshot, svg, &icon_bounds, icon_color);
        }
        else
        {
            g_warning ("Could not compute icon bounds in cell coordinates.");
        }
    }

    GTK_WIDGET_CLASS (nautilus_name_cell_parent_class)->snapshot (widget, snapshot);
}

static void
nautilus_name_cell_class_init (NautilusNameCellClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->dispose = nautilus_name_cell_dispose;
    object_class->finalize = nautilus_name_cell_finalize;

    widget_class->snapshot = snapshot;

    g_type_ensure (NAUTILUS_TYPE_IMAGE);

    gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BOX_LAYOUT);
    gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/nautilus/ui/nautilus-name-cell.ui");

    /* Needs to add the direct children of the template widget to dispose them
     * since a plain GtkWidget doesn't dispose its children automatically. */
    gtk_widget_class_bind_template_child (widget_class, NautilusNameCell, expander);
    gtk_widget_class_bind_template_child (widget_class, NautilusNameCell, content);
    gtk_widget_class_bind_template_child (widget_class, NautilusNameCell, fixed_height_box);
    gtk_widget_class_bind_template_child (widget_class, NautilusNameCell, icon);
    gtk_widget_class_bind_template_child (widget_class, NautilusNameCell, labels_grid);

    gtk_widget_class_bind_template_callback (widget_class, on_label_query_tooltip);
}

NautilusViewCell *
nautilus_name_cell_new (NautilusListBase *view)
{
    return NAUTILUS_VIEW_CELL (g_object_new (NAUTILUS_TYPE_NAME_CELL,
                                             "view", view,
                                             NULL));
}

void
nautilus_name_cell_set_path (NautilusNameCell *self,
                             GQuark            path_attribute_q,
                             GFile            *base_location)
{
    self->path_attribute_q = path_attribute_q;
    g_set_object (&self->file_path_base_location, base_location);
}

void
nautilus_name_cell_set_show_snippet (NautilusNameCell *self,
                                     gboolean          show)
{
    self->show_snippet = show;
}

GtkTreeExpander *
nautilus_name_cell_get_expander (NautilusNameCell *self)
{
    return GTK_TREE_EXPANDER (self->expander);
}

GtkWidget *
nautilus_name_cell_get_content (NautilusNameCell *self)
{
    return self->content;
}
