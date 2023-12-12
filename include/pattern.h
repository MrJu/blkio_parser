#ifndef __PATTERN_H
#define __PATTERN_H

#include <trace.h>

#define __EXPR_G "block_getrq: ([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]"
#define EXPR_G (EXPR_COMMON __EXPR_G)

#define __EXPR_D "block_rq_issue: ([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\(\\) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]"
#define EXPR_D (EXPR_COMMON __EXPR_D)

#define __EXPR_C "block_rq_complete: ([0-9]+),([0-9]+) (\\w+) \\(.*\\) ([0-9]+) \\+ ([0-9]+) \\[([0-9]+)\\]"
#define EXPR_C (EXPR_COMMON __EXPR_C)

#define __EXPR_S "block_split: ([0-9]+),([0-9]+) (\\w+) ([0-9]+) / ([0-9]+) \\[(.+)\\]"
#define EXPR_S (EXPR_COMMON __EXPR_S)

#define __EXPR_I "block_rq_insert: ([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\(\\) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]"
#define EXPR_I (EXPR_COMMON __EXPR_I)

#define __EXPR_Q "block_bio_queue: ([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]"
#define EXPR_Q (EXPR_COMMON __EXPR_Q)

#define __EXPR_M "block_bio_backmerge: ([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]"
#define EXPR_M (EXPR_COMMON __EXPR_M)

#endif /* __PATTERN_H */
