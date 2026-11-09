/*
 * SPDX-FileCopyrightText: 2022 António Fernandes <antoniof@gnome.org>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-label-cell.h"

/* Needed for NautilusColumn (full GType). */
#include <nautilus-extension.h>

#include "nautilus-file.h"
#include "nautilus-global-preferences.h"
#include "nautilus-view-item.h"

struct _NautilusLabelCell
{
    NautilusViewCell parent_instance;

    GSignalGroup *item_signal_group;

    NautilusColumn *column;
    GQuark attribute_q;

    GtkInscription *inscription;
    PangoLayout *measure_layout;

    NautilusColumnWidth *column_width;
    int text_width;
    gboolean needs_measure;
    guint resize_tick_id;
};

/* Grows while scrolling, and shrinks to the cells on screen once it stops. */
struct _NautilusColumnWidth
{
    int widest_text;
    /* Lags widest_text until the column is resized for it, as cells wider than
     * the column's cached width would overflow into the next column. */
    int width;
    GHashTable *text_widths;
    GHashTable *mapped_cells;
    gint64 last_measure_time;
    guint shrink_timeout_id;
};

#define MAX_CACHED_WIDTHS 10000
#define SHRINK_DELAY_MS 500

NautilusColumnWidth *
nautilus_column_width_new (void)
{
    NautilusColumnWidth *self = g_new0 (NautilusColumnWidth, 1);

    self->text_widths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    self->mapped_cells = g_hash_table_new (NULL, NULL);

    return self;
}

void
nautilus_column_width_free (NautilusColumnWidth *self)
{
    g_clear_handle_id (&self->shrink_timeout_id, g_source_remove);
    g_hash_table_unref (self->text_widths);
    g_hash_table_unref (self->mapped_cells);
    g_free (self);
}

void
nautilus_column_width_reset (NautilusColumnWidth *self)
{
    self->widest_text = 0;
    self->width = 0;
    g_hash_table_remove_all (self->text_widths);
}

G_DEFINE_FINAL_TYPE (NautilusLabelCell, nautilus_label_cell, NAUTILUS_TYPE_VIEW_CELL)

