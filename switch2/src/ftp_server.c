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
 * The FTP server (ftpvita.h): ftpd's server (switch2/third_party/ftpd) on a
 * thread of its own.
 *
 * ftpd never waits in a socket call: each ftp_loop() checks the listening
 * socket and every connection without waiting and handles what is ready,
 * so one thread serves all connections. The thread sleeps for a
 * millisecond between loops. ftpd initializes the socket driver itself
 * and closes it in ftp_exit().
 *
 * ftpd prints its progress to a console on the screen; OunceShell has none,
 * so the console functions it calls do nothing here.
 */

#include <ftpvita.h>

#include "console.h"
#include "ftp.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <stdatomic.h>

/* How long the server thread sleeps between loops. */
#define LOOP_INTERVAL_NS 1000000L

/* How long the thread waits before it starts the server again, after the
   network went down. */
#define RESTART_INTERVAL_S 1

static pthread_t server_thread;
static int server_thread_started;
static atomic_int stop_requested;
static atomic_int serving;
static atomic_int recovering;
static _Atomic uint64_t started_at;
static uint64_t monotonic_seconds(void) { struct timespec t; return clock_gettime(CLOCK_MONOTONIC, &t) == 0 ? (uint64_t)t.tv_sec : 0; }
int ftpvita_is_recovering(void) { return atomic_load(&recovering); }
unsigned ftpvita_uptime(void) { uint64_t now = monotonic_seconds(), start = atomic_load(&started_at); return now >= start && start ? (unsigned)(now-start) : 0; }
static atomic_uint clients;
static _Atomic uint64_t uploaded, downloaded;
void ftpvita_metrics_client(int delta) { if(delta > 0) atomic_fetch_add(&clients, 1); else atomic_fetch_sub(&clients, 1); }
void ftpvita_metrics_bytes(uint64_t bytes, int upload) { if(upload) atomic_fetch_add(&uploaded, bytes); else atomic_fetch_add(&downloaded, bytes); }
void ftpvita_metrics_snapshot(unsigned *count, uint64_t *up, uint64_t *down) { *count = atomic_load(&clients); *up = atomic_load(&uploaded); *down = atomic_load(&downloaded); }

/* Bounded diagnostic log; omit command arguments and raw FTP replies. */
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static void log_event(const char *format, ...) {
  int saved_errno = errno;
  pthread_mutex_lock(&log_lock);
  const char *path = "sdmc:/switch/ounceshell-pro/ftp-diagnostics.log";
  FILE *fp = fopen(path, "a+");
  if (fp) {
    if (fseek(fp, 0, SEEK_END) == 0 && ftell(fp) >= 512 * 1024) {
      fclose(fp);
      fp = fopen(path, "w");
    }
    if (fp) {
      fprintf(fp, "[%lld] ", (long long)time(NULL));
      va_list args;
      va_start(args, format);
      vfprintf(fp, format, args);
      va_end(args);
      fputc('\n', fp);
      fclose(fp);
    }
  }
  pthread_mutex_unlock(&log_lock);
  errno = saved_errno;
}

static void sleepFor(time_t seconds, long nanoseconds) {
  struct timespec interval = { .tv_sec = seconds, .tv_nsec = nanoseconds };
  nanosleep(&interval, NULL);
}

static void *serve(void *argument) {
  (void)argument;

  int initialized = 1;
  while (!stop_requested) {
    loop_status_t status = ftp_loop();

    if (status == LOOP_EXIT) {
      log_event("FTP loop exit");
      break;
    }

    if (status == LOOP_RESTART) {
      log_event("Network restart requested");
      // The network went down: start again once it is back.
      recovering = 1;
      ftp_exit();
      initialized = 0;
      unsigned delay = RESTART_INTERVAL_S;
      while (!stop_requested) {
        if (ftp_init() == 0) { initialized = 1; break; }
        log_event("FTP restart failed; retrying");
        for (unsigned tick = 0; tick < delay * 10 && !stop_requested; ++tick) sleepFor(0, 100000000L);
        if (delay < 8) delay *= 2;
      }
      recovering = 0;
      if (stop_requested)
        break;
      log_event("FTP restarted");
      continue;
    }

    sleepFor(0, LOOP_INTERVAL_NS);
  }

  if (initialized) ftp_exit();
  recovering = 0;
  serving = 0;
  log_event("FTP thread stopped");
  return NULL;
}

static void joinServerThread(void) {
  if (!server_thread_started)
    return;

  stop_requested = 1;
  pthread_join(server_thread, NULL);
  server_thread_started = 0;
}

int ftpvita_init(char *vita_ip, unsigned short int *vita_port) {
  if (ftpvita_is_initialized())
    return -1;

  // A server that stopped by itself still has its thread to join.
  joinServerThread();

  // The network state comes from nifm, which stays open while the server
  // may run, as with libftpvita. VitaShell's wait dialog also reads it.
  if (sceNetCtlInit() < 0)
    return -1;

  int state = SCE_NETCTL_STATE_DISCONNECTED;
  sceNetCtlInetGetState(&state);
  if (state != SCE_NETCTL_STATE_CONNECTED)
    return -1;

  SceNetCtlInfo info;
  if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0)
    return -1;

  if (ftp_init() != 0)
    return -1;

  atomic_store(&clients, 0);
  atomic_store(&uploaded, 0);
  atomic_store(&downloaded, 0);
  recovering = 0;
  atomic_store(&started_at, monotonic_seconds());
  stop_requested = 0;
  serving = 1;
  if (pthread_create(&server_thread, NULL, serve, NULL) != 0) {
    serving = 0;
    ftp_exit();
    return -1;
  }
  server_thread_started = 1;

  snprintf(vita_ip, 16, "%s", info.ip_address);
  *vita_port = FTP_LISTEN_PORT;
  log_event("FTP started on port %u", (unsigned)*vita_port);
  return 0;
}

void ftpvita_fini(void) {
  joinServerThread();
  sceNetCtlTerm();
}

int ftpvita_is_initialized(void) {
  return server_thread_started && serving;
}

/* ftpd's console (console.h). */

void console_init(void) {
}

void console_set_status(const char *format, ...) {
  (void)format;
}

void console_print(const char *format, ...) {
  /* Log only known event formats; never log raw commands or replies. */
  if (strstr(format, "accepted connection") || strstr(format, "closing connection") ||
      strstr(format, "connected to") || strstr(format, "revents=") ||
      strstr(format, "errno") || strstr(format, ": %d %s") ||
      strstr(format, "only sent") || strstr(format, "failed to allocate")) {
    /* Format strings with file paths are excluded by the allowlist below. */
    if (strstr(format, "'%s'") || strstr(format, "%s %s")) return;
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log_event("%s", line);
  }
}

void debug_print(const char *format, ...) {
  if (strstr(format, "peer closed") || strstr(format, "disconnected from peer") || strstr(format, "exiting ftp"))
    log_event("%s", format);
}

void console_render(void) {
}
