/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright © 2016 Kinvolk GmbH
 * Copyright © 2017 Endless Mobile, Inc.
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
 * Authors:
 *  - Krzesimir Nowak <krzesimir@kinvolk.io>
 *  - Philip Withnall <withnall@endlessm.com>
 */

#pragma once

#include <gio/gio.h>
#include <glib.h>

G_BEGIN_DECLS

extern const gchar * const EOS_UPDATER_AVAHI_SERVICE_TYPE;
extern const gchar * const eos_avahi_v1_ostree_path;
extern const gchar * const eos_avahi_v1_head_commit_timestamp;

const gchar *eos_avahi_service_file_get_directory (void);

gboolean eos_avahi_service_file_generate (const gchar   *avahi_service_directory,
                                          const gchar   *ostree_path,
                                          GDateTime     *head_commit_timestamp,
                                          GCancellable  *cancellable,
                                          GError       **error);
gboolean eos_avahi_service_file_delete (const gchar   *avahi_service_directory,
                                        GCancellable  *cancellable,
                                        GError       **error);

G_END_DECLS
