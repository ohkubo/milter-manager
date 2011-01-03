/* -*- Mode: C; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 *  Copyright (C) 2008-2010  Kouhei Sutou <kou@clear-code.com>
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
#include <string.h>

#include <glib.h>

#include "milter-reader.h"
#include "milter-logger.h"
#include "milter-utils.h"
#include "milter-marshalers.h"

#define MILTER_READER_GET_PRIVATE(obj)                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                 \
                                 MILTER_TYPE_READER,    \
                                 MilterReaderPrivate))

typedef struct _MilterReaderPrivate	MilterReaderPrivate;
struct _MilterReaderPrivate
{
    GIOChannel *io_channel;
    MilterEventLoop *loop;
    guint channel_watch_id;
    gboolean processing;
    gboolean shutdown_requested;
    guint tag;
};

enum
{
    PROP_0,
    PROP_IO_CHANNEL,
    PROP_TAG
};

enum
{
    FLOW,
    LAST_SIGNAL
};

static gint signals[LAST_SIGNAL] = {0};

MILTER_IMPLEMENT_ERROR_EMITTABLE(error_emittable_init);
MILTER_IMPLEMENT_FINISHED_EMITTABLE(finished_emittable_init);
G_DEFINE_TYPE_WITH_CODE(MilterReader, milter_reader, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE(MILTER_TYPE_ERROR_EMITTABLE, error_emittable_init)
    G_IMPLEMENT_INTERFACE(MILTER_TYPE_FINISHED_EMITTABLE, finished_emittable_init))

static void dispose        (GObject         *object);
static void set_property   (GObject         *object,
                            guint            prop_id,
                            const GValue    *value,
                            GParamSpec      *pspec);
static void get_property   (GObject         *object,
                            guint            prop_id,
                            GValue          *value,
                            GParamSpec      *pspec);

static void
milter_reader_class_init (MilterReaderClass *klass)
{
    GObjectClass *gobject_class;
    GParamSpec *spec;

    gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose      = dispose;
    gobject_class->set_property = set_property;
    gobject_class->get_property = get_property;

    spec = g_param_spec_pointer("io-channel",
                                "GIOChannel object",
                                "The GIOChannel object",
                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
    g_object_class_install_property(gobject_class, PROP_IO_CHANNEL, spec);

    spec = g_param_spec_uint("tag",
                             "Tag",
                             "The tag of the writer",
                             0, G_MAXUINT, 0,
                             G_PARAM_READABLE);
    g_object_class_install_property(gobject_class, PROP_TAG, spec);

    signals[FLOW] =
        g_signal_new("flow",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(MilterReaderClass, flow),
                     NULL, NULL,
                     _milter_marshal_VOID__POINTER_UINT,
                     G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);

    g_type_class_add_private(gobject_class, sizeof(MilterReaderPrivate));
}

static void
milter_reader_init (MilterReader *reader)
{
    MilterReaderPrivate *priv;

    priv = MILTER_READER_GET_PRIVATE(reader);
    priv->io_channel = NULL;
    priv->loop = NULL;
    priv->channel_watch_id = 0;
    priv->processing = FALSE;
    priv->shutdown_requested = FALSE;
    priv->tag = 0;
}

#define BUFFER_SIZE 4096
static gboolean
read_from_channel (MilterReader *reader, GIOChannel *channel)
{
    MilterReaderPrivate *priv;
    gboolean error_occurred = FALSE;
    gboolean eof = FALSE;

    priv = MILTER_READER_GET_PRIVATE(reader);
    while (!error_occurred) {
        GIOStatus status;
        gchar stream[BUFFER_SIZE + 1];
        gsize length = 0;
        GError *io_error = NULL;

        status = g_io_channel_read_chars(channel, stream, BUFFER_SIZE,
                                         &length, &io_error);
        if (status == G_IO_STATUS_EOF) {
            milter_debug("[%u] [reader][eof]", priv->tag);
            eof = TRUE;
        }
        if (io_error) {
            GError *error = NULL;

            priv->shutdown_requested = TRUE;

            milter_utils_set_error_with_sub_error(&error,
                                                  MILTER_READER_ERROR,
                                                  MILTER_READER_ERROR_IO_ERROR,
                                                  io_error,
                                                  "I/O error");
            milter_error("[%u] [reader][error][read] %s",
                         priv->tag, error->message);
            milter_error_emittable_emit(MILTER_ERROR_EMITTABLE(reader),
                                        error);
            g_error_free(error);
            error_occurred = TRUE;
            break;
        }

        if (length <= 0)
            break;

        g_signal_emit(reader, signals[FLOW], 0, stream, length);

        if (eof)
            break;
    }

    return !error_occurred && !eof;
}

static void
clear_watch_id (MilterReaderPrivate *priv)
{
    if (priv->channel_watch_id) {
        milter_event_loop_remove_source(priv->loop,
                                        priv->channel_watch_id);
        priv->channel_watch_id = 0;
    }
}

static void
finish (MilterReader *reader)
{
    MilterReaderPrivate *priv;

    priv = MILTER_READER_GET_PRIVATE(reader);
    priv->shutdown_requested = FALSE;
    clear_watch_id(priv);
    milter_finished_emittable_emit(MILTER_FINISHED_EMITTABLE(reader));
}

static gboolean
channel_watch_func (GIOChannel *channel, GIOCondition condition, gpointer data)
{
    MilterReaderPrivate *priv;
    MilterReader *reader = data;
    gboolean keep_callback = TRUE;
    gboolean error_occurred = FALSE;

    priv = MILTER_READER_GET_PRIVATE(reader);
    priv->processing = TRUE;

    if (!priv->shutdown_requested) {
        if (condition & (G_IO_IN | G_IO_PRI)) {
            milter_debug("[%d] [reader] reading from io channel...", priv->tag);
            keep_callback = read_from_channel(reader, channel);
        } else if (condition & G_IO_HUP) {
            milter_debug("[%d] [reader] connection is closed", priv->tag);
            priv->shutdown_requested = TRUE;
        }
    }

    if (condition & G_IO_ERR)
        error_occurred = TRUE;
    if (!priv->shutdown_requested) {
        if (!keep_callback && (condition & G_IO_HUP))
            error_occurred = TRUE;
        if (condition & G_IO_NVAL)
            error_occurred = TRUE;
    }
    if (error_occurred) {
        gchar *message;
        GError *error = NULL;

        priv->shutdown_requested = TRUE;

        message = milter_utils_inspect_io_condition_error(condition);
        g_set_error(&error,
                    MILTER_READER_ERROR,
                    MILTER_READER_ERROR_IO_ERROR,
                    "%s", message);
        milter_error("[%u] [reader][error][read] %s", priv->tag, message);
        milter_error_emittable_emit(MILTER_ERROR_EMITTABLE(reader),
                                    error);
        g_error_free(error);
        g_free(message);
        keep_callback = FALSE;
    }

    if (priv->shutdown_requested) {
        milter_debug("[%u] [reader] shutdown requested.", priv->tag);
        keep_callback = FALSE;
    }

    if (!keep_callback) {
        milter_debug("[%u] [reader] removing reader watcher.", priv->tag);
        priv->channel_watch_id = 0;
        finish(reader);
    }

    priv->processing = FALSE;

    return keep_callback;
}

static void
watch_io_channel (MilterReader *reader, MilterEventLoop *loop)
{
    MilterReaderPrivate *priv;

    priv = MILTER_READER_GET_PRIVATE(reader);

    priv->loop = loop;
    priv->channel_watch_id =
        milter_event_loop_add_watch(loop,
                                    priv->io_channel,
                                    G_IO_IN | G_IO_PRI |
                                    G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                                    channel_watch_func, reader);
    milter_debug("[%u] [reader][watch] %u", priv->tag, priv->channel_watch_id);
}

static void
dispose (GObject *object)
{
    MilterReaderPrivate *priv;

    priv = MILTER_READER_GET_PRIVATE(object);

    milter_debug("[%u] [reader][dispose]", priv->tag);

    clear_watch_id(priv);

    if (priv->io_channel) {
        g_io_channel_unref(priv->io_channel);
        priv->io_channel = NULL;
    }

    if (priv->loop) {
        g_object_unref(priv->loop);
        priv->loop = NULL;
    }

    G_OBJECT_CLASS(milter_reader_parent_class)->dispose(object);
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    MilterReaderPrivate *priv;

    priv = MILTER_READER_GET_PRIVATE(object);
    switch (prop_id) {
    case PROP_IO_CHANNEL:
        priv->io_channel = g_value_get_pointer(value);
        if (priv->io_channel)
            g_io_channel_ref(priv->io_channel);
        break;
    case PROP_TAG:
        milter_reader_set_tag(MILTER_READER(object), g_value_get_uint(value));
        break;
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
    MilterReaderPrivate *priv;

    priv = MILTER_READER_GET_PRIVATE(object);
    switch (prop_id) {
    case PROP_IO_CHANNEL:
        g_value_set_pointer(value, priv->io_channel);
        break;
    case PROP_TAG:
        g_value_set_uint(value, priv->tag);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

GQuark
milter_reader_error_quark (void)
{
    return g_quark_from_static_string("milter-reader-error-quark");
}

MilterReader *
milter_reader_io_channel_new (GIOChannel *channel)
{
    return g_object_new(MILTER_TYPE_READER,
                        "io-channel", channel,
                        NULL);
}

void
milter_reader_start (MilterReader *reader, MilterEventLoop *loop)
{
    MilterReaderPrivate *priv;

    priv = MILTER_READER_GET_PRIVATE(reader);
    if (priv->io_channel && priv->channel_watch_id == 0)
        watch_io_channel(reader, loop);
}

gboolean
milter_reader_is_watching (MilterReader *reader)
{
    return MILTER_READER_GET_PRIVATE(reader)->channel_watch_id > 0;
}

void
milter_reader_shutdown (MilterReader *reader)
{
    MilterReaderPrivate *priv;
    GError *channel_error = NULL;

    priv = MILTER_READER_GET_PRIVATE(reader);

    if (priv->channel_watch_id == 0)
        return;

    if (priv->shutdown_requested)
        return;
    priv->shutdown_requested = TRUE;

    if (priv->processing)
        return;

    milter_debug("[%u] [reader][shutdown]", priv->tag);
    g_io_channel_shutdown(priv->io_channel, TRUE, &channel_error);
    if (channel_error) {
        GError *error = NULL;

        milter_utils_set_error_with_sub_error(
            &error,
            MILTER_READER_ERROR,
            MILTER_READER_ERROR_IO_ERROR,
            channel_error,
            "failed to shutdown");
        milter_error("[%u] [reader][error][shutdown] %s",
                     priv->tag, error->message);
        milter_error_emittable_emit(MILTER_ERROR_EMITTABLE(reader), error);
        g_error_free(error);
    }
}

guint
milter_reader_get_tag (MilterReader *reader)
{
    return MILTER_READER_GET_PRIVATE(reader)->tag;
}

void
milter_reader_set_tag (MilterReader *reader, guint tag)
{
    MilterReaderPrivate *priv;

    priv = MILTER_READER_GET_PRIVATE(reader);
    priv->tag = tag;
}

/*
vi:ts=4:nowrap:ai:expandtab:sw=4
*/
