/* syscap-window CAPTURE PID BEGIN_USEC END_USEC [TOP]
 *
 * Aggregates the samples of PID between two g_get_monotonic_time () stamps,
 * such as the ones load-timing.patch logs, and prints the TOP functions by
 * self and inclusive samples. Environment variables:
 *
 *   TID=N       only thread N; TID=-1 for all threads but the main one
 *   UNDER=func  only count frames called from func
 *   FOCUS=func  print the callers of func instead
 *   HIST=ms     print a timeline of main thread and worker samples per bucket,
 *               tagging main thread samples with the first of FUNCS=a|b|c
 *               found in their stack
 *
 * Build: gcc -O2 -o syscap-window syscap-window.c $(pkg-config --cflags --libs sysprof-6)
 */
#include <sysprof.h>
#include <stdlib.h>

static int
cmp_count (gconstpointer a, gconstpointer b, gpointer table)
{
  guint ca = GPOINTER_TO_UINT (g_hash_table_lookup (table, *(char **)a));
  guint cb = GPOINTER_TO_UINT (g_hash_table_lookup (table, *(char **)b));
  return (ca < cb) - (ca > cb);
}

static void
print_top (const char *title, GHashTable *table, guint total, guint top)
{
  g_autoptr(GPtrArray) keys = g_hash_table_get_keys_as_ptr_array (table);
  g_ptr_array_sort_with_data (keys, cmp_count, table);
  g_print ("\n-- %s --\n", title);
  for (guint i = 0; i < keys->len && i < top; i++)
    {
      guint c = GPOINTER_TO_UINT (g_hash_table_lookup (table, keys->pdata[i]));
      g_print ("%6u %5.1f%%  %s\n", c, 100.0 * c / total, (char *)keys->pdata[i]);
    }
}

