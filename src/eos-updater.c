/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright © 2016 Endless Mobile
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Vivek Dasmohapatra <vivek@etla.org>
 */

#include "eos-updater-apply.h"
#include "eos-updater-data.h"
#include "eos-updater-fetch.h"
#include "eos-updater-live-boot.h"
#include "eos-updater-generated.h"
#include "eos-updater-object.h"
#include "eos-updater-poll.h"
#include "eos-updater-poll-volume-dbus.h"

#include <libeos-updater-util/util.h>

#include <errno.h>
#include <locale.h>
#include <ostree.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>

typedef struct
{
  EosQuitFile *quit_file;
  GDBusObjectManagerServer *manager;
  EosUpdater *updater;

  GMainLoop *loop;
  EosUpdaterData *data;
} LocalData;

#define LOCAL_DATA_CLEARED { NULL, NULL, NULL, NULL, NULL }

static void
local_data_clear (LocalData *local_data)
{
  g_clear_object (&local_data->quit_file);
  g_clear_object (&local_data->updater);
  g_clear_object (&local_data->manager);
  g_clear_pointer (&local_data->loop, g_main_loop_unref);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (LocalData, local_data_clear)

static void
local_data_init (LocalData *local_data,
                 EosUpdaterData *data,
                 GMainLoop *loop)
{
  local_data->data = data;
  local_data->loop = g_main_loop_ref (loop);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  EosObjectSkeleton *object = NULL;
  EosUpdater *updater = NULL;
  LocalData *local_data = user_data;
  GError *error = NULL;

  g_autofree gchar *src = NULL;
  g_autofree gchar *ref = NULL;
  g_autofree gchar *sum = NULL;

  message ("Acquired a message bus connection");

  /* Create a new org.freedesktop.DBus.ObjectManager rooted at /com/endlessm */
  local_data->manager = g_dbus_object_manager_server_new ("/com/endlessm");
  object  = eos_object_skeleton_new ("/com/endlessm/Updater");

  /* Make the newly created object export the interface com.endlessm.Updater
     (note that @skeleton takes its own reference to @updater). */
  local_data->updater = eos_updater_skeleton_new ();
  updater = local_data->updater;
  eos_object_skeleton_set_updater (object, updater);

  sum = eos_updater_get_booted_checksum (&error);
  if (sum != NULL)
    {
      eos_updater_set_current_id (updater, sum);
      eos_updater_set_download_size (updater, 0);
      eos_updater_set_downloaded_bytes (updater, 0);
      eos_updater_set_unpacked_size (updater, 0);
      eos_updater_set_update_id (updater, "");
      eos_updater_clear_error (updater, EOS_UPDATER_STATE_READY);
    }
  else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      g_clear_error (&error);
      g_set_error (&error, EOS_UPDATER_ERROR,
                   EOS_UPDATER_ERROR_NOT_OSTREE_SYSTEM,
                   "Not an OSTree-based system: cannot update it.");
      eos_updater_set_error (updater, error);
      g_clear_error (&error);
    }
  else
    {
      eos_updater_set_error (updater, error);
      g_clear_error (&error);
    }

  if (!is_installed_system (&error))
    {
      /* Disable updates on live USBs: */
      g_signal_connect (updater, "handle-fetch", G_CALLBACK (handle_on_live_boot), local_data->data);
      g_signal_connect (updater, "handle-poll",  G_CALLBACK (handle_on_live_boot), local_data->data);
      g_signal_connect (updater, "handle-poll-volume",
                        G_CALLBACK (handle_on_live_boot), local_data->data);
      g_signal_connect (updater, "handle-apply", G_CALLBACK (handle_on_live_boot), local_data->data);

      eos_updater_set_error (updater, error);
      g_clear_error (&error);
    }
  else
    {
      /* Handle the various DBus methods: */
      g_signal_connect (updater, "handle-fetch", G_CALLBACK (handle_fetch), local_data->data);
      g_signal_connect (updater, "handle-poll",  G_CALLBACK (handle_poll), local_data->data);
      g_signal_connect (updater, "handle-poll-volume",
                        G_CALLBACK (handle_poll_volume), local_data->data);
      g_signal_connect (updater, "handle-apply", G_CALLBACK (handle_apply), local_data->data);
    }

  /* Export the object (@manager takes its own reference to @object) */
  g_dbus_object_manager_server_export (local_data->manager, G_DBUS_OBJECT_SKELETON (object));
  g_object_unref (object);

  /* Export all objects */
  message ("Exporting objects");
  g_dbus_object_manager_server_set_connection (local_data->manager, connection);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  message ("Acquired the name %s", name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  LocalData *local_data = user_data;

  message ("Lost the name %s. Exiting.", name);
  g_main_loop_quit (local_data->loop);
}

static const gchar *
quit_file_name (void)
{
  return g_getenv ("EOS_UPDATER_TEST_UPDATER_QUIT_FILE");
}

static EosQuitFileCheckResult
check_and_quit (gpointer local_data_ptr)
{
  LocalData *local_data = local_data_ptr;
  EosUpdaterState state = eos_updater_get_state (local_data->updater);

  switch (state)
    {
    case EOS_UPDATER_STATE_NONE:
    case EOS_UPDATER_STATE_READY:
    case EOS_UPDATER_STATE_ERROR:
    case EOS_UPDATER_STATE_UPDATE_APPLIED:
      g_main_loop_quit (local_data->loop);
      return EOS_QUIT_FILE_QUIT;

    case EOS_UPDATER_STATE_POLLING:
    case EOS_UPDATER_STATE_UPDATE_AVAILABLE:
    case EOS_UPDATER_STATE_FETCHING:
    case EOS_UPDATER_STATE_UPDATE_READY:
    case EOS_UPDATER_STATE_APPLYING_UPDATE:
      return EOS_QUIT_FILE_KEEP_CHECKING;

    default:
      g_assert_not_reached ();
    }
}

static gboolean
maybe_setup_quit_file (LocalData *local_data,
                       GError **error)
{
  const gchar *filename = quit_file_name ();
  g_autoptr(EosQuitFile) quit_file = NULL;

  if (filename == NULL)
    return TRUE;

  quit_file = eos_updater_setup_quit_file (filename,
                                           check_and_quit,
                                           local_data,
                                           NULL,
                                           5,
                                           error);
  if (quit_file == NULL)
    return FALSE;

  local_data->quit_file = g_steal_pointer (&quit_file);
  return TRUE;
}

static gboolean
listen_on_session_bus (void)
{
  const gchar *value = NULL;

  value = g_getenv ("EOS_UPDATER_TEST_UPDATER_USE_SESSION_BUS");

  return value != NULL;
}

/* Remove our configuration files from /etc which are identical to the current
 * versions installed in /usr/share or /usr/etc, which we embed as an MD5
 * checksum. If we do this on all systems, we can eventually change the formats
 * in /usr/etc without worrying about the new defaults being overwritten by
 * stale files in /etc.
 *
 * This functionality can be removed after a few releases, once we’re confident
 * all systems will have been upgraded. */
static void
purge_old_config_file (const gchar *etc_path,
                       const gchar *checksum_to_delete)
{
  g_autofree guint8 *etc_contents = NULL;
  gsize etc_length;
  g_autoptr(GChecksum) checksum = NULL;
  g_autoptr(GError) error = NULL;

  g_file_get_contents (etc_path, (gchar **) &etc_contents, &etc_length, &error);
  if (g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
    {
      return;
    }
  else if (error != NULL)
    {
      g_warning ("Error reading ‘%s’ to update it: %s", etc_path,
                 error->message);
      return;
    }

  /* Work out its checksum. */
  checksum = g_checksum_new (G_CHECKSUM_MD5);
  g_checksum_update (checksum, etc_contents, etc_length);

  /* If the files are the same, delete the @etc_path. */
  if (g_strcmp0 (g_checksum_get_string (checksum), checksum_to_delete) == 0)
    {
      g_debug ("File ‘%s’ contains default settings. Deleting.", etc_path);

      if (g_unlink (etc_path) < 0)
        g_warning ("Error deleting ‘%s’: %s", etc_path, g_strerror (errno));
    }
  else
    {
      g_debug ("File ‘%s’ doesn’t contain default settings. Keeping it.",
               etc_path);
    }
}

static void
purge_old_config (void)
{
  /* Checksum from the file as of release 3.1.1. */
  purge_old_config_file (SYSCONFDIR "/dbus-1/system.d/com.endlessm.Updater.conf",
                         "cbaa5af44c70831f46122cd859424ec2");
  /* And this one. */
  purge_old_config_file (SYSCONFDIR "/eos-updater.conf",
                         "3693ff9b337a89ceec8b0630bd887d01");
}

gint
main (gint argc, gchar *argv[])
{
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(OstreeRepo) repo = NULL;
  g_auto(EosUpdaterData) data = EOS_UPDATER_DATA_CLEARED;
  g_auto(EosBusNameID) id = 0;
  g_autoptr(GError) error = NULL;
  g_auto(LocalData) local_data = LOCAL_DATA_CLEARED;
  GBusType bus_type = G_BUS_TYPE_SYSTEM;

  setlocale (LC_ALL, "");
  g_set_prgname (argv[0]);

  purge_old_config ();

  repo = eos_updater_local_repo ();
  eos_updater_data_init (&data, repo);
  loop = g_main_loop_new (NULL, FALSE);
  local_data_init (&local_data, &data, loop);
  if (listen_on_session_bus ())
    bus_type = G_BUS_TYPE_SESSION;
  id = g_bus_own_name (bus_type,
                       "com.endlessm.Updater",
                       G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT |
                       G_BUS_NAME_OWNER_FLAGS_REPLACE,
                       on_bus_acquired,
                       on_name_acquired,
                       on_name_lost,
                       &local_data,
                       NULL);

  if (!maybe_setup_quit_file (&local_data, &error))
    {
      message ("Failed to set up the quit file: %s", error->message);
      return 1;
    }
  g_main_loop_run (loop);

  return 0;
}
