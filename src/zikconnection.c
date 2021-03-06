/* Zik2ctl
 * Copyright (C) 2015 Aurélien Zanelli <aurelien.zanelli@darkosphere.fr>
 *
 * Zik2ctl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Zik2ctl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Zik2ctl. If not, see <http://www.gnu.org/licenses/>.
 */

#include <gio/gio.h>

#include "zikconnection.h"

struct _ZikConnection
{
  gint ref_count;

  GSocket *socket;
  guint8 *recv_buffer;
  gsize recv_buffer_size;
};

G_DEFINE_BOXED_TYPE (ZikConnection, zik_connection, zik_connection_ref,
    zik_connection_unref);

ZikConnection *
zik_connection_new (int fd)
{
  ZikConnection *conn;
  GError *error = NULL;

  conn = g_slice_new0 (ZikConnection);
  conn->ref_count = 1;
  conn->socket = g_socket_new_from_fd (fd, &error);

  if (conn->socket == NULL) {
    g_critical ("failed to create socket from fd %d: %s", fd, error->message);
    g_error_free (error);
    g_slice_free (ZikConnection, conn);
    return NULL;
  }

  /* message size is stored to an uint16_t so make receive buffer accordingly */
  conn->recv_buffer_size = G_MAXUINT16;
  conn->recv_buffer = g_malloc (conn->recv_buffer_size);

  return conn;
}

ZikConnection *
zik_connection_ref (ZikConnection * conn)
{
  g_atomic_int_inc (&conn->ref_count);
  return conn;
}

void
zik_connection_unref (ZikConnection * conn)
{
  if (g_atomic_int_dec_and_test (&conn->ref_count)) {
    if (conn->socket)
      g_object_unref (conn->socket);

    g_free (conn->recv_buffer);
    g_slice_free (ZikConnection, conn);
  }
}

gboolean
zik_connection_open_session (ZikConnection * conn)
{
  ZikMessage *msg;
  gboolean ret;

  msg = zik_message_new_open_session ();
  ret = zik_connection_send_message (conn, msg, NULL);
  zik_message_free (msg);

  return ret;
}

gboolean
zik_connection_close_session (ZikConnection * conn)
{
  ZikMessage *msg;
  gboolean ret;

  msg = zik_message_new_close_session ();
  ret = zik_connection_send_message (conn, msg, NULL);
  zik_message_free (msg);

  return ret;
}

gboolean
zik_connection_send_message (ZikConnection * conn, ZikMessage * msg,
    ZikMessage ** out_answer)
{
  gboolean ret = FALSE;
  GError *error = NULL;
  guint8 *data;
  gsize size;
  gssize sbytes, rbytes;
  ZikMessage *answer;

  data = zik_message_make_buffer (msg, &size);

  /* send data */
  sbytes = g_socket_send (conn->socket, (gchar *) data, size, NULL, &error);
  if (sbytes < 0) {
    g_critical ("ZikConnection %p: failed to send data to socket: %s",
        conn, error->message);
    g_error_free (error);
    goto done;
  } else if ((gsize) sbytes < size) {
    g_warning ("ZikConnection %p: failed to send all data: %" G_GSSIZE_FORMAT
        "/%" G_GSIZE_FORMAT, conn, sbytes, size);
    goto done;
  }

  /* wait for answer */
  rbytes = g_socket_receive_with_blocking (conn->socket,
      (gchar *) conn->recv_buffer, conn->recv_buffer_size, TRUE, NULL, &error);
  if (rbytes < 0) {
    g_critical ("ZikConnection %p: failed to receive data from socket: %s",
        conn, error->message);
    g_error_free (error);
    goto done;
  } else if (rbytes == 0) {
    g_warning ("ZikConnection %p: connection was closed while receiving",
        conn);
    goto done;
  } else if (rbytes < 3) {
    g_warning ("ZikConnection %p: not enough data in answer: %" G_GSSIZE_FORMAT,
        conn, rbytes);
  }

  answer = zik_message_new_from_buffer (conn->recv_buffer, rbytes);
  if (answer == NULL) {
    g_warning ("ZikConnection %p: failed to make message from received buffer",
        conn);
    goto done;
  }

  /* depending on the sent message, it could be an ack or a request answer */
  if (!zik_message_is_acknowledge (answer) &&
      !zik_message_is_request (answer)) {
    g_warning ("ZikConnection %p: bad answer %02x %02x %02x", conn,
        conn->recv_buffer[0], conn->recv_buffer[1], conn->recv_buffer[2]);
    zik_message_free (answer);
    goto done;
  }

  if (out_answer != NULL)
    *out_answer = answer;
  else
    zik_message_free (answer);

  ret = TRUE;

done:
  g_free (data);
  return ret;
}
