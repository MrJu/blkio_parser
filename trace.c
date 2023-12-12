#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <errno.h>
#include <regex.h>

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <regex.h>
#include <errno.h>

#include <list_sort.h>
#include <list.h>
#include <trace.h>
#include <block.h>



int regex_init(struct pattern *p)
{
	int i, ret;

	for (i = 0; p[i].expr; i++) {
		ret = regcomp(&p[i].regex, p[i].expr, REG_EXTENDED);
		if (ret) {
			fprintf(stderr, "compile regex pattern fail: %d\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

void regex_free(struct pattern *p)
{
	int i;

	for (i = 0; p[i].expr; i++) {
		regfree(&p[i].regex);
	}
}

int parse_event(const char *record, struct pattern *p, struct event *e)
{
	int i, ret = 0;
	char temp[256];
	char *name;
	int count, offset = 0;
	regmatch_t matches[MAX_MATCHES];

	for (i = 0; p[i].expr; i++) {
		name = strstr(record, p[i].event);
		if (!name)
			continue;
		else {
			ret = regexec(&p[i].regex, record, MAX_MATCHES, matches, 0);
			if (!ret) {
				/* matched */
				e->match = &p[i];
				break;
			}
		}
	}

	if (!e->match) {
		e->valid = 0;
		/* not matched but not as an error */
		return 0;
	}

	for(i = 1, offset = 0; i < e->match->nr_args + 1; i++) {  
		if (matches[i].rm_so == -1)
			break;
		count = sprintf(temp + offset, "%.*s ",
				matches[i].rm_eo - matches[i].rm_so,
				record + matches[i].rm_so);  
		if (count < 0)
			return -1;
		offset += count;
	}

	/* matched by a certain pattern, then parse the evevt */
	ret = e->match->parse(temp, e);

	dump_event(e);

	return ret;
}

int process_event(struct event *e)
{
	if (!e->valid)
		return -EINVAL;

	return e->match->process(e);
}

void dump_event(struct event *e)
{
	if (!e->valid)
		return;

	e->match->dump(e);
}
