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

/* 
 * block:block_rq_remap
 * block:block_bio_remap
 * block:block_split
 * block:block_unplug
 * block:block_plug
 * block:block_getrq
 * block:block_bio_queue
 * block:block_bio_frontmerge
 * block:block_bio_backmerge
 * block:block_bio_bounce
 * block:block_bio_complete
 * block:block_rq_merge
 * block:block_rq_issue
 * block:block_rq_insert
 * block:block_rq_complete
 * block:block_rq_requeue
 * block:block_dirty_buffer
 * block:block_touch_buffer
 */

LIST_HEAD(queue_list);
LIST_HEAD(device_list);
LIST_HEAD(task_list);

static int count0 = 0;
static int count1 = 0;
static int line_count = 0;

struct io {
	struct list_head entry;
	struct list_head event;
	struct {
		int rw;
		int mark;
		unsigned long long sector;
		unsigned long long nr_sector;
		unsigned long long int nr_sector_origin;
		struct {
			unsigned long long q;
			unsigned long long g;
			unsigned long long m;
			unsigned long long i;
			unsigned long long d;
			unsigned long long c;
		} time;
	} account;
};

struct group {
	char *name;
	struct {
		unsigned long long q2q_acc_origin, q2q_max_origin,
				q2q_avg_origin, q2q_min_origin, q2q_iter_origin;
		unsigned long long q2q_acc, q2q_max, q2q_avg, q2q_min, q2q_iter;
		unsigned long long q2g_acc, q2g_max, q2g_avg, q2g_min;
		unsigned long long q2c_acc, q2c_max, q2c_avg, q2c_min;
		unsigned long long d2c_acc, d2c_max, d2c_avg, d2c_min;
		unsigned long long nr_sector_max, nr_sector_avg, nr_sector_min;
		unsigned long long nr_queue_origin, nr_queue_sector_acc_origin;
		unsigned long long nr_queue, nr_queue_sector_acc;
		unsigned long long nr_split, nr_split_sector_acc;
		unsigned long long nr_merge, nr_merge_sector_acc;
		unsigned long long nr_insert, nr_insert_sector_acc;
		unsigned long long nr_issue, nr_issue_sector_acc;
		unsigned long long nr_complete, nr_complete_sector_acc;
		unsigned long long nr_q2q_origin, nr_q2q, nr_q2g, nr_q2c, nr_d2c;
	} summary;
};

struct device {
	struct list_head entry;
	struct list_head io;
	unsigned int major;
	unsigned int minor;
	struct group r, w, rw;
};

/* for events parsed by the same method, regard Q S G M as group Q */
int parse_event_group_q(const char *buf, struct event *e)
{
	int ret;
	double time;
	struct block_event *be = (struct block_event *) e;

	ret = sscanf(buf, "%u %u %lf %u %u %s %llu %llu %s",
			&e->pid, &e->cpu, &time, &be->major, &be->minor,
			be->rwbs, &be->sector, &be->nr_sector, be->comm);

	if (ret == e->match->nr_args) {
		be->type = e->match->type;
		e->time = (unsigned long long) (time *1000000);
		e->valid = 1;
	} else
		e->valid = 0;

	return e->valid;
}

int parse_event_queue(const char *buf, struct event *e)
{
	return parse_event_group_q(buf, e);
}

int parse_event_split(const char *buf, struct event *e)
{
	return parse_event_group_q(buf, e);
}

int parse_event_getrq(const char *buf, struct event *e)
{
	return parse_event_group_q(buf, e);
}

int parse_event_merge(const char *buf, struct event *e)
{
	return parse_event_group_q(buf, e);
}

/* for events parsed by the same method, regard I D as group Q */
int parse_event_group_i(const char *buf, struct event *e)
{
	int ret;
	double time;
	struct block_event *be = (struct block_event *) e;

	ret = sscanf(buf, "%u %u %lf %u %u %s %llu %llu %llu %s",
			&e->pid, &e->cpu, &time, &be->major, &be->minor,
			be->rwbs, &be->bytes, &be->sector, &be->nr_sector, be->comm);

	if (ret == e->match->nr_args) {
		be->type = e->match->type;
		e->time = (unsigned long long) (time *1000000);
		e->valid = 1;
	} else
		e->valid = 0;

	return e->valid;
}

