/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 *  Copyright (C) 2008-2011  Kouhei Sutou <kou@clear-code.com>
 *
 *  This library is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "../../config.h"
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <errno.h>

#include <glib/gi18n.h>

#include "../client.h"
#include "milter-client-private.h"

static gboolean initialized = FALSE;
static guint milter_client_log_handler_id = 0;

void
milter_client_init (void)
{
    if (initialized)
        return;

    initialized = TRUE;

    milter_client_log_handler_id = MILTER_GLIB_LOG_DELEGATE("milter-client");
}

void
milter_client_quit (void)
{
    if (!initialized)
        return;

    g_log_remove_handler("milter-client", milter_client_log_handler_id);

    milter_client_log_handler_id = 0;
    initialized = FALSE;
}

static gboolean
parse_connection_spec (const gchar *option_name,
                       const gchar *value,
                       gpointer data,
                       GError **error)
{
    MilterClient *client = data;
    GError *spec_error = NULL;
    gboolean success;

    success = milter_client_set_connection_spec(client, value, error);
    if (!success) {
        g_set_error(error,
                    G_OPTION_ERROR,
                    G_OPTION_ERROR_BAD_VALUE,
                    "%s: %s",
                    option_name,
                    spec_error->message);
        g_error_free(spec_error);
    }

    return success;
}

static gboolean
parse_verbose (const gchar *option_name,
               const gchar *value,
               gpointer data,
               GError **error)
{
    milter_logger_set_target_level(milter_logger(), MILTER_LOG_LEVEL_ALL);
    return TRUE;
}

static gboolean
parse_syslog (const gchar *option_name,
              const gchar *value,
              gpointer data,
              GError **error)
{
    MilterClient *client = data;

    milter_client_start_syslog(client);
    return TRUE;
}

static gboolean
parse_syslog_facility (const gchar *option_name,
                       const gchar *value,
                       gpointer data,
                       GError **error)
{
    MilterClient *client = data;

    milter_client_set_syslog_facility(client, value);
    return TRUE;
}

static gboolean
parse_daemon (const gchar *option_name,
              const gchar *value,
              gpointer data,
              GError **error)
{
    MilterClient *client = data;

    milter_client_set_run_as_daemon(client, TRUE);
    return TRUE;
}

static gboolean
parse_user (const gchar *option_name,
            const gchar *value,
            gpointer data,
            GError **error)
{
    MilterClient *client = data;

    milter_client_set_effective_user(client, value);
    return TRUE;
}

static gboolean
parse_group (const gchar *option_name,
             const gchar *value,
             gpointer data,
             GError **error)
{
    MilterClient *client = data;

    milter_client_set_effective_group(client, value);
    return TRUE;
}

static gboolean
parse_unix_socket_group (const gchar *option_name,
                         const gchar *value,
                         gpointer data,
                         GError **error)
{
    MilterClient *client = data;

    milter_client_set_unix_socket_group(client, value);
    return TRUE;
}

static gboolean
parse_unix_socket_mode (const gchar *option_name,
                        const gchar *value,
                        gpointer data,
                        GError **error)
{
    MilterClient *client = data;
    gboolean success;
    guint unix_socket_mode;
    gchar *error_message = NULL;

    success = milter_utils_parse_file_mode(value,
                                           &unix_socket_mode,
                                           &error_message);
    if (success) {
        milter_client_set_unix_socket_mode(client, unix_socket_mode);
    } else {
        g_set_error(error,
                    G_OPTION_ERROR,
                    G_OPTION_ERROR_BAD_VALUE,
                    "%s: %s",
                    option_name,
                    error_message);
        g_free(error_message);
    }

    return success;
}

static gboolean
parse_n_workers (const gchar *option_name,
                 const gchar *value,
                 gpointer data,
                 GError **error)
{
    MilterClient *client = data;
    gchar *end;
    glong n_workers;

    errno = 0;
    n_workers = strtol(value, &end, 0);

    if (end[0] != '\0') {
        g_set_error(error,
                    G_OPTION_ERROR,
                    G_OPTION_ERROR_BAD_VALUE,
                    _("%s: invalid integer value: <%s>(%.*s>%c<%s)"),
                    option_name,
                    value,
                    (int)(end - value), value,
                    end[0],
                    end + 1);
        return FALSE;
    }

    if (n_workers > MILTER_CLIENT_MAX_N_WORKERS || errno == ERANGE) {
        g_set_error(error,
                    G_OPTION_ERROR,
                    G_OPTION_ERROR_BAD_VALUE,
                    _("%s: too big: <%s>: parsed=<%ld>, max=<%d>"),
                    option_name,
                    value,
                    n_workers,
                    MILTER_CLIENT_MAX_N_WORKERS);
      return FALSE;
    }

    milter_client_set_n_workers(client, n_workers);

    return TRUE;
}

