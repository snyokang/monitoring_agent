/**
 * @file agent.c
 * @author Snyo
 */
#include "agent.h"

#include "snyohash.h"
#include "sender.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>

#include <zlog.h>
#include <json/json.h>

#define MAX_AGENT_NAME 50

extern void *scheduler_tag;

agent_t *new_agent(const char *name, unsigned int period) {
	/* Category init */
	char category[6 + MAX_AGENT_NAME + 1] = "Agent_";
	snprintf(category+6, MAX_AGENT_NAME, "%s", name);
	zlog_category_t *_tag = zlog_get_category(category);
	ASSERT(_tag, NULL, NULL, scheduler_tag, "Fail to zlog init of %s", category);

	/* New agent */
    zlog_debug(scheduler_tag, "Create a new agent \'%s\'", name);
	agent_t *agent = (agent_t *)malloc(sizeof(agent_t));
	ASSERT(agent, NULL, NULL, scheduler_tag, "Allocation fail");

	agent->alive = 0;

	// TODO init exception
	pthread_mutex_init(&agent->sync, NULL);
	pthread_cond_init(&agent->synced, NULL);
	pthread_mutex_init(&agent->access, NULL);
	pthread_cond_init(&agent->poked, NULL);

	agent->values = json_object_new_object();
	
	agent->tag = (void *)_tag;
	agent->name = name;
	agent->period = period;

	return agent;
}

void delete_agent(agent_t *agent) {
	zlog_debug(scheduler_tag, "Deleting an agent");
	if(!agent) return;

	pthread_mutex_destroy(&agent->sync);
	pthread_cond_destroy(&agent->synced);
	pthread_mutex_destroy(&agent->access);
	pthread_cond_destroy(&agent->poked);

	free(agent);
}

int start(agent_t *agent) {
	zlog_debug(scheduler_tag, "Initialize agent values");

	agent->alive = 1;
	agent->working = 0;

	agent->last_update = 0;
	agent->deadline = 0;

	agent->stored = 0;

	zlog_debug(scheduler_tag, "Start agent thread");
	pthread_mutex_lock(&agent->sync);
	pthread_create(&agent->running_thread, NULL, run, agent);

	// Let the agent holds "access" lock
	struct timespec timeout = {get_timestamp()/NS_PER_S + 5, 0};
	return pthread_cond_timedwait(&agent->synced, &agent->sync, &timeout);
}

void kill(agent_t *agent) {
	agent->alive = 0;
	pthread_cancel(agent->running_thread);
	pthread_mutex_unlock(&agent->sync);
	pthread_mutex_unlock(&agent->access);
}

void restart(agent_t *agent) {
	zlog_fatal(scheduler_tag, "Restart \'%s\'", agent->name);
	kill(agent);
	start(agent);
}

void poke(agent_t *agent) {
	pthread_mutex_lock(&agent->access);
	zlog_info(scheduler_tag, "Poking the agent");
	pthread_cond_signal(&agent->poked);
	pthread_mutex_unlock(&agent->access);

	// confirm the agent starts collecting
	zlog_info(scheduler_tag, "Waiting to be synced");
	pthread_cond_wait(&agent->synced, &agent->sync);
}

void *run(void *_agent) {
	agent_t *agent = (agent_t *)_agent;

	pthread_mutex_lock(&agent->sync);
	pthread_mutex_lock(&agent->access);
	pthread_cond_signal(&agent->synced);
	pthread_mutex_unlock(&agent->sync);

	while(agent->alive) {
		zlog_info(agent->tag, "Waiting to be poked");
		pthread_cond_wait(&agent->poked, &agent->access);
		zlog_debug(agent->tag, "Poked");

		// Prevent the scheduler not to do other work before collecting start
		pthread_mutex_lock(&agent->sync);

		agent->working = 1;
		agent->deadline = get_timestamp() + (NS_PER_S/10*9)*agent->period;

		pthread_cond_signal(&agent->synced);
		pthread_mutex_unlock(&agent->sync);

		if(!agent->last_update) {
			 agent->last_update = get_timestamp();
		} else {
			agent->last_update += agent->period*NS_PER_S;
		}
		if(!agent->first_update)
			agent->first_update = agent->last_update;

		zlog_debug(agent->tag, "Start updating");
		agent->collect_metrics(agent);
		agent->stored++;

		if(agent->stored == MAX_STORAGE) {
			zlog_debug(agent->tag, "Buffer is full");
			pack(agent);
			json_object_put(agent->values);
			agent->values = json_object_new_object();
		}
		agent->working = 0;
		zlog_debug(agent->tag, "Updating done");
	}
	zlog_debug(agent->tag, "Agent is dying");
	return NULL;
}

void pack(agent_t *agent) {
	zlog_debug(agent->tag, "Packing \'%s\'", agent->name);

	json_object *package = json_object_new_object();
	extern char g_license[];
	extern char g_uuid[];
	json_object_object_add(package, "license", json_object_new_string(g_license));
	json_object_object_add(package, "target_id", json_object_new_string(agent->id));
	json_object_object_add(package, "uuid", json_object_new_string(g_uuid));
	json_object_object_add(package, "target_name", json_object_new_string(agent->name));
	json_object_object_add(package, "agent_ip", json_object_new_string(agent->agent_ip));
	json_object_object_add(package, "target_ip", json_object_new_string(agent->target_ip));
	json_object_object_add(package, "target_type", json_object_new_string(agent->type));
	// json_object *m_arr = json_object_new_array();
	// for(int k=0; k<NUMBER_OF(agent->metric_names); ++k) {
	// 	json_object_array_add(m_arr, json_object_new_string(agent->metric_names[k]));
	// }
	// json_object_object_add(package, "metrics", m_arr);
	// json_object_put(agent->metrics);
	// agent->metrics = json_object_new_array();
	json_object_object_add(package, "metrics", agent->metric_names2);
	json_object_object_add(package, "values", agent->values);

	char *payload;
	payload = strdup(json_object_to_json_string(package));
	zlog_debug(agent->tag, "Payload\n%s (%zu)", payload, strlen(payload));

	zlog_debug(agent->tag, "Flushing buffer");
	agent->stored = 0;
	agent->first_update = 0;

	zlog_debug(agent->tag, "Enqueue the payload");
	enq_payload(payload);
}

void add_metrics(agent_t *agent, json_object *metrics) {
	char ts_str[20];
	sprintf(ts_str, "%lu", agent->last_update);
	json_object_object_add(agent->values, ts_str, metrics);
}

bool busy(agent_t *agent) {
	return agent->working;
}

bool timeup(agent_t *agent) {
	return get_timestamp() > agent->deadline;
}

bool outdated(agent_t *agent) {
	return !agent->last_update \
	       || (get_timestamp() - agent->last_update)/NS_PER_S >= (agent->period);
}