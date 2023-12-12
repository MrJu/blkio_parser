#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <trace.h>

extern struct pattern patt[];

int __stub(int argc, char **argv);

int __stub(int argc, char **argv)
{
	int ret;
	char record[512];
	FILE *f;
	struct event *e;

	ret = regex_init(patt);
	if (ret)
		return ret;

	f = fopen("trace.txt", "r");
	if (!f) {
		printf("Error! opening file");
		return 1;
	}

	while (fgets(record, sizeof(record), f)) {
		e = malloc(sizeof(*e));
		if (!e)
			return -ENOMEM;

		memset(e, 0, sizeof(*e));

		parse_event(record, patt, e);
		ret = process_event(e);
		if (ret)
			free(e);
	}

	// test();

	process_post();

	fclose(f);

	regex_free(patt);

	return 0;
}

_init_t _func1 __section = {__stub, "func1"};
_init_t _func2 __section = {__stub, "func2"};
