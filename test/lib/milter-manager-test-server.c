/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 *  Copyright (C) 2008  Kouhei Sutou <kou@cozmixng.org>
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
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifdef HAVE_CONFIG_H
#  include "../../config.h"
#endif /* HAVE_CONFIG_H */

#include <gcutter.h>
#include "milter-manager-test-server.h"
#include "milter-manager-test-utils.h"

#define MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(obj)                    \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                          \
                                 MILTER_TYPE_MANAGER_TEST_SERVER,      \
                                 MilterManagerTestServerPrivate))

typedef struct _MilterManagerTestServerPrivate	MilterManagerTestServerPrivate;
struct _MilterManagerTestServerPrivate
{
    guint n_add_headers;
    guint n_change_headers;
    guint n_insert_headers;
    guint n_change_froms;
    guint n_add_recipients;
    guint n_delete_recipients;
    guint n_replace_bodies;
    guint n_progresses;
    guint n_quarantines;

    GList *headers;
    GList *recipients;
    gchar *from;
    gchar *from_parameters;
    gchar *quarantine_reason;
};

enum
{
    PROP_0
};

static MilterReplySignals *reply_parent;
static void reply_init (MilterReplySignalsClass *reply);

G_DEFINE_TYPE_WITH_CODE(MilterManagerTestServer, milter_manager_test_server, MILTER_TYPE_SERVER_CONTEXT,
    G_IMPLEMENT_INTERFACE(MILTER_TYPE_REPLY_SIGNALS, reply_init))

static void dispose        (GObject         *object);
static void set_property   (GObject         *object,
                            guint            prop_id,
                            const GValue    *value,
                            GParamSpec      *pspec);
static void get_property   (GObject         *object,
                            guint            prop_id,
                            GValue          *value,
                            GParamSpec      *pspec);
static void add_header     (MilterReplySignals *reply,
                            const gchar        *name,
                            const gchar        *value);
static void insert_header  (MilterReplySignals *reply,
                            guint32             index,
                            const gchar        *name,
                            const gchar        *value);
static void change_header  (MilterReplySignals *reply,
                            const gchar        *name,
                            guint32             index,
                            const gchar        *value);
static void change_from    (MilterReplySignals *reply,
                            const gchar        *from,
                            const gchar        *parameters);
static void add_recipient  (MilterReplySignals *reply,
                            const gchar        *recipient,
                            const gchar        *parameters);
static void delete_recipient
                           (MilterReplySignals *reply,
                            const gchar        *recipient);
static void replace_body   (MilterReplySignals *reply,
                            const gchar        *body,
                            gsize               body_size);
static void progress       (MilterReplySignals *reply);
static void quarantine     (MilterReplySignals *reply,
                            const gchar        *reason);

static void
milter_manager_test_server_class_init (MilterManagerTestServerClass *klass)
{
    GObjectClass *gobject_class;

    gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose      = dispose;
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    g_type_class_add_private(gobject_class,
                             sizeof(MilterManagerTestServerPrivate));
}

static void
reply_init (MilterReplySignalsClass *reply)
{
    reply_parent = g_type_interface_peek_parent(reply);

    reply->add_header       = add_header;
    reply->change_header    = change_header;
    reply->insert_header    = insert_header;
    reply->change_from      = change_from;
    reply->add_recipient    = add_recipient;
    reply->delete_recipient = delete_recipient;
    reply->replace_body     = replace_body;
    reply->progress         = progress;
    reply->quarantine       = quarantine;
}

static void
milter_manager_test_server_init (MilterManagerTestServer *milter)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(milter);

    priv->headers = NULL;
    priv->recipients = NULL;
    priv->from = NULL;
    priv->from_parameters = NULL;
    priv->quarantine_reason = NULL;

    priv->n_add_headers = 0;
    priv->n_change_headers = 0;
    priv->n_insert_headers = 0;
    priv->n_change_froms = 0;
    priv->n_add_recipients = 0;
    priv->n_delete_recipients = 0;
    priv->n_replace_bodies = 0;
    priv->n_progresses = 0;
    priv->n_quarantines = 0;
}

