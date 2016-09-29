/**
 * @file scheduler.h
 * @author Snyo 
 * @brief Schedule plugins
 */

#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "runnable.h"
#include "shash.h"

typedef runnable_t scheduler_t;

extern scheduler_t storage;

int  scheduler_init(scheduler_t *scheduler);
void scheduler_fini(scheduler_t *scheduler);

void scheduler_main(void *_scheduler);

#endif