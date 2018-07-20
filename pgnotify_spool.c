#include <uwsgi.h>
#include "libpq-fe.h"

extern struct uwsgi_server uwsgi;

struct uwsgi_pgnotify_connection {
	char *conn_string;
	char *channel;
	PGconn *conn;
	int fd;
};

static struct uwsgi_pgnotify {
	struct uwsgi_string_list *signals;
} upgnotify;

static struct uwsgi_option pgnotify_options[] = {
	{"pgnotify-spool", required_argument, 0, "<channel> <conn>", uwsgi_opt_add_string_list, &upgnotify.signals, UWSGI_OPT_MASTER},
	UWSGI_END_OF_OPTIONS
};

static int pgnotify_init() {
	struct uwsgi_string_list *usl = NULL;
	uwsgi_foreach(usl, upgnotify.signals) {
		size_t rlen = 0;		
		char **options = uwsgi_split_quoted(usl->value, usl->len, " \t", &rlen);
		if (rlen != 2) {
			uwsgi_log("invalid pgnotify-signal syntax, must be <channel> <connectionstring>\n");
			exit(1);
		}
		struct uwsgi_pgnotify_connection *upc = uwsgi_calloc(sizeof(struct uwsgi_pgnotify_connection));
		upc->channel = uwsgi_concat2("LISTEN ", options[0]);
		upc->conn_string = options[1];
		upc->fd = -1;
		usl->custom_ptr = upc;
	}
	return 0;
}

static struct uwsgi_pgnotify_connection *pgnotify_get_connection_by_fd(int fd) {
	struct uwsgi_string_list *usl = NULL;
	uwsgi_foreach(usl, upgnotify.signals) {
		struct uwsgi_pgnotify_connection *upc = (struct uwsgi_pgnotify_connection *) usl->custom_ptr;
		if (upc->fd == fd) return upc;
	}
	return NULL;
}

static int uwsgi_pgnotify_connect(struct uwsgi_pgnotify_connection *upc) {
	upc->conn = PQconnectdb(upc->conn_string);
	if (PQstatus(upc->conn) != CONNECTION_OK) {
		uwsgi_log_verbose("connection to database for \"%s\" failed: %s", upc->channel, PQerrorMessage(upc->conn));
		PQfinish(upc->conn);
		return -1;
	}

	PGresult *res = PQexec(upc->conn, upc->channel);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		uwsgi_log_verbose("%s failed: %s", upc->channel, PQerrorMessage(upc->conn));
		PQclear(res);
		PQfinish(upc->conn);
		return -1;
	}

	PQclear(res);

	upc->fd = PQsocket(upc->conn);
	if (upc->fd < 0) {
		PQfinish(upc->conn);
		return -1;
	}

	uwsgi_log_verbose("listening to postgresql channel \"%s\" on fd: %d\n", upc->channel+7, upc->fd);

	return upc->fd;
}

static void *pgnotify_loop(void *foobar) {
	// block all signals
        sigset_t smask;
        sigfillset(&smask);
        pthread_sigmask(SIG_BLOCK, &smask, NULL);

	struct uwsgi_string_list *usl = NULL;
	int queue = event_queue_init();

	// first round of connections
	uwsgi_foreach(usl, upgnotify.signals) {
		struct uwsgi_pgnotify_connection *upc = (struct uwsgi_pgnotify_connection *) usl->custom_ptr;
		if (uwsgi_pgnotify_connect(upc) <= 0) continue;
		if (event_queue_add_fd_read(queue, upc->fd)) {
			PQfinish(upc->conn);
			upc->fd = -1;
		}
	}

	// enter the main loop
	for(;;) {
		int interesting_fd = -1;
		int ret = event_queue_wait(queue, 3, &interesting_fd);
		// 0 events retry connections
		if (ret == 0) {
			uwsgi_foreach(usl, upgnotify.signals) {
				struct uwsgi_pgnotify_connection *upc = (struct uwsgi_pgnotify_connection *) usl->custom_ptr;
				if (upc->fd < 0) {
					if (uwsgi_pgnotify_connect(upc) <= 0) continue;
					if (event_queue_add_fd_read(queue, upc->fd)) {
                        			PQfinish(upc->conn);
                        			upc->fd = -1;
                			}
				}
			}
			continue;
		}

		// who is notifying ?
		struct uwsgi_pgnotify_connection *upc = pgnotify_get_connection_by_fd(interesting_fd);
		if (!upc) {
			uwsgi_log("unknown postgresql event received from fd %d !!!\n", interesting_fd);
			// better to close it
			close(interesting_fd);
			continue;
		}
		if (PQconsumeInput(upc->conn) <= 0) {
			uwsgi_log_verbose("lost connection with postgresql server for channel \"%s\"\n", upc->channel+7);
			PQfinish(upc->conn);
			upc->fd = -1;	
			continue;
		}
		// read notifications
		for(;;) {
			PGnotify *notify = PQnotifies(upc->conn);
			if (notify == NULL) break;
			uwsgi_log_verbose("received postgresql notification for %s by PID %d, extra = %s\n", notify->relname, (int) notify->be_pid, notify->extra);
			struct uwsgi_buffer *ub = uwsgi_buffer_new(uwsgi.page_size);
			if ( notify->extra ) {
				char * key = "payload";
				size_t klen = sizeof(key)-1;
				uwsgi_buffer_append_keyval(ub, key, klen, notify->extra, strlen(notify->extra));
			}
			char *filename = uwsgi_spool_request(NULL, ub->buf, ub->pos, NULL, 0);
			uwsgi_buffer_destroy(ub);
			free(filename);
			PQfreemem(notify);
		}	
	}
	return NULL;
}

static void pgnotify_spawn_thread() {
	pthread_t t;
	pthread_create(&t, NULL, pgnotify_loop, NULL);
}

struct uwsgi_plugin pgnotify_spool_plugin = {
	.name = "pgnotify_spool",
	.options = pgnotify_options,
	.init = pgnotify_init,
	.postinit_apps = pgnotify_spawn_thread,
};
