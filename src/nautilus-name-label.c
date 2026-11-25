/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-name-label.h"

/**
 * NautilusNameLabel:
 *
 * A single-line text widget for cells in views.
 *
 * Views rebind many rows per frame while scrolling, so this is optimized for
 * changing text: the text is laid out once and that layout is used both for
 * measuring and for drawing (GtkLabel lays an ellipsizing label out several
 * times per text change). Its height comes from font metrics, so measuring
 * height doesn't need a layout, but unlike GtkInscription it doesn't clip, so
 * glyphs taller than the primary font (e.g. in fallback fonts for other
 * scripts) are not cut off.
 *
 * If given a #NautilusTextCache, layouts are shared with the other users of
 * the cache, so text that was laid out before doesn't need to be again.
 */
struct _NautilusNameLabel
{
    GtkWidget parent_instance;

    char *text;
    PangoEllipsizeMode ellipsize;
    float xalign;
    gboolean tabular_numbers;
    PangoAttrList *attrs;
    NautilusTextCache *cache; /* unowned */

    PangoLayout *layout;    /* At unlimited width */
    PangoLayout *ellipsized; /* NULL if the text fits */

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
invalidate_layouts (NautilusNameLabel *self)
{
    g_clear_object (&self->layout);
    g_clear_object (&self->ellipsized);
    self->natural_width = -1;
}

static void
ensure_metrics (NautilusNameLabel *self)
{
    if (self->line_height >= 0)
    {
        return;
    }

    PangoContext *context = gtk_widget_get_pango_context (GTK_WIDGET (self));
    g_autoptr (PangoLayout) ellipsis = pango_layout_new (context);

    /* Use the height of a line in the primary font, as GtkLabel would have
     * for text in that font, but measured once instead of for every text. */
    pango_layout_set_text (ellipsis, "…", -1);
    pango_layout_get_pixel_size (ellipsis, &self->min_width, &self->line_height);
    self->baseline = PANGO_PIXELS (pango_layout_get_baseline (ellipsis));
}

static PangoAttrList *
get_attrs (NautilusNameLabel *self)
{
    if (self->attrs == NULL)
    {
        self->attrs = pango_attr_list_new ();
        pango_attr_list_insert (self->attrs, pango_attr_insert_hyphens_new (FALSE));
        if (self->tabular_numbers)
        {
            pango_attr_list_insert (self->attrs, pango_attr_font_features_new ("tnum=1"));
        }
    }

    return self->attrs;
}

static void
ensure_layout (NautilusNameLabel *self)
{
    const char *text = self->text != NULL ? self->text : "";

    if (self->layout != NULL)
    {
        return;
    }

    if (self->cache != NULL)
    {
        int height;

        self->layout = nautilus_text_cache_get (self->cache, GTK_WIDGET (self), text,
                                                get_attrs (self),
                                                &self->natural_width, &height);
    }
    else
    {
        self->layout = gtk_widget_create_pango_layout (GTK_WIDGET (self), text);
        pango_layout_set_single_paragraph_mode (self->layout, TRUE);
        pango_layout_set_attributes (self->layout, get_attrs (self));
        pango_layout_get_pixel_size (self->layout, &self->natural_width, NULL);
    }
}

static void
context_changed (NautilusNameLabel *self)
{
    invalidate_metrics (self);
    invalidate_layouts (self);
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
        ensure_layout (self);

        if (self->ellipsize == PANGO_ELLIPSIZE_NONE)
        {
            *minimum = self->natural_width;
        }
        else
        {
            *minimum = MIN (self->min_width, self->natural_width);
        }
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

    ensure_layout (self);

    if (self->natural_width <= width || self->ellipsize == PANGO_ELLIPSIZE_NONE)
    {
        g_clear_object (&self->ellipsized);
        return;
    }

    if (self->ellipsized != NULL &&
        pango_layout_get_width (self->ellipsized) == width * PANGO_SCALE)
    {
        return;
    }

    g_clear_object (&self->ellipsized);
    if (self->cache != NULL)
    {
        self->ellipsized = nautilus_text_cache_get_ellipsized (self->cache, widget,
                                                               self->text != NULL ? self->text : "",
                                                               get_attrs (self),
                                                               self->ellipsize, width);
    }
    else
    {
        self->ellipsized = pango_layout_copy (self->layout);
        pango_layout_set_ellipsize (self->ellipsized, self->ellipsize);
        pango_layout_set_width (self->ellipsized, width * PANGO_SCALE);
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

    ensure_layout (self);
    layout = self->ellipsized != NULL ? self->ellipsized : self->layout;
    pango_layout_get_pixel_extents (layout, NULL, &logical);

    if (self->ellipsized == NULL)
    {
        float xalign = self->xalign;

        if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
        {
            xalign = 1.0 - xalign;
        }
        x = floor (xalign * (width - logical.width));
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
    NautilusNameLabel *self = NAUTILUS_NAME_LABEL (widget);

    GTK_WIDGET_CLASS (nautilus_name_label_parent_class)->system_setting_changed (widget, setting);

    if (self->cache != NULL)
    {
        nautilus_text_cache_clear (self->cache);
    }
    context_changed (self);
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
    invalidate_layouts (self);
    g_clear_pointer (&self->attrs, pango_attr_list_unref);

    G_OBJECT_CLASS (nautilus_name_label_parent_class)->finalize (object);
}

static void
nautilus_name_label_init (NautilusNameLabel *self)
{
    self->ellipsize = PANGO_ELLIPSIZE_MIDDLE;
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

GtkWidget *
nautilus_name_label_new (void)
{
    return g_object_new (NAUTILUS_TYPE_NAME_LABEL, NULL);
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
    invalidate_layouts (self);

    gtk_accessible_update_property (GTK_ACCESSIBLE (self),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, text,
                                    -1);
    gtk_widget_queue_resize (GTK_WIDGET (self));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TEXT]);
}

void
nautilus_name_label_set_ellipsize (NautilusNameLabel  *self,
                                   PangoEllipsizeMode  ellipsize)
{
    self->ellipsize = ellipsize;
    g_clear_object (&self->ellipsized);
    gtk_widget_queue_resize (GTK_WIDGET (self));
}

void
nautilus_name_label_set_xalign (NautilusNameLabel *self,
                                float              xalign)
{
    self->xalign = xalign;
    gtk_widget_queue_draw (GTK_WIDGET (self));
}

void
nautilus_name_label_set_tabular_numbers (NautilusNameLabel *self,
                                         gboolean           tabular_numbers)
{
    self->tabular_numbers = tabular_numbers;
    g_clear_pointer (&self->attrs, pango_attr_list_unref);
    invalidate_layouts (self);
    gtk_widget_queue_resize (GTK_WIDGET (self));
}

/**
 * nautilus_name_label_set_cache:
 * @cache: (nullable) (transfer none): a cache shared with other labels using
 *   the same font, which must outlive @self
 */
void
nautilus_name_label_set_cache (NautilusNameLabel *self,
                               NautilusTextCache *cache)
{
    self->cache = cache;
    invalidate_layouts (self);
    gtk_widget_queue_resize (GTK_WIDGET (self));
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

    return self->ellipsized != NULL && pango_layout_is_ellipsized (self->ellipsized);
}
