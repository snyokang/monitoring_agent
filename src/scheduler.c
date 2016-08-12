/** @file scheduler.c @author Snyo */

#include "scheduler.h"

#include "agent.h"
#include "agents/mysql.h"
#include "snyohash.h"
#include "sender.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <uuid/uuid.h>

zlog_category_t *scheduler_tag;

char *agent_names[] = {
	"MySQL1",
	"MySQL2"
};
agent_t *(*agent_constructors[])(const char *, const char *) = {
	mysql_agent_init,
	mysql_agent_init
};
shash_t *agents;
int agent_number = sizeof(agent_names)/sizeof(char *);

char g_license[100];
char g_uuid[40];

int get_license(char *license);
int get_hostname(char *hostname);
int get_uuid(char *uuid);
int get_agents(char *agents);

// TODO exception handling
void scheduler_init() {
	/* Logging */
	scheduler_tag = (void *)zlog_get_category("Scheduler");
    if (!scheduler_tag) exit(1);

    /* Sender */
	zlog_debug(scheduler_tag, "Initialize sender");
	sender_init("http://192.168.122.37:8082/v1/agents");
    if (!g_sender) {
    	zlog_error(scheduler_tag, "Fail to create a new sender");
    	exit(1);
    }

	/* Register topics */
	// Example
	// {
	//     "license":"asdf",
	//     "hostname":"abcd",
	//     "uuid": "1234",
	//     "agents": "MySQL1, MySQL2"
	// }
	zlog_debug(scheduler_tag, "Register topic to the server");
	char reg_json[128], *ptr;
	ptr = reg_json;
	get_license(g_license);
	ptr += sprintf(ptr, "{\"license\":\"%s", g_license);
	ptr += sprintf(ptr, "\",\"hostname\":\"");
	ptr += get_hostname(ptr);
	get_uuid(g_uuid);
	ptr += sprintf(ptr, "\",\"uuid\":\"%.*s", 36, g_uuid);
	ptr += sprintf(ptr, "\",\"agents\":\"");
	ptr += get_agents(ptr);
	ptr += sprintf(ptr, "\"}");
	if(reg_topic(reg_json) > 0) {
		zlog_error(scheduler_tag, "Fail to register topic");
    	exit(1);
	}

    /* Agents */
	zlog_debug(scheduler_tag, "Initialize agents");
	agents = shash_init();
	if(!agents) {
		zlog_error(scheduler_tag, "Failed to create an agent hash");
    	exit(1);
	}

	for(int i=0; i<agent_number; ++i) {
		zlog_debug(scheduler_tag, "Initialize agent \'%s\'", agent_names[i]);
		char conf_file[100];
		// TODO, genralize conf file name
		sprintf(conf_file, "conf/%s.yaml", "mysql");
		agent_t *agent = (agent_constructors[i])(agent_names[i], conf_file);
		if(!agent) {
			zlog_error(scheduler_tag, "Failed to create \'%s\'", agent_names[i]);
		} else {
			shash_insert(agents, agent_names[i], agent, ELEM_PTR);
			start(agent);
		}
	}
}

void schedule() {
	scheduler_init();
	while(1) {
		timestamp loop_start = get_timestamp();
		for(int i=0; i<agent_number; ++i) {
			agent_t *agent = (agent_t *)shash_search(agents, agent_names[i]);
			if(!agent) continue;
			zlog_debug(scheduler_tag, "Status of \'%s\' [%c%c%c]", \
				                            agent_names[i], \
			                                outdated(agent)?'O':'.', \
			                                busy(agent)?'B':'.', \
			                                busy(agent)&&timeup(agent)?'T':'.');
			if(busy(agent)) {
				if(timeup(agent))
					restart(agent);
			} else if(outdated(agent)) {
				poke(agent);
			}
		}

		if(TICK > get_timestamp()-loop_start) {
			timestamp remain_tick = TICK-(get_timestamp()-loop_start);
			zlog_debug(scheduler_tag, "Sleep for %fms", remain_tick/1000000.0);
			snyo_sleep(remain_tick);
		}
	}
	scheduler_fini();
}

void scheduler_fini() {
	// TODO
}


/**
 * These are used only in this source file
 */

int get_license(char *license) {
	return sprintf(license, "%s", "test");
}

int get_hostname(char *hostname) {
	if(!gethostname(hostname, 100))
		return strlen(hostname);
	return sprintf(hostname, "fail_to_get_hostname");
}

int get_uuid(char *uuid) {
	// uuid_t out;
	// uuid_generate_time(out);
	// uuid_unparse(out, uuid);
	sprintf(uuid, "00000000-0000-0000-0000-000000000000");
	return 36;
}

int get_agents(char *agents) {
	int n = 0;
	for(int i=0; i<agent_number; ++i) {
		n += sprintf(agents+n, "%s", agent_names[i]);
		if(i < agent_number-1)
			sprintf(agents+n++, ",");
	}
	return n;
}
