/*
 * SPDX-FileCopyrightText: 2026 The Nautilus Authors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * A GtkMountOperation whose credentials prompt is built with libadwaita.
 *
 * Only ask-password is replaced, and only in the cases where GTK would show
 * its own in-process dialog (when a username or domain is needed). Prompts
 * that only need a password, questions and the "applications are using
 * this volume" dialog are left to GtkMountOperation, which hands them to the
 * shell's mount operation handler when one is available.
 *
 * Saving passwords is unchanged: the dialog only collects the answers and
 * passes the chosen GPasswordSave to GVfs, which talks to the keyring.
 *
 * Unlike GTK's dialog, this one stays open after Connect, showing a spinner
 * until the mount is done:
 *
 * - If the backend asks again, the previous answer was rejected. The same
 *   dialog switches back to asking, with an error, instead of a new dialog
 *   popping up.
 * - Nothing tells a mount operation that the mount finished, but whoever
 *   runs the mount (GVfs, a volume monitor) holds a reference to it until
 *   then, and every caller in Nautilus drops its own reference right after
 *   starting the mount. So the dialog is closed when the operation is
 *   disposed.
 *
 * The dialog is declared in nautilus-mount-operation.blp, with every row a
 * backend can ask for; each request shows the ones it needs.
 */

#include "nautilus-mount-operation.h"

#include <errno.h>
#include <string.h>
#include <glib/gi18n.h>
#include <adwaita.h>

struct _NautilusMountOperation
{
    GtkMountOperation parent_instance;

    AdwDialog *dialog;
    /* The window the dialog is shown on, if any. */
    GtkWidget *parent;
    GAskPasswordFlags flags;

    /* Connect was pressed and the answers were sent; waiting to hear back. */
    gboolean connecting;
    /* The previous answer was anonymous, for the retry message. */
    gboolean answered_anonymously;
    /* A reference on ourselves, held while waiting for the user. */
    gboolean holds_ref;
    /* See on_close_attempt(). */
    guint close_attempt_idle_id;

    /* Widgets of the dialog, owned by it. */
    GtkWidget *content;
    GtkWidget *error_revealer;
    GtkWidget *error_label;
    GtkWidget *anonymous_group;
    GtkWidget *anonymous_row;
    GtkWidget *credentials_group;
    GtkWidget *username_row;
    GtkWidget *domain_row;
    GtkWidget *hidden_volume_row;
    GtkWidget *system_volume_row;
    GtkWidget *pim_row;
    GtkWidget *password_row;
    GtkWidget *remember_group;
    GtkWidget *remember_row;
    GtkWidget *spinner_revealer;
};

G_DEFINE_FINAL_TYPE (NautilusMountOperation, nautilus_mount_operation, GTK_TYPE_MOUNT_OPERATION)

enum
{
    PROP_0,
    PROP_IS_SHOWING,
};

/* Order of the choices in the "Remember Password" combo row. */
static const GPasswordSave remember_choices[] =
{
    G_PASSWORD_SAVE_NEVER,
    G_PASSWORD_SAVE_FOR_SESSION,
    G_PASSWORD_SAVE_PERMANENTLY,
};

static gboolean
row_is_shown (GtkWidget *row)
{
    return gtk_widget_get_visible (row);
}

/* Rows that are not shown don't need input. */
static gboolean
row_has_input (GtkWidget *row)
{
    const char *text;

    if (!row_is_shown (row))
    {
        return TRUE;
    }

    text = gtk_editable_get_text (GTK_EDITABLE (row));

    return text != NULL && text[0] != '\0';
}

static gboolean
row_is_empty (GtkWidget *row)
{
    const char *text = gtk_editable_get_text (GTK_EDITABLE (row));

    return text == NULL || text[0] == '\0';
}

