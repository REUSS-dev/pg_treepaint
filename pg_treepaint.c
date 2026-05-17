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

PG_MODULE_MAGIC;

#define TARGET_IP "172.25.128.1"
#define TARGET_PORT 4523

typedef struct
{
	bool tp_plan;
} tp_options;

static int es_extension_id;
static explain_per_plan_hook_type prev_explain_per_plan_hook = NULL;

void _PG_init(void);
static void tp_option_handler(ExplainState* es, DefElem* opt, ParseState* pstate);
static void tp_per_plan_hook(PlannedStmt* plannedstmt, IntoClause* into,
	ExplainState* es, const char* queryString,
	ParamListInfo params, QueryEnvironment* queryEnv);
static void send_to_socket(const char* data);

void
_PG_init(void)
{
	es_extension_id = GetExplainExtensionId("pg_treepaint");

	RegisterExtensionExplainOption("tp_plan",
		tp_option_handler,
		GUCCheckBooleanExplainOption);

	prev_explain_per_plan_hook = explain_per_plan_hook;
	explain_per_plan_hook = tp_per_plan_hook;
}

static void
tp_option_handler(ExplainState* es, DefElem* opt, ParseState* pstate)
{
	tp_options* options;

	options = GetExplainExtensionState(es, es_extension_id);
	if (options == NULL)
	{
		options = palloc0_object(tp_options);
		SetExplainExtensionState(es, es_extension_id, options);
	}

	options->tp_plan = defGetBoolean(opt);
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
			send_to_socket(es->str->data);
		}
	}
}

static void
send_to_socket(const char* data)
{
	int sock = 0;
	struct sockaddr_in serv_addr;

	sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock < 0)
	{
		elog(WARNING, "pg_treepaint: Socket creation failed");
		return;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(TARGET_PORT);

	if (inet_pton(AF_INET, TARGET_IP, &serv_addr.sin_addr) <= 0)
	{
		elog(WARNING, "pg_treepaint: Invalid target IP address");
		close(sock);
		return;
	}

	if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
	{
		elog(WARNING, "pg_treepaint: TCP connection failed to %s:%d", TARGET_IP, TARGET_PORT);
		close(sock);
		return;
	}

	if (send(sock, data, strlen(data), 0) < 0)
	{
		elog(WARNING, "pg_treepaint: Failed to transmit payload over network");
	}

	close(sock);
}
