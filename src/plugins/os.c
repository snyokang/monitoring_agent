/**
 * @file os.c
 * @author Snyo
 */
#include "plugins/os.h"

#include <stdio.h>

#include "pluggable.h"
#include "util.h"

#define OS_PLUGIN_TICK NS_PER_S/2

const char *network_metric_names[] = {
	"net_stat/recv_byte",
	"net_stat/recv_packet",
	"net_stat/recv_error",
	"net_stat/send_byte",
	"net_stat/send_packet",
	"net_stat/send_error"
};

const char *disk_metric_names[] = {
	"disk_stat/IOPs",
	"disk_stat/average_queue",
	"disk_stat/await",
	"disk_stat/service_time"
};
const char *proc_metric_names[] = {
	"proc_stat/user",
	"proc_stat/cpu_usage",
	"proc_stat/mem_usage"
};

int collect_cpu_metrics(plugin_t *plugin, json_object *values);
int collect_disk_metrics(plugin_t *plugin, json_object *values);
int collect_proc_metrics(plugin_t *plugin, json_object *values);
int collect_memory_metrics(plugin_t *plugin, json_object *values);
int collect_network_metrics(plugin_t *plugin, json_object *values);

void os_plugin_init(os_plugin_t *plugin) {
	plugin->spec = malloc(sizeof(int));
	if(!plugin->spec) return;
	*(int *)plugin->spec = 0;

	plugin->num = 1;
	plugin->target_ip = "test";

	plugin->period = OS_PLUGIN_TICK;

	plugin->full_count = 5;

	// polymorphism
	plugin->collect = collect_os_metrics;
	plugin->fini = os_plugin_fini;
}

void os_plugin_fini(os_plugin_t *plugin) {
	if(!plugin)
		return;
	
	if(plugin->spec)
		free(plugin->spec);
}

void collect_os_metrics(os_plugin_t *plugin) {
	json_object *values = json_object_new_array();

	int metric_number = 0;
	metric_number += collect_network_metrics(plugin, values);
	metric_number += collect_cpu_metrics(plugin, values);
	metric_number += collect_disk_metrics(plugin, values);
	// metric_number += collect_proc_metrics(plugin, values);
	metric_number += collect_memory_metrics(plugin, values);

	if(!*(int *)plugin->spec)
		*(int *)plugin->spec = metric_number;

	char ts[100];
	sprintf(ts, "%lu", plugin->next_run - plugin->period);
	json_object_object_add(plugin->values, ts, values);
	++plugin->holding;
}

int collect_network_metrics(plugin_t *plugin, json_object *values) {
	FILE *pipe = popen("cat /proc/net/dev | grep : | awk \'{sub(\":\", \"\", $1); print $1, $2, $3, $4, $10, $11, $12}\'", "r");
	if(!pipe) return 0;

	char net_name[50];
	int number = 0;
	int recv_byte, recv_pckt, recv_err;
	int send_byte, send_pckt, send_err;

	int collected = 0;
	while(fscanf(pipe, "%s", net_name) == 1) {
		if(fscanf(pipe, "%d%d%d%d%d%d", &recv_byte, &recv_pckt, &recv_err, &send_byte, &send_pckt, &send_err) == 6) {
			if(!*(int *)plugin->spec) {
				for(int i=0; i<sizeof(network_metric_names)/sizeof(char *); ++i) {
					char new_metric[150];
					sprintf(new_metric, "%s/%s", network_metric_names[i], net_name);
					json_object_array_add(plugin->metric_names, json_object_new_string(new_metric));
					collected++;
				}
			}
			number++;
			json_object_array_add(values, json_object_new_int(recv_byte));
			json_object_array_add(values, json_object_new_int(recv_pckt));
			json_object_array_add(values, json_object_new_int(recv_err));
			json_object_array_add(values, json_object_new_int(send_byte));
			json_object_array_add(values, json_object_new_int(send_pckt));
			json_object_array_add(values, json_object_new_int(send_err));
		} else break;
	}
	json_object_array_add(values, json_object_new_int(number));
	pclose(pipe);

	return collected;
}

int collect_cpu_metrics(plugin_t *plugin, json_object *values) {
	FILE *pipe;
	int collected = 0;

	pipe = popen("iostat | grep -A 1 avg-cpu: | tail -n 1 | awk '{print $1, $3, $6}'", "r");
	if(!pipe) return 0;

	float cpu_user, cpu_sys, cpu_idle;
	if(fscanf(pipe, "%f%f%f", &cpu_user, &cpu_sys, &cpu_idle) != 3)
		return collected;
	if(!*(int *)plugin->spec) {
		json_object_array_add(plugin->metric_names, json_object_new_string("cpu_stat/total"));
		json_object_array_add(plugin->metric_names, json_object_new_string("cpu_stat/user"));
		json_object_array_add(plugin->metric_names, json_object_new_string("cpu_stat/sys"));
		json_object_array_add(plugin->metric_names, json_object_new_string("cpu_stat/idle"));
		collected++;
	}
	json_object_array_add(values, json_object_new_double(100-cpu_idle));
	json_object_array_add(values, json_object_new_double(cpu_user));
	json_object_array_add(values, json_object_new_double(cpu_sys));
	json_object_array_add(values, json_object_new_double(cpu_idle));

	return collected;
}

