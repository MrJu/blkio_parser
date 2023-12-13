#ifndef __TRACE_H
#define __TRACE_H

#include <regex.h>
#include <list.h>

#define MAX_MATCHES 16
#define TASK_COMM_LEN 16

#define EXPR_COMMON ".+-([0-9]+) +\\[([0-9]{3})\\].+ ([0-9]+\\.[0-9]{6}): "
#define __section __attribute((section(".app_init_sec")))

struct event;
struct pattern;
 
typedef struct init_t {
        int (*func)(int, char **);
        char *name;
}_init_t;
 
struct pattern {
	const char *name;
	const char *expr;
	regex_t regex;
	int nr_args;
	char type;
	int (*parse) (const char *, struct event *);
	int (*process)(struct event *);
	void (*dump) (struct event *);
};

struct event {
	struct pattern *match;
	unsigned int pid;
	unsigned int cpu;
	unsigned long long time;
	int valid;
};

extern int regex_init(struct pattern *);
extern void regex_free(struct pattern *);
extern int parse_event(const char *, struct pattern *, struct event *);
extern int process_event(struct event *);
extern void dump_event(struct event *);

#endif /* __TRACE_H */