static gboolean
parse_pim (GtkWidget *row,
           guint     *pim_out)
{
    const char *text;
    char *end = NULL;
    guint64 pim;

    text = gtk_editable_get_text (GTK_EDITABLE (row));

    errno = 0;
    pim = g_ascii_strtoull (text, &end, 10);
    if (errno != 0 || pim > G_MAXUINT || end == text || *end != '\0')
    {
        return FALSE;
    }

    *pim_out = (guint) pim;
    return TRUE;
}

static gboolean
pim_row_is_valid (GtkWidget *row)
{
    guint pim;

    /* An empty PIM is fine: it means the default. */
    if (!row_is_shown (row) || row_is_empty (row))
    {
        return TRUE;
    }

    return parse_pim (row, &pim);
}

static gboolean
is_anonymous (NautilusMountOperation *self)
{
    return row_is_shown (self->anonymous_group) &&
           adw_switch_row_get_active (ADW_SWITCH_ROW (self->anonymous_row));
}

/* The password may legitimately be empty, so it is not checked here,
 * matching GtkMountOperation. */
static gboolean
input_is_valid (NautilusMountOperation *self)
{
    if (is_anonymous (self))
    {
        return TRUE;
    }

    return row_has_input (self->username_row) &&
           row_has_input (self->domain_row) &&
           pim_row_is_valid (self->pim_row);
}

static void
update_connect_sensitivity (NautilusMountOperation *self)
{
    gboolean anonymous;

    if (self->dialog == NULL || self->connecting)
    {
        return;
    }

    anonymous = is_anonymous (self);

    if (anonymous)
    {
        GtkWidget *focus = adw_dialog_get_focus (self->dialog);

        /* Don't leave focus on a row that is about to turn insensitive: it
         * would never get its focus-out, and GTK complains about it. */
        if (focus != NULL &&
            (gtk_widget_is_ancestor (focus, self->credentials_group) ||
             gtk_widget_is_ancestor (focus, self->remember_group)))
        {
            adw_dialog_set_focus (self->dialog, self->anonymous_row);
        }
    }

    gtk_widget_set_sensitive (self->credentials_group, !anonymous);
    gtk_widget_set_sensitive (self->remember_group, !anonymous);

    adw_alert_dialog_set_response_enabled (ADW_ALERT_DIALOG (self->dialog),
                                           "connect",
                                           input_is_valid (self));
}

static void
on_input_changed (GtkEditable            *row,
                  NautilusMountOperation *self)
{
    update_connect_sensitivity (self);
}

static void
on_anonymous_changed (GObject                *row,
                      GParamSpec             *pspec,
                      NautilusMountOperation *self)
{
    update_connect_sensitivity (self);
}

static void
on_password_changed (GtkEditable            *row,
                     NautilusMountOperation *self)
{
    gtk_widget_remove_css_class (GTK_WIDGET (row), "error");
    gtk_accessible_reset_state (GTK_ACCESSIBLE (row), GTK_ACCESSIBLE_STATE_INVALID);
}

/* Pressing Enter moves on to the next text row, like the GTK dialog does,
 * and connects from the last one. */
static void
on_entry_activated (AdwEntryRow            *row,
                    NautilusMountOperation *self)
{
    GtkWidget *order[] =
    {
        self->username_row, self->domain_row, self->pim_row, self->password_row,
    };
    gboolean found = FALSE;

    for (guint i = 0; i < G_N_ELEMENTS (order); i++)
    {
        if (!row_is_shown (order[i]))
        {
            continue;
        }

        if (found)
        {
            gtk_widget_grab_focus (order[i]);
            return;
        }

        found = (order[i] == GTK_WIDGET (row));
    }

    if (input_is_valid (self))
    {
        gtk_widget_activate_default (GTK_WIDGET (row));
    }
}

/* GtkText keeps its selection after losing focus for as long as it owns the
 * PRIMARY selection, and keeps drawing it. Tabbing through prefilled rows
 * then leaves an inactive row highlighted, which reads as if it were still
 * focused. Collapse the selection when focus leaves the row. */
