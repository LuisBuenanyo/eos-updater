/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2013 Collabora Ltd.
 * Copyright 2016 Kinvolk GmbH
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
 * Authors: Vivek Dasmohapatra <vivek@etla.org>
 *          Krzesimir Nowak <krzesimir@kinvolk.io>
 */

#include "eos-updater-poll-main.h"

#include <libeos-updater-util/util.h>

gboolean
metadata_fetch_from_main (EosMetadataFetchData *fetch_data,
                          GVariant *source_variant,
                          EosUpdateInfo **out_info,
                          GError **error)
{
  OstreeRepo *repo = fetch_data->data->repo;
  g_autofree gchar *refspec = NULL;
  g_autoptr(EosUpdateInfo) info = NULL;
  g_autofree gchar *checksum = NULL;
  g_autoptr(GVariant) commit = NULL;
  g_autofree gchar *remote = NULL;
  g_autofree gchar *ref = NULL;
  g_autoptr(EosExtensions) extensions = NULL;

  g_return_val_if_fail (out_info != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!get_booted_refspec (&refspec, &remote, &ref, error))
    return FALSE;

  if (!fetch_latest_commit (repo,
                            g_task_get_cancellable (fetch_data->task),
                            remote,
                            ref,
                            NULL,
                            &checksum,
                            &extensions,
                            error))
    return FALSE;

  if (!is_checksum_an_update (repo, checksum, &commit, error))
    return FALSE;

  if (commit != NULL)
    info = eos_update_info_new (checksum,
                                commit,
                                refspec,  /* for upgrade */
                                refspec,  /* original */
                                NULL,
                                extensions);

  *out_info = g_steal_pointer (&info);

  return TRUE;
}
