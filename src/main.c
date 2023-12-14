#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <trace.h>

_init_t __app_init_start__;
_init_t __app_init_end__;

int main(int argc, char **argv)
{
	_init_t *p;

	if (argc < 3) {
		printf("Usage: %s <app> <filename>\n", argv[0]);
		printf("app list:\n");
		for (p = &__app_init_start__; p < &__app_init_end__; p++)
			printf("  %s\n", p->name);

		return 1;
	}

	for (p = &__app_init_start__; p < &__app_init_end__; p++) {
		if (!strcmp(argv[1], p->name))
			return p->func(argc, argv);
	}

	printf("not app found, app list:");
	for (p = &__app_init_start__; p < &__app_init_end__; p++)
		printf("  %s\n", p->name);

	return 0;
}
