/*
 * Copyright © 2016 DENSO CORPORATION
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#include <waltham-object.h>
//#include <waltham-server.h> /* XXX: misses include guards */
#include <waltham-connection.h>
//#include <waltham-server-protocol.h>

#include "zalloc.h"
#include "w-util.h"

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({                              \
	const __typeof__( ((type *)0)->member ) *__mptr = (ptr);        \
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#define MAX_EPOLL_WATCHES 2

struct server;

struct watch {
	struct server *server;
	int fd;
	void (*cb)(struct watch *w, uint32_t events);
};

struct client {
	struct wl_list link; /* struct server::client_list */
	struct server *server;
	struct wth_connection *connection;
	struct watch conn_watch;

	/* TODO: a list of client objects, for clean-up */
};

struct server {
	int listen_fd;
	struct watch listen_watch;

	bool running;
	int epoll_fd;

	struct wl_list client_list;
};

static int
watch_ctl(struct watch *w, int op, uint32_t events)
{
	struct epoll_event ee;

	ee.events = events;
	ee.data.ptr = w;
	return epoll_ctl(w->server->epoll_fd, op, w->fd, &ee);
}

static void
client_destroy(struct client *c)
{
	fprintf(stderr, "Client %p disconnected.\n", c);

	wth_connection_flush(c->connection);
	wl_list_remove(&c->link);

	watch_ctl(&c->conn_watch, EPOLL_CTL_DEL, 0);
	wth_connection_destroy(c->connection);

	free(c);
}

static void
connection_handle_data(struct watch *w, uint32_t events)
{
	struct client *c = container_of(w, struct client, conn_watch);
	int ret;

	if (events & EPOLLERR) {
		fprintf(stderr, "Client %p errored out.\n", c);
		client_destroy(c);

		return;
	}

	if (events & EPOLLHUP) {
		fprintf(stderr, "Client %p hung up.\n", c);
		client_destroy(c);

		return;
	}

	if (events & EPOLLOUT) {
		ret = wth_connection_flush(c->connection);
		if (ret == 0)
			watch_ctl(&c->conn_watch, EPOLL_CTL_MOD, EPOLLIN);
		else if (ret < 0 && errno != EAGAIN){
			fprintf(stderr, "Client %p flush error.\n", c);
			client_destroy(c);

			return;
		}
	}

	if (events & EPOLLIN) {
		ret = wth_connection_read(c->connection);
		if (ret < 0) {
			fprintf(stderr, "Client %p read error.\n", c);
			client_destroy(c);

			return;
		}

		ret = wth_connection_dispatch(c->connection);
		if (ret < 0) {
			fprintf(stderr, "Client %p dispatch error.\n", c);
			client_destroy(c);

			return;
		}
	}
}

static struct client *
client_create(struct server *srv, struct wth_connection *conn)
{
	struct client *c;

	c = zalloc(sizeof *c);
	if (!c)
		return NULL;

	c->server = srv;
	c->connection = conn;

	c->conn_watch.server = srv;
	c->conn_watch.fd = wth_connection_get_fd(conn);
	c->conn_watch.cb = connection_handle_data;
	if (watch_ctl(&c->conn_watch, EPOLL_CTL_ADD, EPOLLIN) < 0) {
		free(c);
		return NULL;
	}

	fprintf(stderr, "Client %p connected.\n", c);

	wl_list_insert(&srv->client_list, &c->link);

	return c;
}

static void
server_flush_clients(struct server *srv)
{
	struct client *c, *tmp;
	int ret;

	wl_list_for_each_safe(c, tmp, &srv->client_list, link) {
		/* Flush out buffered requests. If the Waltham socket is
		 * full, poll it for writable too.
		 */
		ret = wth_connection_flush(c->connection);
		if (ret < 0 && errno == EAGAIN) {
			watch_ctl(&c->conn_watch, EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT);
		} else if (ret < 0) {
			perror("Connection flush failed");
			client_destroy(c);
		}
	}
}

static void
server_accept_client(struct server *srv)
{
	struct client *client;
	struct wth_connection *conn;
	struct sockaddr_in addr;
	socklen_t len;

	len = sizeof addr;
	conn = wth_accept(srv->listen_fd, (struct sockaddr *)&addr, &len);
	if (!conn) {
		fprintf(stderr, "Failed to accept a connection.\n");

		return;
	}

	client = client_create(srv, conn);
	if (!client) {
		fprintf(stderr, "Failed client_create().\n");

		return;
	}
}

static void
listen_socket_handle_data(struct watch *w, uint32_t events)
{
	struct server *srv = container_of(w, struct server, listen_watch);
	int ret;

	if (events & EPOLLERR) {
		fprintf(stderr, "Listening socket errored out.\n");
		srv->running = false;

		return;
	}

	if (events & EPOLLHUP) {
		fprintf(stderr, "Listening socket hung up.\n");
		srv->running = false;

		return;
	}

	if (events & EPOLLIN)
		server_accept_client(srv);
}

static void
mainloop(struct server *srv)
{
	struct epoll_event ee[MAX_EPOLL_WATCHES];
	struct watch *w;
	int count;
	int i;
	int ret;

	srv->running = true;

	while (srv->running) {
		/* Run any idle tasks at this point. */

		server_flush_clients(srv);

		/* Wait for events or signals */
		count = epoll_wait(srv->epoll_fd,
				   ee, ARRAY_LENGTH(ee), -1);
		if (count < 0 && errno != EINTR) {
			perror("Error with epoll_wait");
			break;
		}

		/* Handle all fds, both the listening socket
		 * (see listen_handle_data()) and clients
		 * (see connection_handle_data()).
		 */
		for (i = 0; i < count; i++) {
			w = ee[i].data.ptr;
			w->cb(w, ee[i].events);
		}
	}
}

static int
server_listen(uint16_t tcp_port)
{
	int fd;
	int reuse = 1;
	struct sockaddr_in addr;

	fd = socket(AF_INET, SOCK_STREAM, 0);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(tcp_port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

	if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		fprintf(stderr, "Failed to bind to port %d", tcp_port);
		close(fd);
		return -1;
	}

	if (listen(fd, 1024) < 0) {
		fprintf(stderr, "Failed to listen to port %d", tcp_port);
		close (fd);
		return -1;
	}

	return fd;
}

int
main(int arcg, char *argv[])
{
	struct server srv = { 0 };
	int fd;

	wl_list_init(&srv.client_list);

	srv.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (srv.epoll_fd == -1) {
		perror("Error on epoll_create1");
		exit(1);
	}

	srv.listen_fd = server_listen(34400);
	if (srv.listen_fd < 0) {
		perror("Error setting up listening socket");
		exit(1);
	}

	srv.listen_watch.server = &srv;
	srv.listen_watch.cb = listen_socket_handle_data;
	srv.listen_watch.fd = srv.listen_fd;
	if (watch_ctl(&srv.listen_watch, EPOLL_CTL_ADD, EPOLLIN) < 0) {
		perror("Error setting up listen polling");
		exit(1);
	}

	mainloop(&srv);

	/* destroy all things */

	close(srv.listen_fd);
	close(srv.epoll_fd);

	return 0;
}