static void
on_row_focus_leave (GtkEventControllerFocus *controller)
{
    GtkEditable *editable;
    int position;

    editable = GTK_EDITABLE (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (controller)));
    position = gtk_editable_get_position (editable);
    gtk_editable_select_region (editable, position, position);
}

static void
clear_widgets (NautilusMountOperation *self)
{
    self->content = NULL;
    self->error_revealer = NULL;
    self->error_label = NULL;
    self->anonymous_group = NULL;
    self->anonymous_row = NULL;
    self->credentials_group = NULL;
    self->username_row = NULL;
    self->domain_row = NULL;
    self->hidden_volume_row = NULL;
    self->system_volume_row = NULL;
    self->pim_row = NULL;
    self->password_row = NULL;
    self->remember_group = NULL;
    self->remember_row = NULL;
    self->spinner_revealer = NULL;
}

/* Stop waiting for the user: the reference that kept us alive meanwhile
 * goes away, so this may finalize the operation. Call it last. */
static void
release_self (NautilusMountOperation *self)
{
    if (self->holds_ref)
    {
        self->holds_ref = FALSE;
        g_object_unref (self);
    }
}

/* Close the dialog without replying. May finalize the operation. */
static void
close_dialog_full (NautilusMountOperation *self,
                   gboolean                notify)
{
    g_autoptr (AdwDialog) dialog = NULL;

    if (self->dialog == NULL)
    {
        return;
    }

    dialog = g_steal_pointer (&self->dialog);

    g_clear_handle_id (&self->close_attempt_idle_id, g_source_remove);
    g_signal_handlers_disconnect_by_data (dialog, self);
    if (self->parent != NULL)
    {
        g_signal_handlers_disconnect_by_data (self->parent, self);
        g_clear_weak_pointer (&self->parent);
    }

    self->connecting = FALSE;
    clear_widgets (self);

    adw_dialog_set_can_close (dialog, TRUE);
    adw_dialog_force_close (dialog);

    if (notify)
    {
        g_object_notify (G_OBJECT (self), "is-showing");
    }

    release_self (self);
}

static void
close_dialog (NautilusMountOperation *self)
{
    close_dialog_full (self, TRUE);
}

static void
apply_answers (NautilusMountOperation *self)
{
    GMountOperation *op = G_MOUNT_OPERATION (self);
    guint selected;

    self->answered_anonymously = is_anonymous (self);

    if (self->flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED)
    {
        g_mount_operation_set_anonymous (op, self->answered_anonymously);
    }

    if (row_is_shown (self->username_row))
    {
        g_mount_operation_set_username (op, gtk_editable_get_text (GTK_EDITABLE (self->username_row)));
    }

    if (row_is_shown (self->domain_row))
    {
        g_mount_operation_set_domain (op, gtk_editable_get_text (GTK_EDITABLE (self->domain_row)));
    }

    if (row_is_shown (self->password_row))
    {
        g_mount_operation_set_password (op, gtk_editable_get_text (GTK_EDITABLE (self->password_row)));
    }

    if (row_is_shown (self->pim_row) && !row_is_empty (self->pim_row))
    {
        guint pim;

        if (parse_pim (self->pim_row, &pim))
        {
            g_mount_operation_set_pim (op, pim);
        }
    }

    if (row_is_shown (self->hidden_volume_row) &&
        adw_switch_row_get_active (ADW_SWITCH_ROW (self->hidden_volume_row)))
    {
        g_mount_operation_set_is_tcrypt_hidden_volume (op, TRUE);
    }

    if (row_is_shown (self->system_volume_row) &&
        adw_switch_row_get_active (ADW_SWITCH_ROW (self->system_volume_row)))
    {
        g_mount_operation_set_is_tcrypt_system_volume (op, TRUE);
    }

    selected = adw_combo_row_get_selected (ADW_COMBO_ROW (self->remember_row));
    if (row_is_shown (self->remember_group) && selected < G_N_ELEMENTS (remember_choices))
    {
        g_mount_operation_set_password_save (op, remember_choices[selected]);
    }
}

