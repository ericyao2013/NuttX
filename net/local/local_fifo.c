/****************************************************************************
 * net/local/local_fifo.c
 *
 *   Copyright (C) 2015 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#if defined(CONFIG_NET) && defined(CONFIG_NET_LOCAL)

#include <sys/stat.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "local/local.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#define LOCAL_RX_SUFFIX    "RX"
#define LOCAL_TX_SUFFIX    "TX"
#define LOCAL_SUFFIX_LEN   2

#define LOCAL_FULLPATH_LEN (UNIX_PATH_MAX + LOCAL_SUFFIX_LEN)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: local_rx_name
 *
 * Description:
 *   Create the name of the RX (client-to-server) FIFO name.
 *
 ****************************************************************************/

static inline void local_rx_name(FAR struct local_conn_s *conn,
                                 FAR char *path)
{
  (void)snprintf(path, LOCAL_FULLPATH_LEN-1, "%s" LOCAL_RX_SUFFIX,
                 conn->lc_path);
   path[LOCAL_FULLPATH_LEN-1] = '\0';
}

/****************************************************************************
 * Name: local_tx_name
 *
 * Description:
 *   Create the name of the TX (server-to-client) FIFO name.
 *
 ****************************************************************************/

static inline void local_tx_name(FAR struct local_conn_s *conn,
                                 FAR char *path)
{
  (void)snprintf(path, LOCAL_FULLPATH_LEN-1, "%s" LOCAL_TX_SUFFIX,
                 conn->lc_path);
   path[LOCAL_FULLPATH_LEN-1] = '\0';
}

/****************************************************************************
 * Name: local_fifo_exists
 *
 * Description:
 *   Check if a FIFO exists.
 *
 ****************************************************************************/

static bool local_fifo_exists(FAR const char *path)
{
  struct stat buf;
  int ret;

  /* Create the client-to-server FIFO */

  ret = stat(path, &buf);
  if (ret < 0)
    {
      return false;
    }

  /* FIFOs are character devices in NuttX.  Return true if what we found
   * is a FIFO.  What if it is something else?  In that case, we will
   * return false and mkfifo() will fail.
   */

  return (bool)S_ISCHR(buf.st_mode);
}

/****************************************************************************
 * Name: local_create_fifo
 *
 * Description:
 *   Create the one of FIFOs needed for a connection.
 *
 ****************************************************************************/

static int local_create_fifo(FAR const char *path)
{
  int ret;

  /* Create the client-to-server FIFO if it does not already exist. */

  if (!local_fifo_exists(path))
    {
      ret = mkfifo(path, 0644);
      if (ret < 0)
        {
          int errcode = errno;
          DEBUGASSERT(errcode > 0);

          ndbg("ERROR: Failed to create FIFO %s: %d\n", path, errcode);
          return -errcode;
        }
    }

  /* The FIFO (or some character driver) exists at PATH or we successfully
   * created the FIFO at that location.
   */

  return OK;
}

/****************************************************************************
 * Name: local_destroy_fifo
 *
 * Description:
 *   Destroy one of the FIFOs used in a connection.
 *
 ****************************************************************************/

static int local_destroy_fifo(FAR const char *path)
{
  int ret;

  /* Unlink the client-to-server FIFO if it exists. */

  if (local_fifo_exists(path))
    {
      ret = unlink(path);
      if (ret < 0)
        {
          int errcode = errno;
          DEBUGASSERT(errcode > 0);

          ndbg("ERROR: Failed to unlink FIFO %s: %d\n", path, errcode);
          return -errcode;
        }
    }

  /* The FIFO does not exist or we successfully unlinked it. */

  return OK;
}

/****************************************************************************
 * Name: local_rx_open
 *
 * Description:
 *   Open a FIFO for read-only access.
 *
 ****************************************************************************/

static inline int local_rx_open(FAR struct local_conn_s *conn,
                                FAR const char *path)
{
  conn->lc_infd = open(path, O_RDONLY);
  if (conn->lc_infd < 0)
    {
      int errcode = errno;
      DEBUGASSERT(errcode > 0);

      ndbg("ERROR: Failed on open %s for reading: %d\n",
           path, errcode);

      /* Map the errcode to something consistent with the return
       * error codes from connect():
       *
       * If errcode is ENOENT, meaning that the FIFO does exist,
       * return EFAULT meaning that the socket structure address is
       * outside the user's address space.
       */

      return errcode == ENOENT ? -EFAULT : -errcode;
    }

  return OK;
}

