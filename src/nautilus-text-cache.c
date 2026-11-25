/*
 * SPDX-FileCopyrightText: 2026 The Files contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-text-cache.h"

/*
 * NautilusTextCache:
 *
 * A least-recently-used cache of laid out text, shared by the cells of a view
 * column, so that rebinding a row to text that was shown before (e.g. when
 * scrolling back, or for repeated strings like sizes and dates) doesn't need
 * Pango to itemize and shape it again.
 *
 * Cached layouts are never modified after being created. Callers get a new
 * reference, so evicted layouts stay valid for as long as a cell uses them.
 * All users of a cache must use the same font; the cache is cleared when it is
 * used with a different one.
 */

typedef struct
{
    char *text;
    GList *link;

    PangoLayout *layout; /* Unlimited width */
    int width;
    int height;

    /* The most recent ellipsized variant: column widths rarely change. */
    PangoLayout *ellipsized;
    int ellipsized_width;
    PangoEllipsizeMode ellipsize_mode;
} Entry;

struct _NautilusTextCache
{
    GHashTable *entries; /* text => Entry */
    GQueue lru;          /* Entry, most recently used first */
    guint max_entries;

    PangoFontDescription *font;
};

static void
entry_free (Entry *entry)
{
    g_free (entry->text);
    g_clear_object (&entry->layout);
    g_clear_object (&entry->ellipsized);
    g_free (entry);
}

NautilusTextCache *
nautilus_text_cache_new (guint max_entries)
{
    NautilusTextCache *self = g_new0 (NautilusTextCache, 1);

    self->entries = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           NULL, (GDestroyNotify) entry_free);
    g_queue_init (&self->lru);
    self->max_entries = max_entries;

    return self;
}

void
nautilus_text_cache_clear (NautilusTextCache *self)
{
    g_queue_clear (&self->lru);
    g_hash_table_remove_all (self->entries);
    g_clear_pointer (&self->font, pango_font_description_free);
}

void
nautilus_text_cache_free (NautilusTextCache *self)
{
    nautilus_text_cache_clear (self);
    g_hash_table_destroy (self->entries);
    g_free (self);
}

static void
check_font (NautilusTextCache *self,
            PangoContext      *context)
{
    const PangoFontDescription *font = pango_context_get_font_description (context);

    if (self->font != NULL && pango_font_description_equal (self->font, font))
    {
        return;
    }

    nautilus_text_cache_clear (self);
    self->font = pango_font_description_copy (font);
}

static Entry *
lookup (NautilusTextCache *self,
        GtkWidget         *widget,
        const char        *text,
        PangoAttrList     *attrs)
{
    PangoContext *context = gtk_widget_get_pango_context (widget);
    Entry *entry;

    check_font (self, context);

    entry = g_hash_table_lookup (self->entries, text);
    if (entry != NULL)
    {
        /* Move to front */
        g_queue_unlink (&self->lru, entry->link);
        g_queue_push_head_link (&self->lru, entry->link);

        return entry;
    }

    entry = g_new0 (Entry, 1);
    entry->text = g_strdup (text);
    entry->layout = pango_layout_new (context);
    pango_layout_set_single_paragraph_mode (entry->layout, TRUE);
    pango_layout_set_attributes (entry->layout, attrs);
    pango_layout_set_text (entry->layout, text, -1);
    pango_layout_get_pixel_size (entry->layout, &entry->width, &entry->height);

    g_queue_push_head (&self->lru, entry);
    entry->link = self->lru.head;
    g_hash_table_insert (self->entries, entry->text, entry);

    if (self->lru.length > self->max_entries)
    {
        Entry *oldest = g_queue_pop_tail (&self->lru);

        g_hash_table_remove (self->entries, oldest->text);
    }

    return entry;
}

/**
 * nautilus_text_cache_get:
 *
 * Returns: (transfer full): a layout of @text at unlimited width. Must not be
 *   modified.
 */
PangoLayout *
nautilus_text_cache_get (NautilusTextCache *self,
                         GtkWidget         *widget,
                         const char        *text,
                         PangoAttrList     *attrs,
                         int               *width,
                         int               *height)
{
    Entry *entry = lookup (self, widget, text, attrs);

    *width = entry->width;
    *height = entry->height;

    return g_object_ref (entry->layout);
}

/**
 * nautilus_text_cache_get_ellipsized:
 *
 * Returns: (transfer full): a layout of @text ellipsized to @width pixels.
 *   Must not be modified.
 */
PangoLayout *
nautilus_text_cache_get_ellipsized (NautilusTextCache  *self,
                                    GtkWidget          *widget,
                                    const char         *text,
                                    PangoAttrList      *attrs,
                                    PangoEllipsizeMode  ellipsize,
                                    int                 width)
{
    Entry *entry = lookup (self, widget, text, attrs);

    if (entry->ellipsized == NULL ||
        entry->ellipsized_width != width ||
        entry->ellipsize_mode != ellipsize)
    {
        g_clear_object (&entry->ellipsized);
        entry->ellipsized = pango_layout_copy (entry->layout);
        pango_layout_set_ellipsize (entry->ellipsized, ellipsize);
        pango_layout_set_width (entry->ellipsized, width * PANGO_SCALE);
        entry->ellipsized_width = width;
        entry->ellipsize_mode = ellipsize;
    }

    return g_object_ref (entry->ellipsized);
}