static gboolean
parse_event_loop_backend (const gchar *option_name,
                          const gchar *value,
                          gpointer data,
                          GError **error)
{
    MilterClient *client = data;
    GEnumClass *backend_enum;
    GEnumValue *backend_value;
    gboolean success;

    backend_enum = g_type_class_ref(MILTER_TYPE_CLIENT_EVENT_LOOP_BACKEND);
    backend_value = g_enum_get_value_by_nick(backend_enum, value);
    if (backend_value) {
        milter_client_set_event_loop_backend(client, backend_value->value);
        success = TRUE;
    } else {
        g_set_error(error,
                    G_OPTION_ERROR,
                    G_OPTION_ERROR_BAD_VALUE,
                    _("%s: unknown event loop backend: <%s> "
                      "available values: [glib|libev]"),
                    option_name, value);
        success = FALSE;
    }
    g_type_class_unref(backend_enum);

    return success;
}

static gboolean
parse_packet_buffer_size (const gchar *option_name,
                          const gchar *value,
                          gpointer data,
                          GError **error)
{
    MilterClient *client = data;
    gchar *end;
    glong packet_buffer_size;

    errno = 0;
    packet_buffer_size = strtol(value, &end, 0);

    if (end[0] != '\0') {
        g_set_error(error,
                    G_OPTION_ERROR,
                    G_OPTION_ERROR_BAD_VALUE,
                    _("%s: invalid integer value: <%s>(%.*s>%c<%s)"),
                    option_name,
                    value,
                    (int)(end - value), value,
                    end[0],
                    end + 1);
        return FALSE;
    }

    if (packet_buffer_size > G_MAXUINT || errno == ERANGE) {
        g_set_error(error,
                    G_OPTION_ERROR,
                    G_OPTION_ERROR_BAD_VALUE,
                    _("%s: too big: <%s>: parsed=<%ld>, max=<%u>"),
                    option_name,
                    value,
                    packet_buffer_size,
                    G_MAXUINT);
      return FALSE;
    }

    if (packet_buffer_size < 0) {
        g_set_error(error,
                    G_OPTION_ERROR,
                    G_OPTION_ERROR_BAD_VALUE,
                    _("%s: must be larger than 0 or equal to 0: "
                      "<%s>: parsed=<%ld>"),
                    option_name,
                    value,
                    packet_buffer_size);
      return FALSE;
    }

    milter_client_set_default_packet_buffer_size(client, packet_buffer_size);

    return TRUE;
}

static const GOptionEntry option_entries[] =
{
    {"connection-spec", 's', 0, G_OPTION_ARG_CALLBACK, parse_connection_spec,
     N_("The spec of socket. (unix:PATH|inet:PORT[@HOST]|inet6:PORT[@HOST])"),
     "SPEC"},
    {"verbose", 'v', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, parse_verbose,
     N_("Be verbose"), NULL},
    {"syslog", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, parse_syslog,
     N_("Use Syslog"), NULL},
    {"syslog-facility", 0, 0, G_OPTION_ARG_CALLBACK, parse_syslog_facility,
     N_("Use facility for syslog"), "FACILITY"},
    {"daemon", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, parse_daemon,
     N_("Run as a daemon"), NULL},
    {"user", 0, 0, G_OPTION_ARG_CALLBACK, parse_user,
     N_("Run as USER's process (need root privilege)"), "USER"},
    {"group", 0, 0, G_OPTION_ARG_CALLBACK, parse_group,
     N_("Run as GROUP's process (need root privilege)"), "GROUP"},
    {"unix-socket-group", 0, 0, G_OPTION_ARG_CALLBACK, parse_unix_socket_group,
     N_("Change UNIX domain socket group to GROUP"), "GROUP"},
    {"unix-socket-mode", 0, 0, G_OPTION_ARG_CALLBACK, parse_unix_socket_mode,
     N_("Change UNIX domain socket mode to MODE (default: 0660)"), "MODE"},
    {"n-workers", 0, 0, G_OPTION_ARG_CALLBACK, parse_n_workers,
     N_("Run N_WORKERS processes (default: 0)"), "N_WORKERS"},
    {"event-loop-backend", 0, 0, G_OPTION_ARG_CALLBACK, parse_event_loop_backend,
     N_("Use BACKEND as event loop backend (glib|libev) (default: glib)"),
     "BACKEND"},
    {"packet-buffer-size", 0, 0, G_OPTION_ARG_CALLBACK, parse_packet_buffer_size,
     N_("Use SIZE as packet buffer size in bytes. 0 disables packet buffering. "
        "(default: 0; disabled)"), "SIZE"},
    {NULL}
};

static gboolean
hook_pre_parse (GOptionContext *context, GOptionGroup *group,
                gpointer data, GError **error)
{
    return TRUE;
}

static gboolean
hook_post_parse (GOptionContext *context, GOptionGroup *group,
                 gpointer data, GError **error)
{
    return TRUE;
}

GOptionGroup *
milter_client_get_option_group (MilterClient *client)
{
    GOptionGroup *group;

    group = g_option_group_new("milter",
                               "Milter Common Options",
                               "Show milter help options",
                               g_object_ref(client),
                               (GDestroyNotify)g_object_unref);
    g_option_group_add_entries(group, option_entries);
    g_option_group_set_translation_domain(group, GETTEXT_PACKAGE);
    g_option_group_set_parse_hooks(group, hook_pre_parse, hook_post_parse);
    return group;
}

/*
vi:ts=4:nowrap:ai:expandtab:sw=4
*/