int parse_event_insert(const char *buf, struct event *e)
{
	return parse_event_group_i(buf, e);
}

int parse_event_issue(const char *buf, struct event *e)
{
	return parse_event_group_i(buf, e);
}

int parse_event_complete(const char *buf, struct event *e)
{
	int ret;
	double time;
	struct block_event *be = (struct block_event *) e;

	ret = sscanf(buf, "%u %u %lf %u %u %s %llu %llu %d",
			&e->pid, &e->cpu, &time, &be->major, &be->minor,
			be->rwbs, &be->sector, &be->nr_sector, &be->error);

	if (ret == e->match->nr_args) {
		be->type = e->match->type;
		e->time = (unsigned long long) (time *1000000);
		e->valid = 1;
	} else
		e->valid = 0;

	return e->valid;
}

struct device *search_for_device(struct block_event *be, struct list_head *head)
{
	struct device *dev;
	
	list_for_each_entry(dev, head, entry) {
		if (dev->major == be->major && dev->minor == be->minor)
			return dev;
	}

	dev = malloc(sizeof(*dev));
	if (!dev) {
		printf("dev malloc failed");
		return NULL;
	}

	memset(dev, 0, sizeof(*dev));

	dev->major = be->major;
	dev->minor = be->minor;
	dev->r.name = "r";
	dev->w.name = "w";
	dev->rw.name = "rw";
	INIT_LIST_HEAD(&dev->entry);
	INIT_LIST_HEAD(&dev->io);

	list_add_tail(&dev->entry, head);

	return dev;
}

static struct io *search_for_io(struct block_event *be, struct list_head *head)
{
	struct io *io;
	
	list_for_each_entry(io, head, entry) {
		if (io->account.sector == be->sector)
			return io;
	}

	return NULL;
}

int is_read(struct block_event *be)
{
	return !!strstr(be->rwbs, "R");
}

int is_write(struct block_event *be)
{
	return !!strstr(be->rwbs, "W");
}

int process_event_queue(struct event *e)
{
	struct io *io;
	struct block_event *be = (struct block_event *) e;

	io = malloc(sizeof(*io));
	if (!io)
		return -ENOMEM;

	memset(io, 0, sizeof(*io));
	INIT_LIST_HEAD(&io->entry);
	INIT_LIST_HEAD(&io->event);

	io->account.rw = is_read(be) | (is_write(be) << 1);
	io->account.mark |= (1 << (be->type - 'A'));
	io->account.sector = be->sector;
	io->account.nr_sector_origin = be->nr_sector;
	io->account.nr_sector = be->nr_sector;
	io->account.time.q = e->time;

	INIT_LIST_HEAD(&be->entry);
	list_add_tail(&be->entry, &io->event);
	list_add_tail(&io->entry, &queue_list);

	return 0;
}

int process_event_split(struct event *e)
{
	struct io *origin, *split;
	struct block_event *q;
	struct block_event *be = (struct block_event *) e;

	origin = search_for_io(be, &queue_list);
	if (!origin)
		return -ENOENT;

	q = list_entry(origin->event.next, struct block_event, entry);

	/*
	 * dd-158     [003] .....   152.372240: block_bio_queue: 254,0 WS 397640 + 3768 [dd]
	 * dd-158     [003] .....   152.374022: block_split:     254,0 WS 397640 / 400200 [dd]`
	 * */
	be->sector = be->nr_sector;
	be->nr_sector = q->nr_sector - (be->sector - q->sector);
	q->nr_sector = be->sector - q->sector;
	origin->account.nr_sector = q->nr_sector;

	split = malloc(sizeof(*split));
	if (!split)
		return -ENOMEM;

	memset(split, 0, sizeof(*split));
	INIT_LIST_HEAD(&split->entry);
	INIT_LIST_HEAD(&split->event);

	split->account.rw = is_read(be) | (is_write(be) << 1);
	split->account.mark |= (1 << (be->type - 'A'));
	split->account.sector = be->sector;
	split->account.nr_sector = be->nr_sector;
	split->account.time.q = e->time;

	INIT_LIST_HEAD(&be->entry);
	list_add_tail(&be->entry, &split->event);
	list_add_tail(&split->entry, &queue_list);

	return 0;
}