int collect_disk_metrics(plugin_t *plugin, json_object *values) {
	FILE *pipe = popen("df -T -P | grep ext | awk '{sub(\"%%\", \"\", $6); print $1, $6}'", "r");
	if(!pipe) return 0;

	char disk_name[100];
	int disk_usage;

	int collected = 0;
	while(fscanf(pipe, "%s", disk_name) == 1) {
		if(fscanf(pipe, "%d", &disk_usage) == 1) {
			if(!*(int *)plugin->spec) {
				char new_metric[150];
				sprintf(new_metric, "disk_stat/disk_usage/%s", disk_name);
				json_object_array_add(plugin->metric_names, json_object_new_string(new_metric));
				collected ++;
			}
			json_object_array_add(values, json_object_new_double((double)disk_usage/100));
		} else break;
	}
	pclose(pipe);

	pipe = popen("iostat -xc | grep -A 5 Device: | awk '{print $1, $4, $5, $9, $10, $13}'", "r");
	if(!pipe) return 0;
	float rps, wps, avgqu, await, svctm;
	if(fscanf(pipe, "%*[^\n]\n") != 0)
		return collected;
	while(fscanf(pipe, "%s", disk_name) == 1) {
		if(fscanf(pipe, "%f%f%f%f%f", &rps, &wps, &avgqu, &await, &svctm) == 5) {
			if(!*(int *)plugin->spec) {
				for(int i=0; i<sizeof(disk_metric_names)/sizeof(char *); ++i) {
					char new_metric[150];
					sprintf(new_metric, "%s/%s", disk_metric_names[i], disk_name);
					json_object_array_add(plugin->metric_names, json_object_new_string(new_metric));
					collected ++;
				}
			}
			json_object_array_add(values, json_object_new_double(rps+wps));
			json_object_array_add(values, json_object_new_double(avgqu));
			json_object_array_add(values, json_object_new_double(await));
			json_object_array_add(values, json_object_new_double(svctm));
		} else break;
	}
	pclose(pipe);

	return collected;
}

int collect_proc_metrics(plugin_t *plugin, json_object *values) {
	FILE *pipe = popen("top -n 1 | grep -A 3 PID | tail -n 3 | awk \'{print $3, $10, $11, $13}\'", "r");
	if(!pipe) return 0;

	int collected = 0;

	char user[100], name[100];
	double cpu, mem;
	while(fscanf(pipe, "%s%lf%lf%s", user, &cpu, &mem, name) == 4) {
		printf("%s %lf %lf %s\n", user, cpu, mem, name);
	}
	pclose(pipe);

	return collected;
}

int collect_memory_metrics(plugin_t *plugin, json_object *values) {
	FILE *pipe = popen("cat /proc/meminfo | grep -E \"^Mem|^Cached|^Active:|^Inactive:|^Vmalloc[TU]\" | awk '{print $2}'", "r");
	if(!pipe) return 0;

	int total, free, available, cached;
	int active, inactive;
	int v_total, v_used;

	int collected = 0;

	if(fscanf(pipe, "%d%d%d%d%d%d%d%d", &total, &free, &available, &cached, &active, &inactive, &v_total, &v_used) != 8)
		return 0;

	if(!*(int *)plugin->spec) {
		json_object_array_add(plugin->metric_names, json_object_new_string("mem_stat/total"));
		json_object_array_add(plugin->metric_names, json_object_new_string("mem_stat/free"));
		json_object_array_add(plugin->metric_names, json_object_new_string("mem_stat/available"));
		json_object_array_add(plugin->metric_names, json_object_new_string("mem_stat/cached"));
		json_object_array_add(plugin->metric_names, json_object_new_string("mem_stat/user"));
		json_object_array_add(plugin->metric_names, json_object_new_string("mem_stat/sys"));
		json_object_array_add(plugin->metric_names, json_object_new_string("mem_stat/virtual_usage"));
	}
	json_object_array_add(values, json_object_new_int(total));
	json_object_array_add(values, json_object_new_int(free));
	json_object_array_add(values, json_object_new_int(available));
	json_object_array_add(values, json_object_new_int(cached));
	json_object_array_add(values, json_object_new_int((active+inactive)));
	json_object_array_add(values, json_object_new_int((total-free-active-inactive)));
	json_object_array_add(values, json_object_new_int((double)v_used/(double)v_total));
	collected += 7;

	pclose(pipe);

	return collected;
}
