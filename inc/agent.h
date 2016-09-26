/** @file agent.h @author Snyo */

#ifndef _AGENT_H_
#define _AGENT_H_

#include "snyohash.h"
#include "util.h"

#include <stdbool.h>
#include <pthread.h>
#include <json/json.h>

#define MAX_STORAGE 2

typedef struct _agent agent_t;

struct _agent {
	/* Status variables */
	volatile unsigned alive : 1;
	volatile unsigned working : 1;

	/* Target info */
	const char *name;
	const char *type;
	int        id;
	const char *agent_ip;
	const char *target_ip;

	/* Collecting varables */
	unsigned int period;
	timestamp    first_update;
	timestamp    last_update;
	timestamp    due;

	/* Thread variables */
	pthread_t       running_thread;
	pthread_mutex_t sync;   // Synchronization
	pthread_cond_t  synced;
	pthread_mutex_t access; // Read, Write
	pthread_cond_t  poked;

	/* Buffer */
	int stored;
	json_object *values;

	/* Logging */
	void *tag;

	/* Metric info */
	const char **metric_names;
	
	/* Inheritance */
	void *detail;

	/* Polymorphism */
	void (*collect_metrics)(agent_t *);
	void (*delete)(agent_t *);
};

/** @brief Constructor */
agent_t *new_agent(const char *name, unsigned int period);
/** @brief Destructor */
void delete_agent(agent_t *agent);

/**
 * @defgroup agent_syscall
 * System Calls
 * @{
 */
/** @brief Start the agent in thread */
int start(agent_t *agent);
void kill(agent_t *agent);
/** @brief Run loop */
void *run(void *_agent);
/** @brief Restart the agent */
void restart(agent_t *agent);
/** @brief Poke the agent to start update */
void poke(agent_t *agent);
/** @brief Post the buffer */
void pack(agent_t *agent);
void add_metrics(agent_t *agent, json_object *jarr);
/** @} */

/**
 * @defgroup agent_check
 * Check status of the agent
 * @{
 */
bool busy(agent_t *agent);
bool timeup(agent_t *agent);
bool outdated(agent_t *agent);
/** @} */

#endif