int process_event_getrq(struct event *e)
{
	struct io *io;
	struct block_event *be = (struct block_event *) e;

	io = search_for_io(be, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (be->type - 'A'));
	io->account.time.g = e->time;

	INIT_LIST_HEAD(&be->entry);
	list_add_tail(&be->entry, &io->event);

	return 0;
}

int process_event_merge(struct event *e)
{
	struct io *io;
	struct device *dev;
	struct block_event *be = (struct block_event *) e;

	io = search_for_io(be, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (be->type - 'A'));
	io->account.time.m = e->time;

	INIT_LIST_HEAD(&be->entry);
	list_add_tail(&be->entry, &io->event);

	dev = search_for_device(be, &device_list);
	if (!dev)
		return -ENODEV;

	list_move_tail(&io->entry, &dev->io);

	return 0;
}

int process_event_insert(struct event *e)
{
	struct io *io;
	struct block_event *be = (struct block_event *) e;

	io = search_for_io(be, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (be->type - 'A'));
	io->account.time.i = e->time;

	INIT_LIST_HEAD(&be->entry);
	list_add_tail(&be->entry, &io->event);

	return 0;
}

int process_event_issue(struct event *e)
{
	struct io *io;
	struct block_event *be = (struct block_event *) e;

	io = search_for_io(be, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (be->type - 'A'));
	io->account.time.d = e->time;

	INIT_LIST_HEAD(&be->entry);
	list_add_tail(&be->entry, &io->event);

	return 0;
}

int process_event_complete(struct event *e)
{
	struct io *io;
	struct device *dev;
	struct block_event *be = (struct block_event *) e;

	io = search_for_io(be, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (be->type - 'A'));
	io->account.time.c = e->time;

	INIT_LIST_HEAD(&be->entry);
	list_add_tail(&be->entry, &io->event);

	dev = search_for_device(be, &device_list);
	if (!dev)
		return -ENODEV;

	list_move_tail(&io->entry, &dev->io);

	return 0;
}

/* for events parsed by the same method, regard Q S G M as group Q */
void dump_event_group_q(struct event *e)
{
	struct block_event *be = (struct block_event *) e;

	printf("%c: pid:%u cpu:%u time:%llu major:%u minor:%u rwbs:%s " \
			"sector:%llu nr_sector:%llu comm:%s\n",
			be->type, e->pid, e->cpu, e->time,
			be->major, be->minor, be->rwbs,
			be->sector, be->nr_sector, be->comm);
}

void dump_event_queue(struct event *e)
{
	return dump_event_group_q(e);
}

void dump_event_split(struct event *e)
{
	return dump_event_group_q(e);
}

void dump_event_getrq(struct event *e)
{
	return dump_event_group_q(e);
}

void dump_event_merge(struct event *e)
{
	return dump_event_group_q(e);
}

/* for events parsed by the same method, regard I D as group Q */
void dump_event_group_i(struct event *e)
{
	struct block_event *be = (struct block_event *) e;

	printf("%c: pid:%u cpu:%u time:%llu major:%u minor:%u rwbs:%s " \
			"bytes:%llu sector:%llu nr_sector:%llu comm:%s\n",
			be->type, e->pid, e->cpu, e->time,
			be->major, be->minor, be->rwbs, be->bytes,
			be->sector, be->nr_sector, be->comm);
}

void dump_event_insert(struct event *e)
{
	return dump_event_group_i(e);
}

void dump_event_issue(struct event *e)
{
	return dump_event_group_i(e);
}

void dump_event_complete(struct event *e)
{
	struct block_event *be = (struct block_event *) e;

	printf("%c: pid:%u cpu:%u time:%llu major:%u minor:%u rwbs:%s " \
			"sector:%llu nr_sector:%llu error:%d\n",
			be->type, e->pid, e->cpu, e->time,
			be->major, be->minor, be->rwbs,
			be->sector, be->nr_sector, be->error);
}

