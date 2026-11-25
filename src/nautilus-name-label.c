/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-name-label.h"

/**
 * NautilusNameLabel:
 *
 * A single-line, middle-ellipsizing text widget for file names in views.
 *
 * Views rebind many rows per frame while scrolling, so this is optimized for
 * changing text: the text is laid out once and that layout is used both for
 * measuring and for drawing (GtkLabel lays an ellipsizing label out several
 * times per text change). Like GtkInscription, its height comes from font
 * metrics, so measuring height doesn't need a layout. Unlike GtkInscription,
 * it doesn't clip, so glyphs taller than the primary font (e.g. in fallback
 * fonts for other scripts) are not cut off, and its natural width is the
 * width of the text.
 */
struct _NautilusNameLabel
{
    GtkWidget parent_instance;

    char *text;
    PangoLayout *layout;

    /* Cached, in pixels; -1 if unknown. */
    int natural_width;
    int min_width;
    int line_height;
    int baseline;
};

G_DEFINE_FINAL_TYPE (NautilusNameLabel, nautilus_name_label, GTK_TYPE_WIDGET)

enum
{
    PROP_0,
    PROP_TEXT,
    N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
invalidate_metrics (NautilusNameLabel *self)
{
    self->min_width = -1;
    self->line_height = -1;
    self->baseline = -1;
}

static void
ensure_metrics (NautilusNameLabel *self)
{
    if (self->line_height >= 0)
    {
        return;
    }

    PangoContext *context = gtk_widget_get_pango_context (GTK_WIDGET (self));
    g_autoptr (PangoFontMetrics) metrics = pango_context_get_metrics (context, NULL, NULL);
    g_autoptr (PangoLayout) ellipsis = pango_layout_new (context);
    int ascent = pango_font_metrics_get_ascent (metrics);
    int descent = pango_font_metrics_get_descent (metrics);

    self->line_height = PANGO_PIXELS_CEIL (ascent + descent);
    self->baseline = PANGO_PIXELS_CEIL (ascent);

    pango_layout_set_text (ellipsis, "…", -1);
    pango_layout_get_pixel_size (ellipsis, &self->min_width, NULL);
}

static PangoLayout *
ensure_layout (NautilusNameLabel *self)
{
    if (self->layout == NULL)
    {
        g_autoptr (PangoAttrList) attrs = pango_attr_list_new ();

        self->layout = gtk_widget_create_pango_layout (GTK_WIDGET (self), self->text);
        pango_layout_set_ellipsize (self->layout, PANGO_ELLIPSIZE_MIDDLE);
        pango_layout_set_single_paragraph_mode (self->layout, TRUE);
        pango_attr_list_insert (attrs, pango_attr_insert_hyphens_new (FALSE));
        pango_layout_set_attributes (self->layout, attrs);
    }

    return self->layout;
}

static void
context_changed (NautilusNameLabel *self)
{
    invalidate_metrics (self);
    self->natural_width = -1;
    if (self->layout != NULL)
    {
        pango_layout_context_changed (self->layout);
    }
    gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
nautilus_name_label_measure (GtkWidget      *widget,
                             GtkOrientation  orientation,
                             int             for_size,
                             int            *minimum,
                             int            *natural,
                             int            *minimum_baseline,
                             int            *natural_baseline)
{
    NautilusNameLabel *self = NAUTILUS_NAME_LABEL (widget);

    ensure_metrics (self);

    if (orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        if (self->natural_width < 0)
        {
            PangoLayout *layout = ensure_layout (self);

            /* This layout is reused for drawing if the text fits. */
            pango_layout_set_width (layout, -1);
            pango_layout_get_pixel_size (layout, &self->natural_width, NULL);
        }

        *minimum = MIN (self->min_width, self->natural_width);
        *natural = self->natural_width;
    }
    else
    {
        *minimum = *natural = self->line_height;
        *minimum_baseline = *natural_baseline = self->baseline;
    }
}

static void
nautilus_name_label_size_allocate (GtkWidget *widget,
                                   int        width,
                                   int        height,
                                   int        baseline)
{
    NautilusNameLabel *self = NAUTILUS_NAME_LABEL (widget);
    PangoLayout *layout = ensure_layout (self);

    /* Only set a width (which lays the text out again) if it must be
     * ellipsized. Otherwise keep the layout from measuring. */
    if (self->natural_width >= 0 && self->natural_width <= width)
    {
        pango_layout_set_width (layout, -1);
    }
    else
    {
        pango_layout_set_width (layout, width * PANGO_SCALE);
    }
}

static void
nautilus_name_label_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
    NautilusNameLabel *self = NAUTILUS_NAME_LABEL (widget);
    PangoLayout *layout;
    PangoRectangle logical;
    GdkRGBA color;
    int width = gtk_widget_get_width (widget);
    int height = gtk_widget_get_height (widget);
    float x = 0, y;

    if (self->text == NULL || self->text[0] == '\0')
    {
        return;
    }

    layout = ensure_layout (self);
    pango_layout_get_pixel_extents (layout, NULL, &logical);

    if (pango_layout_get_width (layout) < 0 &&
        gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
    {
        x = width - logical.width;
    }
    x -= logical.x;

    /* Center the line. Lines in fonts taller than the primary font overflow
     * the allocation instead of being clipped. */
    y = floor ((height - logical.height) / 2.0) - logical.y;

    gtk_widget_get_color (widget, &color);

    gtk_snapshot_save (snapshot);
    gtk_snapshot_translate (snapshot, &GRAPHENE_POINT_INIT (x, y));
    gtk_snapshot_append_layout (snapshot, layout, &color);
    gtk_snapshot_restore (snapshot);
}

static void
nautilus_name_label_system_setting_changed (GtkWidget        *widget,
                                            GtkSystemSetting  setting)
{
    GTK_WIDGET_CLASS (nautilus_name_label_parent_class)->system_setting_changed (widget, setting);

    context_changed (NAUTILUS_NAME_LABEL (widget));
}

static void
nautilus_name_label_direction_changed (GtkWidget        *widget,
                                       GtkTextDirection  previous_direction)
{
    GTK_WIDGET_CLASS (nautilus_name_label_parent_class)->direction_changed (widget, previous_direction);

    context_changed (NAUTILUS_NAME_LABEL (widget));
}

static void
nautilus_name_label_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
    NautilusNameLabel *self = NAUTILUS_NAME_LABEL (object);

    switch (prop_id)
    {
        case PROP_TEXT:
        {
            g_value_set_string (value, self->text);
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_name_label_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
    NautilusNameLabel *self = NAUTILUS_NAME_LABEL (object);

    switch (prop_id)
    {
        case PROP_TEXT:
        {
            nautilus_name_label_set_text (self, g_value_get_string (value));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
nautilus_name_label_finalize (GObject *object)
{
    NautilusNameLabel *self = NAUTILUS_NAME_LABEL (object);

    g_free (self->text);
    g_clear_object (&self->layout);

    G_OBJECT_CLASS (nautilus_name_label_parent_class)->finalize (object);
}

static void
nautilus_name_label_init (NautilusNameLabel *self)
{
    self->natural_width = -1;
    invalidate_metrics (self);
}

static void
nautilus_name_label_class_init (NautilusNameLabelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->finalize = nautilus_name_label_finalize;
    object_class->get_property = nautilus_name_label_get_property;
    object_class->set_property = nautilus_name_label_set_property;

    widget_class->measure = nautilus_name_label_measure;
    widget_class->size_allocate = nautilus_name_label_size_allocate;
    widget_class->snapshot = nautilus_name_label_snapshot;
    widget_class->system_setting_changed = nautilus_name_label_system_setting_changed;
    widget_class->direction_changed = nautilus_name_label_direction_changed;

    properties[PROP_TEXT] = g_param_spec_string ("text", NULL, NULL,
                                                 NULL,
                                                 G_PARAM_READWRITE |
                                                 G_PARAM_EXPLICIT_NOTIFY |
                                                 G_PARAM_STATIC_STRINGS);
    g_object_class_install_properties (object_class, N_PROPS, properties);

    gtk_widget_class_set_css_name (widget_class, "label");
    gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_LABEL);
}

const char *
nautilus_name_label_get_text (NautilusNameLabel *self)
{
    g_return_val_if_fail (NAUTILUS_IS_NAME_LABEL (self), NULL);

    return self->text;
}

void
nautilus_name_label_set_text (NautilusNameLabel *self,
                              const char        *text)
{
    g_return_if_fail (NAUTILUS_IS_NAME_LABEL (self));

    if (g_strcmp0 (self->text, text) == 0)
    {
        return;
    }

    g_free (self->text);
    self->text = g_strdup (text);
    self->natural_width = -1;

    if (self->layout != NULL)
    {
        pango_layout_set_text (self->layout, text != NULL ? text : "", -1);
    }

    gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, text,
                                    -1);
    gtk_widget_queue_resize (GTK_WIDGET (self));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
}

/**
 * nautilus_name_label_is_ellipsized:
 *
 * Returns: whether the text doesn't fit and is shown ellipsized
 */
gboolean
nautilus_name_label_is_ellipsized (NautilusNameLabel *self)
{
    g_return_val_if_fail (NAUTILUS_IS_NAME_LABEL (self), FALSE);

    return self->layout != NULL && pango_layout_is_ellipsized (self->layout);
}
