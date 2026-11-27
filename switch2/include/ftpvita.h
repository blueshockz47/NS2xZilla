/*
  VitaShell for Switch 2
  Copyright (C) 2026, VitaShell Switch 2 port contributors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * The FTP server that SELECT (the - button) starts, under the names of
 * libftpvita's functions that VitaShell calls. On Switch 2 the server is
 * ftpd's (switch2/third_party/ftpd), run by switch2/src/ftp_server.c. It
 * serves the SD card at its root.
 */

#ifndef VITASHELL_SWITCH2_FTPVITA_H
#define VITASHELL_SWITCH2_FTPVITA_H

/* VitaShell reaches sceNetCtl through this header, as with libftpvita. */
#include <psp2/net/netctl.h>
#include <stdint.h>
void ftpvita_metrics_rates(uint64_t *up,uint64_t *down);
void ftpvita_metrics_client(int delta);
void ftpvita_metrics_bytes(uint64_t bytes, int upload);
void ftpvita_metrics_snapshot(unsigned *clients, uint64_t *up, uint64_t *down);
int ftpvita_is_recovering(void);
unsigned ftpvita_uptime(void);

/* Starts the server and writes its address and port. Returns 0, or a
   negative value when the console is not connected or the server could not
   start. */
int ftpvita_init(char *vita_ip, unsigned short int *vita_port);

/* Stops the server and closes its connections. */
void ftpvita_fini(void);

/* Returns 1 while the server runs, otherwise 0. */
int ftpvita_is_initialized(void);

#endif