struct pattern patt[] = {
	{
		.type = 'G',
		.expr =  EXPR_G,
		.nr_args= 9,
		.regex = {},
		.name = "block_getrq",
		.parse = parse_event_getrq,
		.process = process_event_getrq,
		.dump = dump_event_getrq,
	},
	{
		.type = 'D',
		.expr =  EXPR_D,
		.nr_args = 10,
		.regex = {},
		.name = "block_rq_issue",
		.parse = parse_event_issue,
		.process = process_event_issue,
		.dump = dump_event_issue,
	},
	{
		.type = 'C',
		.expr =  EXPR_C,
		.nr_args = 9,
		.regex = {},
		.name = "block_rq_complete",
		.parse = parse_event_complete,
		.process = process_event_complete,
		.dump = dump_event_complete,

	},
	{
		.type = 'S',
		.expr =  EXPR_S,
		.nr_args = 9,
		.regex = {},
		.name = "block_split",
		.parse = parse_event_split,
		.process = process_event_split,
		.dump = dump_event_split,
	},
	{
		.type = 'I',
		.expr =  EXPR_I,
		.nr_args = 10,
		.regex = {},
		.name = "block_rq_insert",
		.parse = parse_event_insert,
		.process = process_event_insert,
		.dump = dump_event_insert,
	},
	{
		.type = 'Q',
		.expr =  EXPR_Q,
		.nr_args = 9,
		.regex = {},
		.name = "block_bio_queue",
		.parse = parse_event_queue,
		.process = process_event_queue,
		.dump = dump_event_queue,
	},
	{
		.type = 'M',
		.expr =  EXPR_M,
		.nr_args = 9,
		.regex = {},
		.name = "block_bio_backmerge",
		.parse = parse_event_merge,
		.process = process_event_merge,
		.dump = dump_event_merge,
	},
	{
		.type = 0,
		.expr = NULL,
	},
};

int is_marked(struct io *io, char mark)
{
	return !!(io->account.mark & (1 << (mark - 'A')));
}

void update_summary_q2q_origin(struct group *group, struct io *io)
{
	double q2q;

	if (!is_marked(io, 'Q'))
		return;

	/* only the 1st queue */ 
	if (!group->summary.q2q_iter_origin) {
		/* set q2q_min the time of 1st queue for initializaton */
		group->summary.q2q_min_origin = io->account.time.q;
		/* save the the time of 1st queue for the next cycle */
		group->summary.q2q_iter_origin = io->account.time.q;
		/* do nothing else for the 1st queue */
		return;
	}

	/* obtain the diff between last and this queue */
	q2q = io->account.time.q - group->summary.q2q_iter_origin;
	/* update q2q_iter for the next cycle */
	group->summary.q2q_iter_origin = io->account.time.q;

	group->summary.nr_q2q_origin++;
	group->summary.q2q_acc_origin += q2q;

	if (q2q > group->summary.q2q_max_origin)
		group->summary.q2q_max_origin = q2q;

	if (q2q < group->summary.q2q_min_origin)
		group->summary.q2q_min_origin = q2q;

	group->summary.q2q_avg_origin
		= group->summary.q2q_acc_origin / group->summary.nr_q2q_origin;
}



void update_summary_q2q(struct group *group, struct io *io)
{
	double q2q;

	if (!is_marked(io, 'Q') && !is_marked(io, 'S'))
		return;

	/* only the 1st queue */ 
	if (!group->summary.q2q_iter) {
		/* set q2q_min the time of 1st queue for initializaton */
		group->summary.q2q_min = io->account.time.q;
		/* save the the time of 1st queue for the next cycle */
		group->summary.q2q_iter = io->account.time.q;
		/* do nothing else for the 1st queue */
		return;
	}

	/* obtain the diff between last and this queue */
	q2q = io->account.time.q - group->summary.q2q_iter;
	/* update q2q_iter for the next cycle */
	group->summary.q2q_iter = io->account.time.q;

	group->summary.nr_q2q++;
	group->summary.q2q_acc += q2q;

	if (q2q > group->summary.q2q_max)
		group->summary.q2q_max = q2q;

	if (q2q < group->summary.q2q_min)
		group->summary.q2q_min = q2q;

	group->summary.q2q_avg
		= group->summary.q2q_acc / group->summary.nr_q2q;
}