static void
show_connecting (NautilusMountOperation *self)
{
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (self->dialog);

    self->connecting = TRUE;

    /* Move focus away first: a focused text field that turns insensitive
     * never gets its focus-out, and GTK complains about it. */
    adw_dialog_set_focus (self->dialog, NULL);
    gtk_widget_set_sensitive (self->content, FALSE);
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->error_revealer), FALSE);
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->spinner_revealer), TRUE);

    adw_alert_dialog_set_response_label (dialog, "connect", _("Connecting…"));
    adw_alert_dialog_set_response_enabled (dialog, "connect", FALSE);
}

static void
on_connect (NautilusMountOperation *self)
{
    apply_answers (self);
    show_connecting (self);

    g_mount_operation_reply (G_MOUNT_OPERATION (self), G_MOUNT_OPERATION_HANDLED);

    /* From here on, our lifetime is up to whoever runs the mount. */
    release_self (self);
}

static void
on_cancel (NautilusMountOperation *self)
{
    /* While connecting, the answers are already sent and cannot be taken
     * back. Closing the dialog just leaves the mount to finish on its own,
     * as it does with GTK's dialog. */
    if (!self->connecting)
    {
        g_mount_operation_reply (G_MOUNT_OPERATION (self), G_MOUNT_OPERATION_ABORTED);
    }

    close_dialog (self);
}

static void
on_dialog_response (AdwAlertDialog         *dialog,
                    const char             *response,
                    NautilusMountOperation *self)
{
    g_clear_handle_id (&self->close_attempt_idle_id, g_source_remove);

    if (g_strcmp0 (response, "connect") == 0)
    {
        on_connect (self);
    }
    else
    {
        on_cancel (self);
    }
}

/* Closing the window takes the dialog away with it, without a response,
 * and nothing else would answer the backend. The window is not necessarily
 * disposed (GtkMountOperation holds a reference on it), so this watches
 * for it being unrealized, which happens when it is closed. */
static void
on_parent_unrealize (GtkWidget              *parent,
                     NautilusMountOperation *self)
{
    on_cancel (self);
}

static gboolean
on_close_attempt_idle (gpointer user_data)
{
    NautilusMountOperation *self = NAUTILUS_MOUNT_OPERATION (user_data);

    self->close_attempt_idle_id = 0;
    on_cancel (self);

    return G_SOURCE_REMOVE;
}

/* The dialog refuses to close by itself (can-close is FALSE) so that it can
 * stay open while connecting. AdwAlertDialog then emits close-attempt both
 * for Escape and right before emitting "response" for a pressed button.
 * Escape is the one not followed by a response, so treat a close attempt as
 * Cancel unless a response comes in first. */
static void
on_close_attempt (AdwDialog              *dialog,
                  NautilusMountOperation *self)
{
    if (self->close_attempt_idle_id == 0)
    {
        self->close_attempt_idle_id = g_idle_add (on_close_attempt_idle, self);
    }
}

