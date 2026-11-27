/*
 * Copyright (C) 2026 by volte.io
 *
 * This file is part of Open5GS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "ogs-app.h"

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define LI_FEED_RETRY           ogs_time_from_sec(2)
/* A sidecar that does not read is cut off instead of growing this without end. */
#define LI_FEED_MAX_BACKLOG     (64*1024*1024)

static struct {
    char *path;
    uint8_t source;
    ogs_li_feed_connected_f connected;
    ogs_li_feed_command_f command;
    void (*disconnected)(void);

    /* What the sidecar sent and is not a whole command yet. */
    uint8_t in[512];
    size_t in_len;

    int fd;
    ogs_poll_t *poll_in;
    ogs_poll_t *poll_out;
    ogs_timer_t *timer;

    /* Unsent octets are buf[off..len); a message being built starts at msg. */
    uint8_t *buf;
    size_t len, off, cap;
    size_t msg;
    bool building;
} self = { .fd = -1 };

static void li_feed_connect(void);
static void li_feed_flush(void);

static void li_feed_disconnect(void)
{
    if (self.poll_in) ogs_pollset_remove(self.poll_in);
    if (self.poll_out) ogs_pollset_remove(self.poll_out);
    self.poll_in = self.poll_out = NULL;
    if (self.fd >= 0) close(self.fd);
    self.fd = -1;
    self.len = self.off = 0;
    self.in_len = 0;
    self.building = false;
    if (self.timer) ogs_timer_start(self.timer, LI_FEED_RETRY);
    if (self.disconnected)
        self.disconnected();
}

static void li_feed_readable(short when, ogs_socket_t fd, void *data)
{
    ssize_t n;
    size_t off = 0;

    n = read(fd, self.in + self.in_len, sizeof(self.in) - self.in_len);
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
        return;
    if (n <= 0) {
        ogs_warn("LI feed: the sidecar closed the connection");
        li_feed_disconnect();
        return;
    }
    self.in_len += n;

    while (self.fd >= 0 && self.in_len - off >= 4) {
        uint32_t len;
        const uint8_t *msg = self.in + off + 4;

        memcpy(&len, self.in + off, 4);
        len = be32toh(len);
        if (len < 2 || len > sizeof(self.in) - 4) {
            ogs_error("LI feed: malformed command, closing");
            li_feed_disconnect();
            return;
        }
        if (self.in_len - off - 4 < len)
            break;
        if (msg[0] == OGS_LI_FEED_VERSION && self.command)
            self.command(msg[1], msg + 2, len - 2);
        off += 4 + len;
    }
    if (self.fd < 0)
        return;
    memmove(self.in, self.in + off, self.in_len - off);
    self.in_len -= off;
}

static void li_feed_writable(short when, ogs_socket_t fd, void *data)
{
    li_feed_flush();
}

static void li_feed_flush(void)
{
    while (self.fd >= 0 && self.off < self.len) {
        ssize_t n = send(self.fd,
                self.buf + self.off, self.len - self.off, MSG_NOSIGNAL);
        if (n > 0) {
            self.off += n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && errno == EAGAIN) {
            break;
        } else {
            ogs_warn("LI feed: send failed");
            li_feed_disconnect();
            return;
        }
    }
    if (self.fd < 0)
        return;

    if (self.off == self.len) {
        self.off = self.len = 0;
        if (self.poll_out) {
            ogs_pollset_remove(self.poll_out);
            self.poll_out = NULL;
        }
    } else if (!self.poll_out) {
        self.poll_out = ogs_pollset_add(ogs_app()->pollset,
                OGS_POLLOUT, self.fd, li_feed_writable, NULL);
        ogs_assert(self.poll_out);
    }
}

static void li_feed_timer(void *data)
{
    li_feed_connect();
}

static void li_feed_connect(void)
{
    struct sockaddr_un addr;
    int fd;

    ogs_assert(self.fd < 0);

    fd = socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
    ogs_assert(fd >= 0);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    ogs_cpystrn(addr.sun_path, self.path, sizeof(addr.sun_path));

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        ogs_timer_start(self.timer, LI_FEED_RETRY);
        return;
    }

    self.fd = fd;
    self.poll_in = ogs_pollset_add(ogs_app()->pollset,
            OGS_POLLIN, fd, li_feed_readable, NULL);
    ogs_assert(self.poll_in);
    ogs_info("LI feed: connected");

    ogs_li_feed_begin(OGS_LI_FEED_EVENT_SYNC_BEGIN);
    ogs_li_feed_end();
    if (self.connected)
        self.connected();
    ogs_li_feed_begin(OGS_LI_FEED_EVENT_SYNC_END);
    ogs_li_feed_end();
}