enum
{
    PROP_0,
    PROP_COLUMN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static gboolean
resize_column (GtkWidget     *widget,
               GdkFrameClock *frame_clock,
               gpointer       user_data)
{
    NautilusLabelCell *self = NAUTILUS_LABEL_CELL (widget);

    self->resize_tick_id = 0;

    if (self->column_width->widest_text > self->column_width->width)
    {
        self->column_width->width = self->column_width->widest_text;
        gtk_widget_queue_resize (widget);
    }

    return G_SOURCE_REMOVE;
}

static gboolean
shrink_to_mapped_cells (gpointer user_data)
{
    NautilusColumnWidth *self = user_data;
    gint64 idle_ms = (g_get_monotonic_time () - self->last_measure_time) / 1000;
    GHashTableIter iter;
    gpointer cell;
    NautilusLabelCell *any_cell = NULL;
    int needed = 0;

    if (idle_ms < SHRINK_DELAY_MS)
    {
        self->shrink_timeout_id = g_timeout_add (SHRINK_DELAY_MS - idle_ms,
                                                 shrink_to_mapped_cells, self);
        return G_SOURCE_REMOVE;
    }

    self->shrink_timeout_id = 0;

    g_hash_table_iter_init (&iter, self->mapped_cells);
    while (g_hash_table_iter_next (&iter, &cell, NULL))
    {
        any_cell = cell;
        needed = MAX (needed, any_cell->text_width);
    }

    if (any_cell != NULL && needed < self->width)
    {
        self->widest_text = needed;
        self->width = needed;
        gtk_widget_queue_resize (GTK_WIDGET (any_cell));
    }

    return G_SOURCE_REMOVE;
}

static void
measure_text (NautilusLabelCell *self)
{
    const char *text = gtk_inscription_get_text (self->inscription);
    gpointer cached_width;
    int width;

    self->needs_measure = FALSE;
    self->text_width = 0;

    self->column_width->last_measure_time = g_get_monotonic_time ();
    if (self->column_width->shrink_timeout_id == 0)
    {
        self->column_width->shrink_timeout_id = g_timeout_add (SHRINK_DELAY_MS,
                                                               shrink_to_mapped_cells,
                                                               self->column_width);
    }

    if (text == NULL || *text == '\0')
    {
        return;
    }

    if (g_hash_table_lookup_extended (self->column_width->text_widths, text,
                                      NULL, &cached_width))
    {
        width = GPOINTER_TO_INT (cached_width);
    }
    else
    {
        if (self->measure_layout == NULL)
        {
            self->measure_layout = pango_layout_new (gtk_widget_get_pango_context (GTK_WIDGET (self->inscription)));
            pango_layout_set_attributes (self->measure_layout,
                                         gtk_inscription_get_attributes (self->inscription));
        }

        pango_layout_set_text (self->measure_layout, text, -1);
        pango_layout_get_pixel_size (self->measure_layout, &width, NULL);

        if (g_hash_table_size (self->column_width->text_widths) >= MAX_CACHED_WIDTHS)
        {
            g_hash_table_remove_all (self->column_width->text_widths);
        }
        g_hash_table_insert (self->column_width->text_widths,
                             g_strdup (text), GINT_TO_POINTER (width));
    }

    self->text_width = width;

    self->column_width->widest_text = MAX (self->column_width->widest_text, width);

    /* Bind and map can happen during layout, where a resize may be dropped.
     * If this cell goes away before the tick, the next measurement retries. */
    if (self->column_width->widest_text > self->column_width->width &&
        self->resize_tick_id == 0)
    {
        self->resize_tick_id = gtk_widget_add_tick_callback (GTK_WIDGET (self),
                                                             resize_column, NULL, NULL);
    }
}

static void
update_label (NautilusLabelCell *self)
{
    g_autoptr (NautilusViewItem) item = NULL;
    NautilusFile *file;
    g_autofree gchar *string = NULL;

    item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_return_if_fail (item != NULL);
    file = nautilus_view_item_get_file (item);

    string = nautilus_file_get_string_attribute_q (file, self->attribute_q);

    /* Unlike GtkLabel, doesn't queue a resize. */
    gtk_inscription_set_text (self->inscription, string);

    /* Cells of hidden columns are bound too, but not mapped. */
    self->needs_measure = TRUE;
    if (gtk_widget_get_mapped (GTK_WIDGET (self)))
    {
        measure_text (self);
    }
}

static void
on_file_changed (NautilusLabelCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_autofree gchar *string = NULL;

    /* Emitted for any attribute. Not checked on bind, where the column width
     * may have been reset. */
    string = nautilus_file_get_string_attribute_q (nautilus_view_item_get_file (item),
                                                   self->attribute_q);
    if (g_strcmp0 (string, gtk_inscription_get_text (self->inscription)) != 0)
    {
        update_label (self);
    }
}

static void
nautilus_label_cell_map (GtkWidget *widget)
{
    NautilusLabelCell *self = NAUTILUS_LABEL_CELL (widget);

    GTK_WIDGET_CLASS (nautilus_label_cell_parent_class)->map (widget);

    g_hash_table_add (self->column_width->mapped_cells, self);

    if (self->needs_measure)
    {
        measure_text (self);
    }
}

static void
nautilus_label_cell_unmap (GtkWidget *widget)
{
    NautilusLabelCell *self = NAUTILUS_LABEL_CELL (widget);

    g_hash_table_remove (self->column_width->mapped_cells, self);

    GTK_WIDGET_CLASS (nautilus_label_cell_parent_class)->unmap (widget);
}

static void
nautilus_label_cell_measure (GtkWidget      *widget,
                             GtkOrientation  orientation,
                             int             for_size,
                             int            *minimum,
                             int            *natural,
                             int            *minimum_baseline,
                             int            *natural_baseline)
{
    NautilusLabelCell *self = NAUTILUS_LABEL_CELL (widget);

    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        *minimum = *natural = self->column_width->width;
    }
    else
    {
        gtk_widget_measure (GTK_WIDGET (self->inscription), orientation, for_size,
                            minimum, natural, minimum_baseline, natural_baseline);
    }
}

static void
nautilus_label_cell_size_allocate (GtkWidget *widget,
                                   int        width,
                                   int        height,
                                   int        baseline)
{
    NautilusLabelCell *self = NAUTILUS_LABEL_CELL (widget);

    gtk_widget_allocate (GTK_WIDGET (self->inscription), width, height, baseline, NULL);
}