int
main (int argc, char *argv[])
{
  g_autoptr(SysprofDocumentLoader) loader = NULL;
  g_autoptr(SysprofDocument) document = NULL;
  g_autoptr(GListModel) samples = NULL;
  g_autoptr(GHashTable) self_tab = g_hash_table_new (g_str_hash, g_str_equal);
  g_autoptr(GHashTable) incl_tab = g_hash_table_new (g_str_hash, g_str_equal);
  g_autoptr(GError) error = NULL;
  g_autoptr(GHashTable) threads = g_hash_table_new (NULL, NULL);
  int tid_filter = g_getenv ("TID") ? atoi (g_getenv ("TID")) : 0;
  gint64 begin, end;
  guint total = 0, top;
  int pid;

  if (argc < 5)
    return 1;
  pid = atoi (argv[2]);
  begin = g_ascii_strtoll (argv[3], NULL, 10) * 1000;
  end = g_ascii_strtoll (argv[4], NULL, 10) * 1000;
  top = argc > 5 ? atoi (argv[5]) : 40;

  loader = sysprof_document_loader_new (argv[1]);
  if (!(document = sysprof_document_loader_load (loader, NULL, &error)))
    g_error ("%s", error->message);

  samples = sysprof_document_list_samples (document);
  if (g_getenv ("HIST"))
    {
      /* Samples per bucket: main thread, other threads, and the busiest
       * symbol of the main thread in that bucket (matching FUNCS, a | list) */
      gint64 bucket = g_ascii_strtoll (g_getenv ("HIST"), NULL, 10) * 1000000;
      guint n_buckets = (end - begin) / bucket + 1;
      g_autofree guint *main_c = g_new0 (guint, n_buckets);
      g_autofree guint *other_c = g_new0 (guint, n_buckets);
      g_autoptr(GPtrArray) tags = g_ptr_array_new_with_free_func ((GDestroyNotify) g_hash_table_unref);
      g_auto(GStrv) funcs = g_strsplit (g_getenv ("FUNCS") ? g_getenv ("FUNCS") : "", "|", 0);
      for (guint b = 0; b < n_buckets; b++)
        g_ptr_array_add (tags, g_hash_table_new (g_str_hash, g_str_equal));
      for (guint i = 0; i < g_list_model_get_n_items (samples); i++)
        {
          g_autoptr(SysprofDocumentTraceable) t = g_list_model_get_item (samples, i);
          gint64 time = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (t));
          if (sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (t)) != pid || time < begin || time > end)
            continue;
          guint b = (time - begin) / bucket;
          if (sysprof_document_traceable_get_thread_id (t) == pid)
            {
              SysprofSymbol *symbols[128];
              guint n = sysprof_document_symbolize_traceable (document, t, symbols, G_N_ELEMENTS (symbols), NULL);
              const char *tag = "other";
              main_c[b]++;
              for (guint j = n; j > 0 && funcs[0]; j--)
                for (guint k = 0; funcs[k]; k++)
                  if (g_strcmp0 (sysprof_symbol_get_name (symbols[j - 1]), funcs[k]) == 0)
                    { tag = funcs[k]; goto found; }
            found:
              g_hash_table_insert (tags->pdata[b], (char *) g_intern_string (tag),
                                   GUINT_TO_POINTER (GPOINTER_TO_UINT (g_hash_table_lookup (tags->pdata[b], g_intern_string (tag))) + 1));
            }
          else
            other_c[b]++;
        }
      for (guint b = 0; b < n_buckets; b++)
        {
          GHashTableIter it; gpointer k, v;
          g_print ("%5u ms  main %3u  workers %3u  ", (guint) (b * bucket / 1000000), main_c[b], other_c[b]);
          g_hash_table_iter_init (&it, tags->pdata[b]);
          while (g_hash_table_iter_next (&it, &k, &v))
            g_print (" %s=%u", (char *) k, GPOINTER_TO_UINT (v));
          g_print ("\n");
        }
      return 0;
    }
  for (guint i = 0; i < g_list_model_get_n_items (samples); i++)
    {
      g_autoptr(SysprofDocumentTraceable) t = g_list_model_get_item (samples, i);
      SysprofSymbol *symbols[128];
      g_autoptr(GHashTable) seen = g_hash_table_new (g_str_hash, g_str_equal);
      gint64 time = sysprof_document_frame_get_time (SYSPROF_DOCUMENT_FRAME (t));
      guint n;

      if (sysprof_document_frame_get_pid (SYSPROF_DOCUMENT_FRAME (t)) != pid ||
          time < begin || time > end)
        continue;

      {
        int tid = sysprof_document_traceable_get_thread_id (t);
        g_hash_table_insert (threads, GINT_TO_POINTER (tid),
                             GUINT_TO_POINTER (GPOINTER_TO_UINT (g_hash_table_lookup (threads, GINT_TO_POINTER (tid))) + 1));
        if ((tid_filter > 0 && tid != tid_filter) || (tid_filter < 0 && tid == pid))
          continue;
      }
      total++;
      n = sysprof_document_symbolize_traceable (document, t, symbols, G_N_ELEMENTS (symbols), NULL);
      if (g_getenv ("UNDER"))
        {
          /* Only frames called from UNDER */
          guint at = n;
          for (guint j = 0; j < n; j++)
            if (g_strcmp0 (sysprof_symbol_get_name (symbols[j]), g_getenv ("UNDER")) == 0)
              at = j;
          if (at == n)
            continue;
          n = at;
        }
      if (g_getenv ("FOCUS"))
        {
          /* Print the caller chain (up to 12 frames above FOCUS) as one key */
          int at = -1;
          for (guint j = 0; j < n; j++)
            if (g_strcmp0 (sysprof_symbol_get_name (symbols[j]), g_getenv ("FOCUS")) == 0)
              { at = j; break; }
          if (at < 0)
            continue;
          GString *chain = g_string_new (NULL);
          for (guint j = at + 1; j < n && j < (guint) at + 13; j++)
            g_string_append_printf (chain, "%s%s", j > (guint) at + 1 ? " < " : "", sysprof_symbol_get_name (symbols[j]));
          const char *key = g_intern_string (chain->str);
          g_string_free (chain, TRUE);
          g_hash_table_insert (incl_tab, (char *)key,
                               GUINT_TO_POINTER (GPOINTER_TO_UINT (g_hash_table_lookup (incl_tab, key)) + 1));
          total++;
          continue;
        }
      for (guint j = 0; j < n; j++)
        {
          const char *name = g_intern_string (sysprof_symbol_get_name (symbols[j]));

          if (g_str_has_prefix (name, "- - "))
            continue;
          if (j == 0 || (j == 1 && g_str_has_prefix (sysprof_symbol_get_name (symbols[0]), "- - ")))
            g_hash_table_insert (self_tab, (char *)name,
                                 GUINT_TO_POINTER (GPOINTER_TO_UINT (g_hash_table_lookup (self_tab, name)) + 1));
          if (g_hash_table_add (seen, (char *)name))
            g_hash_table_insert (incl_tab, (char *)name,
                                 GUINT_TO_POINTER (GPOINTER_TO_UINT (g_hash_table_lookup (incl_tab, name)) + 1));
        }
    }

  {
    GHashTableIter it; gpointer k, v;
    g_hash_table_iter_init (&it, threads);
    while (g_hash_table_iter_next (&it, &k, &v))
      g_print ("thread %d: %u samples%s\n", GPOINTER_TO_INT (k), GPOINTER_TO_UINT (v),
               GPOINTER_TO_INT (k) == pid ? " (main)" : "");
  }
  g_print ("samples in window: %u (%.1f ms window)\n", total, (end - begin) / 1e6);
  if (total == 0)
    return 0;
  print_top ("self", self_tab, total, top);
  print_top ("inclusive", incl_tab, total, top);
  return 0;
}