void update_summary_q2g(struct group *group, struct io *io)
{
	double q2g;

	if (!is_marked(io, 'G') || (!is_marked(io, 'Q') && !is_marked(io, 'S')))
		return;

	q2g = io->account.time.g - io->account.time.q;

	/* for the first time */
	if (!group->summary.nr_q2g)
		group->summary.q2g_min = q2g;

	group->summary.nr_q2g++;
	group->summary.q2g_acc += q2g;

	if (q2g > group->summary.q2g_max)
		group->summary.q2g_max = q2g;

	if (q2g < group->summary.q2g_min)
		group->summary.q2g_min = q2g;

	group->summary.q2g_avg
		= group->summary.q2g_acc / group->summary.nr_q2g;
}

void update_summary_q2c(struct group *group, struct io *io)
{
	double q2c;

	if (!is_marked(io, 'C') || (!is_marked(io, 'Q') && !is_marked(io, 'S')))
		return;

	q2c = io->account.time.c - io->account.time.q;

	/* for the first time */
	if (!group->summary.nr_q2c)
		group->summary.q2c_min = q2c;

	group->summary.nr_q2c++;
	group->summary.q2c_acc += q2c;

	if (q2c > group->summary.q2c_max)
		group->summary.q2c_max = q2c;

	if (q2c < group->summary.q2c_min)
		group->summary.q2c_min = q2c;

	group->summary.q2c_avg
		= group->summary.q2c_acc / group->summary.nr_q2c;
}

void update_summary_d2c(struct group *group, struct io *io)
{
	double d2c;

	if (!is_marked(io, 'C') || !is_marked(io, 'D'))
		return;

	d2c = io->account.time.c - io->account.time.d;

	/* for the first time */
	if (!group->summary.nr_d2c)
		group->summary.d2c_min = d2c;

	group->summary.nr_d2c++;
	group->summary.d2c_acc += d2c;

	if (d2c > group->summary.d2c_max)
		group->summary.d2c_max = d2c;

	if (d2c < group->summary.d2c_min)
		group->summary.d2c_min = d2c;

	group->summary.d2c_avg
		= group->summary.d2c_acc / group->summary.nr_d2c;
}

void update_summary_queue_origin(struct group *group, struct io *io)
{
	if (!is_marked(io, 'Q'))
		return;

	group->summary.nr_queue_origin++;
	group->summary.nr_queue_sector_acc_origin += io->account.nr_sector_origin;
}

void update_summary_queue(struct group *group, struct io *io)
{
	if ((!is_marked(io, 'Q') && !is_marked(io, 'S')))
		return;

	/* for the first time */
	if (!group->summary.nr_queue)
		group->summary.nr_sector_min = io->account.nr_sector;

	group->summary.nr_queue++;
	group->summary.nr_queue_sector_acc += io->account.nr_sector;

	if (io->account.nr_sector > group->summary.nr_sector_max)
		group->summary.nr_sector_max = io->account.nr_sector;

	if (io->account.nr_sector < group->summary.nr_sector_min)
		group->summary.nr_sector_min = io->account.nr_sector;

	group->summary.nr_sector_avg
		= group->summary.nr_queue_sector_acc / group->summary.nr_queue;
}

void update_summary_split(struct group *group, struct io *io)
{
	if ((!is_marked(io, 'S')))
		return;

	group->summary.nr_split++;
	group->summary.nr_split_sector_acc += io->account.nr_sector;
}

void update_summary_merge(struct group *group, struct io *io)
{
	if ((!is_marked(io, 'M')))
		return;

	group->summary.nr_merge++;
	group->summary.nr_merge_sector_acc += io->account.nr_sector;
}

void update_summary_insert(struct group *group, struct io *io)
{
	if ((!is_marked(io, 'I')))
		return;

	group->summary.nr_insert++;
	group->summary.nr_insert_sector_acc += io->account.nr_sector;
}

void update_summary_issue(struct group *group, struct io *io)
{
	if ((!is_marked(io, 'D')))
		return;

	group->summary.nr_issue++;
	group->summary.nr_issue_sector_acc += io->account.nr_sector;
}

