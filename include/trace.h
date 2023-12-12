#ifndef __TRACE_H
#define __TRACE_H

#include <regex.h>
#include <list.h>

#define MAX_MATCHES 16
#define TASK_COMM_LEN 16

#define EXPR_COMMON ".+-([0-9]+) +\\[([0-9]{3})\\].+ ([0-9]+\\.[0-9]{6}): "

struct event;
struct pattern;

struct pattern {
	const char *expr;
	regex_t regex;
	int nr_args;
	char type;
	const char *event;
	int (*parse) (const char *, struct event *);
	int (*process)(struct event *);
	void (*dump) (struct event *);
};

struct event {
	struct list_head entry;
	struct pattern *match;
	unsigned char type;
	unsigned int pid;
	unsigned int cpu;
	unsigned long long time;
	unsigned int major;
	unsigned int minor;
	char rwbs[8];
	unsigned long long bytes;
	unsigned long long sector;
	unsigned long long nr_sector;
	char comm[TASK_COMM_LEN];
	int error;
	int valid;
};

extern int regex_init(struct pattern *);
extern void regex_free(struct pattern *);
extern int parse_event(const char *, struct pattern *, struct event *);
extern int process_event(struct event *);
extern void dump_event(struct event *);

#endif /* __TRACE_H */

