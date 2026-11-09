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
    gboolean needs_measure;
    guint resize_tick_id;
};

/* Shared by all cells of a column in a view. */
struct _NautilusColumnWidth
{
    /* Only grows, so it is the widest text seen so far. */
    int widest_text;
    /* What cells report as their width: widest_text, once the column has been
     * told to resize for it. Reporting widest_text right away would make cells
     * wider than the column's cached width, overflowing into the next one. */
    int width;
    /* Text → width in pixels. Columns repeat a lot of values (types, owners,
     * dates), so this saves laying out the same text again on every bind. */
    GHashTable *text_widths;
};

#define MAX_CACHED_WIDTHS 10000

NautilusColumnWidth *
nautilus_column_width_new (void)
{
    NautilusColumnWidth *self = g_new0 (NautilusColumnWidth, 1);

    self->text_widths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    return self;
}

void
nautilus_column_width_free (NautilusColumnWidth *self)
{
    g_hash_table_unref (self->text_widths);
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
        /* Makes the column measure all its cells again. */
        gtk_widget_queue_resize (widget);
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

        /* Columns of unique values (exact sizes, times) would grow it forever. */
        if (g_hash_table_size (self->column_width->text_widths) >= MAX_CACHED_WIDTHS)
        {
            g_hash_table_remove_all (self->column_width->text_widths);
        }
        g_hash_table_insert (self->column_width->text_widths,
                             g_strdup (text), GINT_TO_POINTER (width));
    }

    /* Only grow, so a resize is queued only when a new widest text shows up. */
    self->column_width->widest_text = MAX (self->column_width->widest_text, width);

    /* This runs on bind and map, which can happen in the middle of layout,
     * where a resize may be dropped. Resize at the start of the next frame
     * instead. Checked on every measurement, so if this cell goes away before
     * then, the next cell to be measured schedules it again. */
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

    /* Unlike GtkLabel, this doesn't queue a resize: rebinding while scrolling
     * doesn't relayout the column. */
    gtk_inscription_set_text (self->inscription, string);

    /* Cells of hidden columns are bound too, but not mapped; don't spend time
     * measuring them until they are shown. */
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

    /* "file-changed" is emitted for any attribute (e.g. thumbnails), so avoid
     * measuring again when this column's text stays the same. Not done on bind,
     * where the column width may have been reset. */
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

    if (self->needs_measure)
    {
        measure_text (self);
    }
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
    /* Width comes from our own measurement, not from character counts. */
    gtk_inscription_set_min_chars (GTK_INSCRIPTION (child), 0);
    /* Only reached if the user shrinks the column. */
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
        /* Font family is part of the pango context, so measurement sees it. */
        gtk_widget_add_css_class (GTK_WIDGET (self->inscription), "monospace");
    }
    else
    {
        /* Same as the "numeric" style class, but as an attribute instead of
         * CSS so that the measurement layout can share it. */
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
    widget_class->measure = nautilus_label_cell_measure;
    widget_class->size_allocate = nautilus_label_cell_size_allocate;

    properties[PROP_COLUMN] = g_param_spec_object ("column",
                                                   "", "",
                                                   NAUTILUS_TYPE_COLUMN,
                                                   G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

/**
 * nautilus_label_cell_new:
 * @column_width: shared by all cells of the column. Owned by the view, which
 *   outlives its cells, and reset when it loads a new directory.
 */
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
