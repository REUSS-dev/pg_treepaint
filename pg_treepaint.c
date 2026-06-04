#include "postgres.h"
#include "fmgr.h"
#include "commands/defrem.h"
#include "commands/explain.h"
#include "commands/explain_format.h"
#include "commands/explain_state.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>

PG_MODULE_MAGIC;

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 4523
#define SOCKET_TIMEOUT 1

typedef struct
{
	bool tp_plan;
	char* tp_ip;
	int tp_port;
} tp_options;

static int es_extension_id;
static explain_per_plan_hook_type prev_explain_per_plan_hook = NULL;

void _PG_init(void);
static void tp_plan_handler(ExplainState* es, DefElem* opt, ParseState* pstate);
static void tp_ip_handler(ExplainState* es, DefElem* opt, ParseState* pstate);
static void tp_port_handler(ExplainState* es, DefElem* opt, ParseState* pstate);
static void tp_per_plan_hook(PlannedStmt* plannedstmt, IntoClause* into,
	ExplainState* es, const char* queryString,
	ParamListInfo params, QueryEnvironment* queryEnv);
static void send_to_socket(const char* data, const char* ip, int port);
static tp_options* ensure_tp_options(ExplainState* es);

void
_PG_init(void)
{
	es_extension_id = GetExplainExtensionId("pg_treepaint");

	RegisterExtensionExplainOption("tp_plan",
		tp_plan_handler,
		GUCCheckBooleanExplainOption);

	RegisterExtensionExplainOption("tp_ip",
		tp_ip_handler,
		NULL);

	RegisterExtensionExplainOption("tp_port",
		tp_port_handler,
		NULL);

	prev_explain_per_plan_hook = explain_per_plan_hook;
	explain_per_plan_hook = tp_per_plan_hook;
}

static tp_options*
ensure_tp_options(ExplainState* es)
{
	tp_options* options = GetExplainExtensionState(es, es_extension_id);
	if (options == NULL)
	{
		options = palloc0_object(tp_options);
		options->tp_ip = NULL;
		options->tp_port = 0;

		SetExplainExtensionState(es, es_extension_id, options);
	}
	return options;
}

static void
tp_plan_handler(ExplainState* es, DefElem* opt, ParseState* pstate)
{
	tp_options* options = ensure_tp_options(es);
	options->tp_plan = defGetBoolean(opt);

	if (options->tp_plan)
	{
		es->format = EXPLAIN_FORMAT_JSON;
	}
}

static void
tp_ip_handler(ExplainState* es, DefElem* opt, ParseState* pstate)
{
	tp_options* options = ensure_tp_options(es);
	options->tp_ip = pstrdup(defGetString(opt));
}

static void
tp_port_handler(ExplainState* es, DefElem* opt, ParseState* pstate)
{
	tp_options* options = ensure_tp_options(es);
	options->tp_port = (int)defGetInt64(opt);
}

static void
tp_per_plan_hook(PlannedStmt* plannedstmt, IntoClause* into,
	ExplainState* es, const char* queryString,
	ParamListInfo params, QueryEnvironment* queryEnv)
{
	tp_options* options;

	if (prev_explain_per_plan_hook)
		(*prev_explain_per_plan_hook)(plannedstmt, into, es, queryString, params, queryEnv);

	options = GetExplainExtensionState(es, es_extension_id);
	if (options != NULL && options->tp_plan)
	{
		if (es->str != NULL && es->str->len > 0)
		{
			const char* target_ip = (options->tp_ip != NULL) ? options->tp_ip : DEFAULT_IP;
			int target_port = (options->tp_port > 0) ? options->tp_port : DEFAULT_PORT;

			send_to_socket(es->str->data, target_ip, target_port);
		}
	}
}

static void
send_to_socket(const char* data, const char* ip, int port)
{
	int sock = 0;
	struct sockaddr_in serv_addr;
	int flags;
	int connect_res;

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0)
	{
		elog(WARNING, "pg_treepaint: Invalid IP address");
		return;
	}

	if (port < 0 || 65535 < port)
	{
		elog(WARNING, "pg_treepaint: Invalid port. Port should be in range from 0 to 65535");
		return;
	}

	sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock < 0)
	{
		elog(WARNING, "pg_treepaint: Socket open failed");
		return;
	}

	flags = fcntl(sock, F_GETFL, 0);
	if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0)
	{
		elog(WARNING, "pg_treepaint: Failed to set non-blocking mode");
		close(sock);
		return;
	}

	connect_res = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

	if (connect_res < 0)
	{
		if (errno == EINPROGRESS)
		{
			fd_set write_fds;
			struct timeval timeout;
			int select_res;

			FD_ZERO(&write_fds);
			FD_SET(sock, &write_fds);

			timeout.tv_sec = SOCKET_TIMEOUT;
			timeout.tv_usec = 0;

			select_res = select(sock + 1, NULL, &write_fds, NULL, &timeout);

			if (select_res < 0)
			{
				elog(WARNING, "pg_treepaint: Error during select() connecting to %s:%d", ip, port);
				close(sock);
				return;
			}
			else if (select_res == 0)
			{
				elog(WARNING, "pg_treepaint: Connection timed out to %s:%d after %d second", ip, port, SOCKET_TIMEOUT);
				close(sock);
				return;
			}
			else
			{
				int so_error = 0;
				socklen_t len = sizeof(so_error);

				if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0)
				{
					elog(WARNING, "pg_treepaint: Connection failed to %s:%d. Socket error: %d", ip, port, so_error);
					close(sock);
					return;
				}
			}
		}
		else
		{
			elog(WARNING, "pg_treepaint: TCP connection failed immediately to %s:%d", ip, port);
			close(sock);
			return;
		}
	}

	if (fcntl(sock, F_SETFL, flags) < 0)
	{
		elog(WARNING, "pg_treepaint: Failed to reset blocking mode");
		close(sock);
		return;
	}

	if (send(sock, data, strlen(data), 0) < 0)
	{
		elog(WARNING, "pg_treepaint: Failed to transmit payload over network");
	}

	close(sock);
}