static void
create_dialog (NautilusMountOperation *self)
{
    g_autoptr (GtkBuilder) builder = NULL;
    g_autoptr (GtkBuilderScope) scope = NULL;
    g_autoptr (GError) error = NULL;
    GtkWidget *entry_rows[4];

    scope = gtk_builder_cscope_new ();
    gtk_builder_cscope_add_callback (scope, on_dialog_response);
    gtk_builder_cscope_add_callback (scope, on_close_attempt);
    gtk_builder_cscope_add_callback (scope, on_anonymous_changed);
    gtk_builder_cscope_add_callback (scope, on_input_changed);
    gtk_builder_cscope_add_callback (scope, on_password_changed);
    gtk_builder_cscope_add_callback (scope, on_entry_activated);

    builder = gtk_builder_new ();
    gtk_builder_set_scope (builder, scope);
    /* Signal handlers in the .blp get us as their data. */
    gtk_builder_set_current_object (builder, G_OBJECT (self));
    if (!gtk_builder_add_from_resource (builder, "/org/gnome/nautilus/ui/nautilus-mount-operation.ui", &error))
    {
        g_error ("Failed to load the mount operation dialog: %s", error->message);
    }

    self->dialog = g_object_ref (ADW_DIALOG (gtk_builder_get_object (builder, "dialog")));

#define GET_WIDGET(name) self->name = GTK_WIDGET (gtk_builder_get_object (builder, #name))
    GET_WIDGET (content);
    GET_WIDGET (error_revealer);
    GET_WIDGET (error_label);
    GET_WIDGET (anonymous_group);
    GET_WIDGET (anonymous_row);
    GET_WIDGET (credentials_group);
    GET_WIDGET (username_row);
    GET_WIDGET (domain_row);
    GET_WIDGET (hidden_volume_row);
    GET_WIDGET (system_volume_row);
    GET_WIDGET (pim_row);
    GET_WIDGET (password_row);
    GET_WIDGET (remember_group);
    GET_WIDGET (remember_row);
    GET_WIDGET (spinner_revealer);
#undef GET_WIDGET

    entry_rows[0] = self->username_row;
    entry_rows[1] = self->domain_row;
    entry_rows[2] = self->pim_row;
    entry_rows[3] = self->password_row;
    for (guint i = 0; i < G_N_ELEMENTS (entry_rows); i++)
    {
        GtkEventController *focus = gtk_event_controller_focus_new ();

        g_signal_connect (focus, "leave", G_CALLBACK (on_row_focus_leave), NULL);
        gtk_widget_add_controller (entry_rows[i], focus);
    }
}

static GtkWidget *
row_to_focus (NautilusMountOperation *self,
              gboolean                retry)
{
    GtkWidget *order[] =
    {
        self->username_row, self->domain_row, self->password_row,
    };

    if (is_anonymous (self))
    {
        return NULL;
    }

    if (retry && row_is_shown (self->password_row))
    {
        return self->password_row;
    }

    for (guint i = 0; i < G_N_ELEMENTS (order); i++)
    {
        if (row_is_shown (order[i]) && row_is_empty (order[i]))
        {
            return order[i];
        }
    }

    return NULL;
}

/* Set the dialog up for a request. @retry means the backend asked again
 * after the previous answer, i.e. it was not accepted; what was typed then
 * is still in the rows. */
