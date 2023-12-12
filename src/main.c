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

        for(p = &__app_init_start__; p < &__app_init_end__; p++){
                printf("==%s\n", p->name);
                p->func(argc, argv);
        }

	return 0;
}
