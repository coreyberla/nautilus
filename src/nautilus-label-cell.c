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

    /* -1 until measured. */
    int text_width;
    int width;
};

G_DEFINE_FINAL_TYPE (NautilusLabelCell, nautilus_label_cell, NAUTILUS_TYPE_VIEW_CELL)

enum
{
    PROP_0,
    PROP_COLUMN,
    N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

enum
{
    TEXT_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static int
measure_text (NautilusLabelCell *self,
              const char        *text)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    guint text_hash;
    int width;

    if (text == NULL || *text == '\0')
    {
        return 0;
    }

    /* Scrolling back to a row doesn't measure it again. */
    text_hash = g_str_hash (text);
    if (item != NULL &&
        nautilus_view_item_get_label_width (item, self->attribute_q, text_hash, &width))
    {
        return width;
    }

    if (self->measure_layout == NULL)
    {
        self->measure_layout = pango_layout_new (gtk_widget_get_pango_context (GTK_WIDGET (self->inscription)));
        pango_layout_set_attributes (self->measure_layout,
                                     gtk_inscription_get_attributes (self->inscription));
    }

    pango_layout_set_text (self->measure_layout, text, -1);
    pango_layout_get_pixel_size (self->measure_layout, &width, NULL);

    if (item != NULL)
    {
        nautilus_view_item_set_label_width (item, self->attribute_q, text_hash, width);
    }

    return width;
}

/* Measured on demand, so cells of hidden columns are never measured. */
int
nautilus_label_cell_get_text_width (NautilusLabelCell *self)
{
    if (self->text_width < 0)
    {
        self->text_width = measure_text (self, gtk_inscription_get_text (self->inscription));
    }

    return self->text_width;
}

void
nautilus_label_cell_set_width (NautilusLabelCell *self,
                               int                width)
{
    if (width != self->width)
    {
        self->width = width;
        gtk_widget_queue_resize (GTK_WIDGET (self));
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
    self->text_width = -1;
    g_signal_emit (self, signals[TEXT_CHANGED], 0);
}

static void
on_file_changed (NautilusLabelCell *self)
{
    g_autoptr (NautilusViewItem) item = nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (self));
    g_autofree gchar *string = NULL;

    /* Emitted for any attribute. */
    string = nautilus_file_get_string_attribute_q (nautilus_view_item_get_file (item),
                                                   self->attribute_q);
    if (g_strcmp0 (string, gtk_inscription_get_text (self->inscription)) != 0)
    {
        update_label (self);
    }
}

static void
on_unbind (NautilusLabelCell *self)
{
    self->text_width = 0;
    g_signal_emit (self, signals[TEXT_CHANGED], 0);
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
        *minimum = *natural = self->width;
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

    self->text_width = -1;

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
    g_signal_connect_object (self->item_signal_group, "unbind",
                             G_CALLBACK (on_unbind), self,
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

    widget_class->measure = nautilus_label_cell_measure;
    widget_class->size_allocate = nautilus_label_cell_size_allocate;

    properties[PROP_COLUMN] = g_param_spec_object ("column",
                                                   "", "",
                                                   NAUTILUS_TYPE_COLUMN,
                                                   G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);

    signals[TEXT_CHANGED] = g_signal_new ("text-changed",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0, NULL, NULL, NULL,
                                          G_TYPE_NONE, 0);
}

NautilusViewCell *
nautilus_label_cell_new (NautilusListBase *view,
                         NautilusColumn   *column)
{
    return NAUTILUS_VIEW_CELL (g_object_new (NAUTILUS_TYPE_LABEL_CELL,
                                             "view", view,
                                             "column", column,
                                             NULL));
}