int ogs_li_feed_open(
        const char *path, uint8_t source, ogs_li_feed_connected_f connected,
        ogs_li_feed_command_f command, void (*disconnected)(void))
{
    struct sockaddr_un addr;

    ogs_assert(path);
    ogs_assert(!self.path);

    if (strlen(path) >= sizeof(addr.sun_path)) {
        ogs_error("LI feed: socket path too long");
        return OGS_ERROR;
    }

    self.path = ogs_strdup(path);
    ogs_assert(self.path);
    self.source = source;
    self.connected = connected;
    self.command = command;
    self.disconnected = disconnected;
    self.timer = ogs_timer_add(ogs_app()->timer_mgr, li_feed_timer, NULL);
    ogs_assert(self.timer);
    /* The first connect runs in the event loop, as every later one does. */
    ogs_timer_start(self.timer, 1);

    return OGS_OK;
}

void ogs_li_feed_close(void)
{
    if (!self.path)
        return;

    if (self.timer) ogs_timer_delete(self.timer);
    self.timer = NULL;
    self.disconnected = NULL;
    li_feed_disconnect();
    free(self.buf);
    self.buf = NULL;
    self.cap = 0;
    ogs_free(self.path);
    self.path = NULL;
}

bool ogs_li_feed_connected(void)
{
    return self.fd >= 0;
}

size_t ogs_li_feed_backlog(void)
{
    return self.len - self.off;
}

const uint8_t *ogs_li_feed_find(
        const uint8_t *elements, size_t len, uint8_t type, size_t *value_len)
{
    size_t off = 0;

    while (off + 3 <= len) {
        size_t n = (elements[off + 1] << 8) | elements[off + 2];
        if (off + 3 + n > len)
            break;
        if (elements[off] == type) {
            *value_len = n;
            return elements + off + 3;
        }
        off += 3 + n;
    }
    return NULL;
}

static uint8_t *li_feed_reserve(size_t n)
{
    uint8_t *p;

    if (self.len + n > self.cap) {
        size_t cap = self.cap ? self.cap : 4096;
        while (cap < self.len + n)
            cap *= 2;
        p = realloc(self.buf, cap);
        ogs_assert(p);
        self.buf = p;
        self.cap = cap;
    }
    p = self.buf + self.len;
    self.len += n;
    return p;
}

void ogs_li_feed_begin(uint8_t event)
{
    uint8_t *p;

    ogs_assert(!self.building);
    if (self.fd < 0)
        return;

    self.building = true;
    self.msg = self.len;
    p = li_feed_reserve(7);
    memset(p, 0, 4);
    p[4] = OGS_LI_FEED_VERSION;
    p[5] = self.source;
    p[6] = event;
}

void ogs_li_feed_put(uint8_t type, const void *value, size_t len)
{
    uint8_t *p;

    if (!self.building)
        return;
    ogs_assert(len <= 0xffff);

    p = li_feed_reserve(3 + len);
    p[0] = type;
    p[1] = len >> 8;
    p[2] = len & 0xff;
    if (len)
        memcpy(p + 3, value, len);
}

void ogs_li_feed_put_str(uint8_t type, const char *value)
{
    if (value && value[0])
        ogs_li_feed_put(type, value, strlen(value));
}

void ogs_li_feed_put_u8(uint8_t type, uint8_t value)
{
    ogs_li_feed_put(type, &value, 1);
}

void ogs_li_feed_put_u32(uint8_t type, uint32_t value)
{
    value = htobe32(value);
    ogs_li_feed_put(type, &value, 4);
}

void ogs_li_feed_put_u64(uint8_t type, uint64_t value)
{
    value = htobe64(value);
    ogs_li_feed_put(type, &value, 8);
}

void ogs_li_feed_end(void)
{
    uint32_t n;

    if (!self.building)
        return;
    self.building = false;

    n = htobe32(self.len - self.msg - 4);
    memcpy(self.buf + self.msg, &n, 4);

    if (self.len - self.off > LI_FEED_MAX_BACKLOG) {
        ogs_error("LI feed: the sidecar does not read, closing");
        li_feed_disconnect();
        return;
    }
    /* During the sync after a connect, send in one go at its end. */
    li_feed_flush();
}