void update_summary_complete(struct group *group, struct io *io)
{
	if ((!is_marked(io, 'C')))
		return;

	group->summary.nr_complete++;
	group->summary.nr_complete_sector_acc += io->account.nr_sector;
}

#define update_summary(dev, grp, io) \
	__update_summary(&(dev->grp), io)

void __update_summary(struct group *grp, struct io *io)
{
	if (!strcmp(grp->name, "r") && !(io->account.rw & (1 << 0)))
		return;
	else if (!strcmp(grp->name, "w") && !(io->account.rw & (1 << 1)))
		return;
	else if (!strcmp(grp->name, "rw") && !io->account.rw)
		return;

	update_summary_queue_origin(grp, io);
	update_summary_queue(grp, io);
	update_summary_split(grp, io);
	update_summary_merge(grp, io);
	update_summary_insert(grp, io);
	update_summary_issue(grp, io);
	update_summary_complete(grp, io);

	update_summary_q2q_origin(grp, io);
	update_summary_q2q(grp, io);
	update_summary_q2g(grp, io);
	update_summary_q2c(grp, io);
	update_summary_d2c(grp, io);
}

#define __maybe_unused(arg) ((void) arg)

static int compare(void *priv, const struct list_head *a, const struct list_head *b)
{
	__maybe_unused(priv);
	struct io *io_a = list_entry(a, struct io, entry);
	struct io *io_b = list_entry(b, struct io, entry);

	return io_a->account.time.q - io_b->account.time.q;
}

void process_post(void)
{
	struct device *dev;
	struct io *io;

	list_for_each_entry(dev, &device_list, entry) {
		list_sort(NULL, &dev->io, compare);
		list_for_each_entry(io, &dev->io, entry) {
			update_summary(dev, r, io);
			update_summary(dev, w, io);
			update_summary(dev, rw, io);
		}
	}

	list_for_each_entry(dev, &device_list, entry) {
#define DEV(dev) dev->major, dev->minor
#define X(dev, group, x) dev->group.summary.x

		printf("\n=================================== NR event ===================================\n");
		printf("DEV       Group     #Q_origin #Q        #S        #M        #D        #C        Ratio\n");
		/*    major, minor  rw    #Q      #S      #M      #D      #C      Ratio    */
		printf("%-3d, %-3d  %-8s  %-8llu  %-8llu  %-8llu  %-8llu  %-8llu  %-8llu  %-7.3lf\n",
				DEV(dev),
				"rw",
				X(dev, rw, nr_queue_origin),
				X(dev, rw, nr_queue),
				X(dev, rw, nr_split),
				X(dev, rw, nr_merge),
				X(dev, rw, nr_issue),
				X(dev, rw, nr_complete),
				X(dev, rw, nr_queue) ? ((double)X(dev, rw, nr_queue)) / X(dev, rw, nr_issue) : 0
		);

		printf("\n====================================== NR sector =======================================\n");
		printf("DEV       Group     BLKmin    BLKavg    BLKmax    Queue     Split     Merge     Complete\n");
		/*    major, minor  rw    BLKmin  BLKavg  BLKmax  Queue   Split   Merge   Complete  */
		printf("%-3d, %-3d  %-8s  %-8llu  %-8llu  %-8llu  %-8llu  %-8llu  %-8llu  %-8llu  \n",
				DEV(dev),
				"rw",
				X(dev, rw, nr_sector_min),
				X(dev, rw, nr_sector_avg),
				X(dev, rw, nr_sector_max),
				X(dev, rw, nr_queue_sector_acc),
				X(dev, rw, nr_split_sector_acc),
				X(dev, rw, nr_merge_sector_acc),
				X(dev, rw, nr_complete_sector_acc)
		);

		printf("\n======================================= Time Span ========================================\n");
		printf("DEV       Group     X2X           Max         Avg         Min         Acc      Nr        \n");
		/*    major, minor  rw    X2X   Max      Avg      Min      Acc      Nr        */
		printf("%-3d, %-3d  %-8s  %-10s  %10.6lf  %10.6lf  %10.6lf  %10.6lf %-8llu  \n",
				DEV(dev),
				"rw",
				"Q2Q",
				X(dev, rw, q2q_max) / 1000000.0,
				X(dev, rw, q2q_avg) / 1000000.0,
				X(dev, rw, q2q_min) / 1000000.0,
				X(dev, rw, q2q_acc) / 1000000.0,
				X(dev, rw, nr_q2q)
		);

		printf("%-3d, %-3d  %-8s  %-10s  %10.6lf  %10.6lf  %10.6lf  %10.6lf %-8llu  \n",
				DEV(dev),
				"rw",
				"Q2G",
				X(dev, rw, q2g_max) / 1000000.0,
				X(dev, rw, q2g_avg) / 1000000.0,
				X(dev, rw, q2g_min) / 1000000.0,
				X(dev, rw, q2g_acc) / 1000000.0,
				X(dev, rw, nr_q2g)
		);

		printf("%-3d, %-3d  %-8s  %-10s  %10.6lf  %10.6lf  %10.6lf  %10.6lf %-8llu  \n",
				DEV(dev),
				"rw",
				"Q2C",
				X(dev, rw, q2c_max) / 1000000.0,
				X(dev, rw, q2c_avg) / 1000000.0,
				X(dev, rw, q2c_min) / 1000000.0,
				X(dev, rw, q2c_acc) / 1000000.0,
				X(dev, rw, nr_q2c)
		);

		printf("%-3d, %-3d  %-8s  %-10s  %10.6lf  %10.6lf  %10.6lf  %10.6lf %-8llu  \n",
				DEV(dev),
				"rw",
				"D2C",
				X(dev, rw, d2c_max) / 1000000.0,
				X(dev, rw, d2c_avg) / 1000000.0,
				X(dev, rw, d2c_min) / 1000000.0,
				X(dev, rw, d2c_acc) / 1000000.0,
				X(dev, rw, nr_d2c)
		);

		printf("%-3d, %-3d  %-8s  %-10s  %10.6lf  %10.6lf  %10.6lf  %10.6lf %-8llu  \n",
				DEV(dev),
				"rw",
				"Q2Q_origin",
				X(dev, rw, q2q_max_origin) / 1000000.0,
				X(dev, rw, q2q_avg_origin) / 1000000.0,
				X(dev, rw, q2q_min_origin) / 1000000.0,
				X(dev, rw, q2q_acc_origin) / 1000000.0,
				X(dev, rw, nr_q2q_origin)
		);
	}
}