static void
prepare_dialog (NautilusMountOperation *self,
                const char             *message,
                const char             *default_user,
                const char             *default_domain,
                gboolean                retry)
{
    GMountOperation *op = G_MOUNT_OPERATION (self);
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG (self->dialog);
    GAskPasswordFlags flags = self->flags;
    const char *newline;

    /* Backends send "Primary text\nSecondary text". */
    newline = strchr (message, '\n');
    if (newline != NULL)
    {
        g_autofree char *heading = g_strndup (message, newline - message);

        adw_alert_dialog_set_heading (dialog, heading);
        adw_alert_dialog_set_body (dialog, newline + 1);
    }
    else
    {
        adw_alert_dialog_set_heading (dialog, message);
        adw_alert_dialog_set_body (dialog, "");
    }

    gtk_widget_set_visible (self->anonymous_group, (flags & G_ASK_PASSWORD_ANONYMOUS_SUPPORTED) != 0);
    gtk_widget_set_visible (self->username_row, (flags & G_ASK_PASSWORD_NEED_USERNAME) != 0);
    gtk_widget_set_visible (self->domain_row, (flags & G_ASK_PASSWORD_NEED_DOMAIN) != 0);
    gtk_widget_set_visible (self->hidden_volume_row, (flags & G_ASK_PASSWORD_TCRYPT) != 0);
    gtk_widget_set_visible (self->system_volume_row, (flags & G_ASK_PASSWORD_TCRYPT) != 0);
    gtk_widget_set_visible (self->pim_row, (flags & G_ASK_PASSWORD_TCRYPT) != 0);
    gtk_widget_set_visible (self->password_row, (flags & G_ASK_PASSWORD_NEED_PASSWORD) != 0);
    gtk_widget_set_visible (self->remember_group, (flags & G_ASK_PASSWORD_SAVING_SUPPORTED) != 0);

    if (!retry)
    {
        GPasswordSave password_save = g_mount_operation_get_password_save (op);

        gtk_editable_set_text (GTK_EDITABLE (self->username_row), default_user != NULL ? default_user : "");
        gtk_editable_set_text (GTK_EDITABLE (self->domain_row), default_domain != NULL ? default_domain : "");

        for (guint i = 0; i < G_N_ELEMENTS (remember_choices); i++)
        {
            if (remember_choices[i] == password_save)
            {
                adw_combo_row_set_selected (ADW_COMBO_ROW (self->remember_row), i);
                break;
            }
        }
    }

    /* GtkMountOperation always starts out anonymous. When the backend
     * already suggests a username, signing in as that user is far more
     * likely to be what is wanted, and it lets focus land straight on the
     * password. After a rejected attempt, offer signing in. */
    adw_switch_row_set_active (ADW_SWITCH_ROW (self->anonymous_row),
                               !retry && row_is_empty (self->username_row));

    gtk_editable_set_text (GTK_EDITABLE (self->password_row), "");

    if (retry)
    {
        gtk_label_set_text (GTK_LABEL (self->error_label),
                            self->answered_anonymously ?
                            _("The server did not allow connecting anonymously.") :
                            _("The server did not accept the username or password."));

        if (!self->answered_anonymously)
        {
            gtk_widget_add_css_class (self->password_row, "error");
            gtk_accessible_update_state (GTK_ACCESSIBLE (self->password_row),
                                         GTK_ACCESSIBLE_STATE_INVALID, GTK_ACCESSIBLE_INVALID_TRUE,
                                         -1);
        }

        /* The dialog does not change otherwise, so say what happened. */
        gtk_accessible_announce (GTK_ACCESSIBLE (self->dialog),
                                 gtk_label_get_text (GTK_LABEL (self->error_label)),
                                 GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
    }
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->error_revealer), retry);

    /* Back from connecting, if we were. */
    self->connecting = FALSE;
    gtk_widget_set_sensitive (self->content, TRUE);
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->spinner_revealer), FALSE);
    adw_alert_dialog_set_response_label (dialog, "connect", _("Co_nnect"));

    update_connect_sensitivity (self);
}

static void
ask (NautilusMountOperation *self,
     const char             *message,
     const char             *default_user,
     const char             *default_domain)
{
    gboolean retry = self->dialog != NULL;
    GtkWidget *focus;

    if (!retry)
    {
        create_dialog (self);
    }

    /* Keep the operation alive while waiting for the user, like GTK does. */
    if (!self->holds_ref)
    {
        self->holds_ref = TRUE;
        g_object_ref (self);
    }

    prepare_dialog (self, message, default_user, default_domain, retry);

    if (!retry)
    {
        GtkWindow *parent = gtk_mount_operation_get_parent (GTK_MOUNT_OPERATION (self));

        if (parent != NULL)
        {
            g_set_weak_pointer (&self->parent, GTK_WIDGET (parent));
            g_signal_connect (parent, "unrealize", G_CALLBACK (on_parent_unrealize), self);
        }

        g_object_notify (G_OBJECT (self), "is-showing");
        adw_dialog_present (self->dialog, self->parent);
    }

    focus = row_to_focus (self, retry);
    if (focus != NULL)
    {
        gtk_widget_grab_focus (focus);
    }
}