static void
dispose (GObject *object)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(object);

    if (priv->headers) {
        g_list_foreach(priv->headers,
                       (GFunc)milter_manager_test_header_free, NULL);
        g_list_free(priv->headers);
        priv->headers = NULL;
    }

    if (priv->recipients) {
        g_list_foreach(priv->recipients, (GFunc)g_free, NULL);
        g_list_free(priv->recipients);
        priv->recipients = NULL;
    }

    if (priv->from) {
        g_free(priv->from);
        priv->from = NULL;
    }

    if (priv->from_parameters) {
        g_free(priv->from_parameters);
        priv->from_parameters = NULL;
    }

    if (priv->quarantine_reason) {
        g_free(priv->quarantine_reason);
        priv->quarantine_reason = NULL;
    }

    G_OBJECT_CLASS(milter_manager_test_server_parent_class)->dispose(object);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(object);
    switch (prop_id) {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(object);
    switch (prop_id) {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

MilterManagerTestServer *
milter_manager_test_server_new (void)
{
    return  g_object_new(MILTER_TYPE_MANAGER_TEST_SERVER,
                         NULL);
}

static void
add_header (MilterReplySignals *reply,
            const gchar        *name,
            const gchar        *value)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(reply);
    priv->n_add_headers++;
    priv->headers = g_list_append(priv->headers,
                                  milter_manager_test_header_new(name, value));
}

static void
insert_header (MilterReplySignals *reply,
               guint32             index,
               const gchar        *name,
               const gchar        *value)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(reply);
    priv->n_insert_headers++;

    priv->headers = g_list_insert(priv->headers,
                                  milter_manager_test_header_new(name, value),
                                  index);
}

static void
change_header (MilterReplySignals *reply,
               const gchar        *name,
               guint32             index,
               const gchar        *value)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(reply);
    priv->n_change_headers++;
}

static void
change_from (MilterReplySignals *reply,
             const gchar        *from,
             const gchar        *parameters)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(reply);
    priv->n_change_froms++;

    if (priv->from)
        g_free(priv->from);
    priv->from = g_strdup(from);

    if (priv->from_parameters)
        g_free(priv->from_parameters);
    priv->from_parameters = g_strdup(parameters);
}

static void
add_recipient (MilterReplySignals *reply,
               const gchar        *recipient,
               const gchar        *parameters)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(reply);
    priv->n_add_recipients++;

    priv->recipients = g_list_append(priv->recipients, g_strdup(recipient));
}

static void
delete_recipient (MilterReplySignals *reply, const gchar *recipient)
{
    MilterManagerTestServerPrivate *priv;
    GList *list;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(reply);
    priv->n_delete_recipients++;

    list = g_list_find(priv->recipients, recipient);
    if (list) {
        g_free(list->data);
        priv->recipients = g_list_delete_link(priv->recipients, list);
    }
}

static void
replace_body (MilterReplySignals *reply,
              const gchar        *body,
              gsize               body_size)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(reply);
    priv->n_replace_bodies++;
}

static void
progress (MilterReplySignals *reply)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(reply);
    priv->n_progresses++;
}

static void
quarantine (MilterReplySignals *reply, const gchar *reason)
{
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(reply);

    priv->n_quarantines++;

    if (priv->quarantine_reason)
        g_free(priv->quarantine_reason);

    priv->quarantine_reason = g_strdup(reason);
}

guint
milter_manager_test_server_get_n_add_headers (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->n_add_headers;
}

guint
milter_manager_test_server_get_n_insert_headers (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->n_insert_headers;
}

guint
milter_manager_test_server_get_n_change_headers (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->n_change_headers;
}

guint
milter_manager_test_server_get_n_change_froms (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->n_change_froms;
}

guint
milter_manager_test_server_get_n_add_recipients (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->n_add_recipients;
}

guint
milter_manager_test_server_get_n_delete_recipients (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->n_delete_recipients;
}

guint
milter_manager_test_server_get_n_replace_bodies (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->n_replace_bodies;
}

guint
milter_manager_test_server_get_n_progresses (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->n_progresses;
}

guint
milter_manager_test_server_get_n_quarantines (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->n_quarantines;
}

static gboolean
cb_waiting (gpointer data)
{
    gboolean *waiting = data;

    *waiting = FALSE;
    return FALSE;
}

const GList *
milter_manager_test_server_get_headers (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->headers;
}

const gchar *
milter_manager_test_server_get_quarantine_reason (MilterManagerTestServer *server)
{
    return MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server)->quarantine_reason;
}

void
milter_manager_test_server_wait_signal (MilterManagerTestServer *server)
{
    gboolean timeout_waiting = TRUE;
    gboolean idle_waiting = TRUE;
    guint timeout_waiting_id, idle_id;
    MilterManagerTestServerPrivate *priv;

    priv = MILTER_MANAGER_TEST_SERVER_GET_PRIVATE(server);

    idle_id = g_idle_add_full(G_PRIORITY_DEFAULT,
                              cb_waiting, &idle_waiting, NULL);

    timeout_waiting_id = g_timeout_add_seconds(1, cb_waiting,
                                               &timeout_waiting);
    while (idle_waiting) {
        g_main_context_iteration(NULL, FALSE);
    }

    g_source_remove(idle_id);
    g_source_remove(timeout_waiting_id);

    cut_assert_true(timeout_waiting, "timeout");
}

/*
vi:ts=4:nowrap:ai:expandtab:sw=4
*/