void test(void)
{
	unsigned long long d2c = 0;
	int d2c_count = 0;
	unsigned long long blocks = 0;
	struct block_event *be;
	struct io *io;
	struct device *dev;


	printf("count0:%d count1:%d line_count:%d\n", count0, count1, line_count);

	list_for_each_entry(dev, &device_list, entry) {
		list_for_each_entry(io, &dev->io, entry) {
			be = list_entry(io->event.next, struct block_event, entry);
			printf("%c: cpu:%d time:%llu\n", be->type, be->e.cpu, be->e.time);
			blocks += be->nr_sector;
		}
	}

	list_for_each_entry(dev, &device_list, entry) {
		list_for_each_entry(io, &dev->io, entry) {
			d2c += (io->account.time.c - io->account.time.d);
			printf("%u,%u d2c:%llu c:%llu d:%llu q:%llu\n", dev->major, dev->minor,
					d2c, io->account.time.c, io->account.time.d, io->account.time.q);
			d2c_count++;
		}
	}

	printf("blocks: %llu size:%llu MB d2c:%llu\n", blocks, blocks * 512 / 1024 / 1024, d2c / d2c_count);
}

int __stub(int argc, char **argv)
{
	int ret;
	char record[512];
	FILE *f;
	struct block_event *be;

	ret = regex_init(patt);
	if (ret)
		return ret;

	f = fopen(argv[2], "r");
	if (!f) {
		printf("Error! opening file\n");
		return 1;
	}

	while (fgets(record, sizeof(record), f)) {
		be = malloc(sizeof(*be));
		if (!be)
			return -ENOMEM;

		memset(be, 0, sizeof(*be));

		parse_event(record, patt, &be->e);
		ret = process_event(&be->e);
		if (ret)
			free(be);
	}

	// test();

	process_post();

	fclose(f);

	regex_free(patt);

	return 0;
}

INSTALL_APP(__stub, block);