/****************************************************************************
 * Name: local_tx_open
 *
 * Description:
 *   Open a FIFO for write-only access.
 *
 ****************************************************************************/

static inline int local_tx_open(FAR struct local_conn_s *conn,
                                FAR const char *path)
{
  conn->lc_outfd = open(path, O_WRONLY);
  if (conn->lc_outfd < 0)
    {
      int errcode = errno;
      DEBUGASSERT(errcode > 0);

      ndbg("ERROR: Failed on open %s for writing: %d\n",
           path, errcode);

      /* Map the errcode to something consistent with the return
       * error codes from connect():
       *
       * If errcode is ENOENT, meaning that the FIFO does exist,
       * return EFAULT meaning that the socket structure address is
       * outside the user's address space.
       */

      return errcode == ENOENT ? -EFAULT : -errcode;
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: local_create_fifos
 *
 * Description:
 *   Create the FIFO pair needed for a connection.
 *
 ****************************************************************************/

int local_create_fifos(FAR struct local_conn_s *client)
{
  char path[LOCAL_FULLPATH_LEN];
  int ret;

  /* Create the client-to-server FIFO if it does not already exist. */

  local_tx_name(client, path);
  ret = local_create_fifo(path);
  if (ret >= 0)
    {
      /* Create the server-to-client FIFO if it does not already exist. */

      local_rx_name(client, path);
      ret = local_create_fifo(path);
    }

  return ret;
}

/****************************************************************************
 * Name: local_destroy_fifos
 *
 * Description:
 *   Destroy the FIFO pair used for a connection.
 *
 ****************************************************************************/

int local_destroy_fifos(FAR struct local_conn_s *client)
{
  char path[LOCAL_FULLPATH_LEN];
  int ret1;
  int ret2;

  /* Destroy the client-to-server FIFO if it exists. */

  local_tx_name(client, path);
  ret1 = local_destroy_fifo(path);

  /* Destroy the server-to-client FIFO if it exists. */

  local_rx_name(client, path);
  ret2 = local_create_fifo(path);

  /* Return a failure if one occurred. */

  return ret1 < 0 ? ret1 : ret2;
}

/****************************************************************************
 * Name: local_open_client_rx
 *
 * Description:
 *   Only the client-side Rx FIFO.
 *
 ****************************************************************************/

int local_open_client_rx(FAR struct local_conn_s *client)
{
  char path[LOCAL_FULLPATH_LEN];

  /* Get the server-to-client path name */

  local_tx_name(client, path);

  /* Then open the file for read-only access */

  return local_rx_open(client, path);
}

/****************************************************************************
 * Name: local_open_client_tx
 *
 * Description:
 *   Only the client-side Tx FIFO.
 *
 ****************************************************************************/

int local_open_client_tx(FAR struct local_conn_s *client)
{
  char path[LOCAL_FULLPATH_LEN];

  /* Get the client-to-server path name */

  local_rx_name(client, path);

  /* Then open the file for write-only access */

  return local_tx_open(client, path);
}

/****************************************************************************
 * Name: local_open_server_rx
 *
 * Description:
 *   Only the server-side Rx FIFO.
 *
 ****************************************************************************/

int local_open_server_rx(FAR struct local_conn_s *server)
{
  char path[LOCAL_FULLPATH_LEN];

  /* Get the client-to-server path name */

  local_rx_name(server, path);

  /* Then open the file for write-only access */

  return local_rx_open(server, path);
}

/****************************************************************************
 * Name: local_open_server_tx
 *
 * Description:
 *   Only the server-side Tx FIFO.
 *
 ****************************************************************************/

int local_open_server_tx(FAR struct local_conn_s *server)
{
  char path[LOCAL_FULLPATH_LEN];

  /* Get the server-to-client path name */

  local_tx_name(server, path);

  /* Then open the file for read-only access */

  return local_tx_open(server, path);
}
#endif /* CONFIG_NET && CONFIG_NET_LOCAL */