static void
nautilus_label_cell_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    NautilusLabelCell *self = NAUTILUS_LABEL_CELL (object);

    switch (prop_id)
    {
        case PROP_COLUMN:
        {
            self->column = g_value_get_object (value);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_label_cell_init (NautilusLabelCell *self)
{
    GtkWidget *child;

    child = gtk_inscription_new (NULL);
    gtk_widget_set_parent (child, GTK_WIDGET (self));
    gtk_widget_add_css_class (child, "dimmed");
    gtk_inscription_set_min_chars (GTK_INSCRIPTION (child), 0);
    gtk_inscription_set_text_overflow (GTK_INSCRIPTION (child), GTK_INSCRIPTION_OVERFLOW_ELLIPSIZE_END);
    self->inscription = GTK_INSCRIPTION (child);

    /* Connect automatically to an item. */
    self->item_signal_group = g_signal_group_new (NAUTILUS_TYPE_VIEW_ITEM);
    g_signal_group_connect_swapped (self->item_signal_group, "file-changed",
                                    G_CALLBACK (on_file_changed), self);
    g_signal_connect_object (self->item_signal_group, "bind",
                             G_CALLBACK (update_label), self,
                             G_CONNECT_SWAPPED);
    g_object_bind_property (self, "item",
                            self->item_signal_group, "target",
                            G_BINDING_SYNC_CREATE);
}

static void
nautilus_label_cell_constructed (GObject *object)
{
    NautilusLabelCell *self = NAUTILUS_LABEL_CELL (object);
    g_autofree gchar *column_name = NULL;
    gfloat xalign;

    G_OBJECT_CLASS (nautilus_label_cell_parent_class)->constructed (object);

    g_object_get (self->column,
                  "attribute_q", &self->attribute_q,
                  "name", &column_name,
                  "xalign", &xalign,
                  NULL);
    gtk_inscription_set_xalign (self->inscription, xalign);

    if (nautilus_file_is_date_sort_attribute_q (self->attribute_q))
    {
        g_signal_connect_object (nautilus_preferences, "changed::" NAUTILUS_PREFERENCES_DATE_TIME_FORMAT,
                                 G_CALLBACK (update_label), self,
                                 G_CONNECT_SWAPPED);
    }

    if (g_strcmp0 (column_name, "permissions") == 0)
    {
        gtk_widget_add_css_class (GTK_WIDGET (self->inscription), "monospace");
    }
    else
    {
        /* Instead of the "numeric" style class: CSS font features don't
         * reach the measurement layout. */
        g_autoptr (PangoAttrList) attrs = pango_attr_list_new ();

        pango_attr_list_insert (attrs, pango_attr_font_features_new ("tnum=1"));
        gtk_inscription_set_attributes (self->inscription, attrs);
    }
}

static void
nautilus_label_cell_dispose (GObject *object)
{
    NautilusLabelCell *self = (NautilusLabelCell *) object;

    g_clear_object (&self->item_signal_group);
    g_clear_object (&self->measure_layout);
    if (self->inscription)
    {
        gtk_widget_unparent (GTK_WIDGET (self->inscription));
        self->inscription = NULL;
    }
    G_OBJECT_CLASS (nautilus_label_cell_parent_class)->dispose (object);
}

static void
nautilus_label_cell_class_init (NautilusLabelCellClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->set_property = nautilus_label_cell_set_property;
    object_class->constructed = nautilus_label_cell_constructed;
    object_class->dispose = nautilus_label_cell_dispose;

    widget_class->map = nautilus_label_cell_map;
    widget_class->unmap = nautilus_label_cell_unmap;
    widget_class->measure = nautilus_label_cell_measure;
    widget_class->size_allocate = nautilus_label_cell_size_allocate;

    properties[PROP_COLUMN] = g_param_spec_object ("column",
                                                   "", "",
                                                   NAUTILUS_TYPE_COLUMN,
                                                   G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

NautilusViewCell *
nautilus_label_cell_new (NautilusListBase    *view,
                         NautilusColumn      *column,
                         NautilusColumnWidth *column_width)
{
    NautilusLabelCell *self = g_object_new (NAUTILUS_TYPE_LABEL_CELL,
                                            "view", view,
                                            "column", column,
                                            NULL);

    self->column_width = column_width;

    return NAUTILUS_VIEW_CELL (self);
}