static void
nautilus_mount_operation_ask_password (GMountOperation   *op,
                                       const char        *message,
                                       const char        *default_user,
                                       const char        *default_domain,
                                       GAskPasswordFlags  flags)
{
    NautilusMountOperation *self = NAUTILUS_MOUNT_OPERATION (op);

    /* Leave password-only prompts to GTK so they keep going to the shell's
     * mount operation handler, as they do today. */
    if (!(flags & (G_ASK_PASSWORD_NEED_USERNAME | G_ASK_PASSWORD_NEED_DOMAIN)))
    {
        GMountOperationClass *parent_class = G_MOUNT_OPERATION_CLASS (nautilus_mount_operation_parent_class);

        /* Our dialog may still be showing its spinner. Keep ourselves alive
         * past closing it, since GTK takes over from here. */
        g_object_ref (self);
        close_dialog (self);
        parent_class->ask_password (op, message, default_user, default_domain, flags);
        g_object_unref (self);
        return;
    }

    /* A second request while the first is still unanswered is a bug in the
     * caller; the first one would never get its reply. */
    g_return_if_fail (self->dialog == NULL || self->connecting);

    self->flags = flags;
    ask (self, message, default_user, default_domain);
}

static void
nautilus_mount_operation_aborted (GMountOperation *op)
{
    NautilusMountOperation *self = NAUTILUS_MOUNT_OPERATION (op);

    close_dialog (self);

    G_MOUNT_OPERATION_CLASS (nautilus_mount_operation_parent_class)->aborted (op);
}

static void
nautilus_mount_operation_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    NautilusMountOperation *self = NAUTILUS_MOUNT_OPERATION (object);

    switch (prop_id)
    {
        case PROP_IS_SHOWING:
        {
            /* Our dialog is showing, or one of GtkMountOperation's is
             * (questions, processes, or the shell's handler). */
            GObjectClass *parent_class = G_OBJECT_CLASS (nautilus_mount_operation_parent_class);
            GParamSpec *parent_pspec = g_object_class_find_property (parent_class, "is-showing");

            parent_class->get_property (object, parent_pspec->param_id, value, parent_pspec);
            if (self->dialog != NULL)
            {
                g_value_set_boolean (value, TRUE);
            }
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
        break;
    }
}

static void
nautilus_mount_operation_dispose (GObject *object)
{
    NautilusMountOperation *self = NAUTILUS_MOUNT_OPERATION (object);

    /* The mount is over: whoever ran it has let go of us. Don't notify
     * "is-showing" here: the mount has already been reported finished, and
     * whatever a handler was watching for may be gone by now. Callers that
     * paused their own progress while the dialog showed resume it from
     * their mount callback instead. */
    close_dialog_full (self, FALSE);

    G_OBJECT_CLASS (nautilus_mount_operation_parent_class)->dispose (object);
}

static void
nautilus_mount_operation_class_init (NautilusMountOperationClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GMountOperationClass *mount_op_class = G_MOUNT_OPERATION_CLASS (klass);

    object_class->get_property = nautilus_mount_operation_get_property;
    object_class->dispose = nautilus_mount_operation_dispose;

    mount_op_class->ask_password = nautilus_mount_operation_ask_password;
    mount_op_class->aborted = nautilus_mount_operation_aborted;

    /* Callers watch notify::is-showing to pause their own progress UI while
     * a prompt is up. GtkMountOperation computes it from its own dialog, so
     * extend it to cover ours. */
    g_object_class_override_property (object_class, PROP_IS_SHOWING, "is-showing");
}

static void
nautilus_mount_operation_init (NautilusMountOperation *self)
{
}

/**
 * nautilus_mount_operation_new:
 * @parent: (nullable): transient parent of the dialogs
 *
 * Creates a mount operation that asks for credentials with a libadwaita
 * dialog. Drop-in replacement for gtk_mount_operation_new().
 *
 * The credentials dialog stays open with a spinner after Connect until the
 * operation is released, so callers must not keep a reference to it past
 * the end of the mount.
 *
 * Returns: (transfer full): a new #GMountOperation
 */
GMountOperation *
nautilus_mount_operation_new (GtkWindow *parent)
{
    return g_object_new (NAUTILUS_TYPE_MOUNT_OPERATION,
                         "parent", parent,
                         NULL);
}
