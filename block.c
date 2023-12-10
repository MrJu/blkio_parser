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

#define MAX_MATCHES 16
#define TASK_COMM_LEN 16

LIST_HEAD(queue_list);
LIST_HEAD(device_list);
LIST_HEAD(task_list);

static int count0 = 0;
static int count1 = 0;
static int line_count = 0;

struct pattern {
	char type;
	const char *expr;
	int count;
	regex_t regex;
	const char *event;
};

struct event {
	struct list_head entry;
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

struct device {
	struct list_head entry;
	struct list_head io;
	unsigned int major;
	unsigned int minor;

	struct summary {
		struct domain {
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
			unsigned long long nr_complete, nr_complete_sector_acc;
			unsigned long long nr_q2q_origin, nr_q2q, nr_q2g, nr_q2c, nr_d2c;
		} rw, r, w;
	} summary;
};

struct pattern patterns[] = {
	{
		.type = 'G',
    		.expr = ".+-([0-9]+) +\\[([0-9]{3})\\].+ ([0-9]+\\.[0-9]{6}): block_getrq: " \
			 	"([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]",
		.count = 9,
		.regex = NULL,
		.event = "block_getrq"
	},
	{
		.type = 'D',
		.expr = ".+-([0-9]+) +\\[([0-9]{3})\\].+ ([0-9]+\\.[0-9]{6}): block_rq_issue: " \
			 	"([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\(\\) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]",
		.count = 10,
		.regex = NULL,
		.event = "block_rq_issue"
	},
	{
		.type = 'C',
    		.expr = ".+-([0-9]+) +\\[([0-9]{3})\\].+ ([0-9]+\\.[0-9]{6}): block_rq_complete: " \
			 	"([0-9]+),([0-9]+) (\\w+) \\(.*\\) ([0-9]+) \\+ ([0-9]+) \\[([0-9]+)\\]",
		.count = 9,
		.regex = NULL,
		.event = "block_rq_complete"

	},
	{
		.type = 'S',
    		.expr = ".+-([0-9]+) +\\[([0-9]{3})\\].+ ([0-9]+\\.[0-9]{6}): block_split: " \
			 	"([0-9]+),([0-9]+) (\\w+) ([0-9]+) / ([0-9]+) \\[(.+)\\]",
		.count = 9,
		.regex = NULL,
		.event = "block_split"
	},
	{
		.type = 'I',
    		.expr = ".+-([0-9]+) +\\[([0-9]{3})\\].+ ([0-9]+\\.[0-9]{6}): block_rq_insert: "  \
			 	"([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\(\\) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]",
		.count = 10,
		.regex = NULL,
		.event = "block_rq_insert"
	},
	{
		.type = 'Q',
		.expr = ".+-([0-9]+) +\\[([0-9]{3})\\].+ ([0-9]+\\.[0-9]{6}): block_bio_queue: " \
			 	"([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]",
		.count = 9,
		.regex = NULL,
		.event = "block_bio_queue"
	},
	{
		.type = 'M',
		.expr = ".+-([0-9]+) +\\[([0-9]{3})\\].+ ([0-9]+\\.[0-9]{6}): block_bio_backmerge: " \
			 	"([0-9]+),([0-9]+) (\\w+) ([0-9]+) \\+ ([0-9]+) \\[(.+)\\]",
		.count = 9,
		.regex = NULL,
		.event = "block_bio_backmerge"
	},
	{
		.type = 0,
		.expr = NULL,
	}
};

void dump_event(struct event *e) {
	switch (e->type) {
		case 'Q':
		case 'S':
		case 'G':
		case 'M':
			printf("%c: pid:%u cpu:%u time:%llu " \
					"major:%u minor:%u rwbs:%s " \
					"sector:%llu nr_sector:%llu comm:%s\n",
					e->type, e->pid, e->cpu, e->time,
					e->major, e->minor, e->rwbs,
					e->sector, e->nr_sector, e->comm);
			break;
		case 'I':
		case 'D':
			printf("%c: pid:%u cpu:%u time:%llu " \
					"major:%u minor:%u rwbs:%s bytes:%llu " \
					"sector:%llu nr_sector:%llu comm:%s\n",
					e->type, e->pid, e->cpu, e->time,
					e->major, e->minor, e->rwbs, e->bytes,
					e->sector, e->nr_sector, e->comm);
			break;
		case 'C':
			printf("%c: pid:%u cpu:%u time:%llu " \
					"major:%u minor:%u rwbs:%s " \
					"sector:%llu nr_sector:%llu error:%d\n",
					e->type, e->pid, e->cpu, e->time,
					e->major, e->minor, e->rwbs,
					e->sector, e->nr_sector, e->error);
			break;
		default:
			printf("%c: not parsed\n", e->type);
	}
}

int parse_event(const char *record, struct event *e)
{
	int i, ret = 0;
	char temp[256];
	int count, offset = 0;
	regmatch_t matches[MAX_MATCHES];
	struct pattern *matched = NULL;
	double time;

	count0++;
	for (i = 0; patterns[i].expr; i++) {
		ret = regexec(&patterns[i].regex, record, MAX_MATCHES, matches, 0);
		if (!ret) {
			matched = &patterns[i];
			count1++;
			break;
		} else
			continue;
	}

	if (!matched) {
		// printf("== %s", record);	
		e->valid = 0;
		return 0;
	}

	for(i = 1, offset = 0; i < matched->count + 1; i++) {  
		if (matches[i].rm_so == -1)
			break;
		count = sprintf(temp + offset, "%.*s ",
				matches[i].rm_eo - matches[i].rm_so,
				record + matches[i].rm_so);  
		if (count < 0)
			return -1;
			/* err handling */;
		offset += count;
			// printf("temp: %s\n", temp);
	}

	// printf("temp: %s\n", temp);

	switch (matched->type) {
		case 'Q':
		case 'S':
		case 'G':
		case 'M':
			ret = sscanf(temp, "%u %u %lf %u %u %s %llu %llu %s",
					&e->pid, &e->cpu, &time, &e->major, &e->minor,
					e->rwbs, &e->sector, &e->nr_sector, e->comm);
			break;
		case 'I':
		case 'D':
			ret = sscanf(temp, "%u %u %lf %u %u %s %llu %llu %llu %s",
					&e->pid, &e->cpu, &time, &e->major, &e->minor,
					e->rwbs, &e->bytes, &e->sector, &e->nr_sector, e->comm);
			break;
		case 'C':
			ret = sscanf(temp, "%u %u %lf %u %u %s %llu %llu %d",
					&e->pid, &e->cpu, &time, &e->major, &e->minor,
					e->rwbs, &e->sector, &e->nr_sector, &e->error);
			break;
		default:
			e->valid = 0;
			printf("%c\n", matched->type);
	}

	if (ret == matched->count) {
		e->type = matched->type;
		e->time = (unsigned long long) (time *1000000);
		e->valid = 1;
	} else {
		e->valid = 0;
	}

	// dump_event(e);

	line_count++;

	return e->valid;
}

struct device *search_for_device(struct event *e, struct list_head *head)
{
	struct device *dev;
	
	list_for_each_entry(dev, head, entry) {
		if (dev->major == e->major && dev->minor == e->minor)
			return dev;
	}

	dev = malloc(sizeof(*dev));
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(*dev));

	dev->major = e->major;
	dev->minor = e->minor;
	INIT_LIST_HEAD(&dev->entry);
	INIT_LIST_HEAD(&dev->io);

	list_add_tail(&dev->entry, head);

	return dev;
}

static struct io *search_for_io(struct event *e, struct list_head *head)
{
	struct io *io;
	
	list_for_each_entry(io, head, entry) {
		if (io->account.sector == e->sector)
			return io;
	}

	return NULL;
}

int is_read(struct event *e)
{
	return !!strstr(e->rwbs, "R");
}

int is_write(struct event *e)
{
	return !!strstr(e->rwbs, "W");
}

int process_event_queue(struct event *e)
{
	struct io *io;

	io = malloc(sizeof(*io));
	if (!io)
		return -ENOMEM;

	memset(io, 0, sizeof(*io));
	INIT_LIST_HEAD(&io->entry);
	INIT_LIST_HEAD(&io->event);

	io->account.rw = is_read(e) | (is_write(e) << 1);
	io->account.mark |= (1 << (e->type - 'A'));
	io->account.sector = e->sector;
	io->account.nr_sector_origin = e->nr_sector;
	io->account.nr_sector = e->nr_sector;
	io->account.time.q = e->time;

	INIT_LIST_HEAD(&e->entry);
	list_add_tail(&e->entry, &io->event);
	list_add_tail(&io->entry, &queue_list);

	return 0;
}

int process_event_split(struct event *e)
{
	struct io *origin, *split;
	struct event *q;

	origin = search_for_io(e, &queue_list);
	if (!origin)
		return -ENOENT;

	q = list_entry(origin->event.next, struct event, entry);

	/*
	 * dd-158     [003] .....   152.372240: block_bio_queue: 254,0 WS 397640 + 3768 [dd]
	 * dd-158     [003] .....   152.374022: block_split:     254,0 WS 397640 / 400200 [dd]`
	 * */
	e->sector = e->nr_sector;
	e->nr_sector = q->nr_sector - (e->sector - q->sector);
	q->nr_sector = e->sector - q->sector;
	origin->account.nr_sector = q->nr_sector;

	split = malloc(sizeof(*split));
	if (!split)
		return -ENOMEM;

	memset(split, 0, sizeof(*split));
	INIT_LIST_HEAD(&split->entry);
	INIT_LIST_HEAD(&split->event);

	split->account.rw = is_read(e) | (is_write(e) << 1);
	split->account.mark |= (1 << (e->type - 'A'));
	split->account.sector = e->sector;
	split->account.nr_sector = e->nr_sector;
	split->account.time.q = e->time;

	INIT_LIST_HEAD(&e->entry);
	list_add_tail(&e->entry, &split->event);
	list_add_tail(&split->entry, &queue_list);

	return 0;
}

int process_event_getrq(struct event *e)
{
	struct io *io;

	io = search_for_io(e, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (e->type - 'A'));
	io->account.time.g = e->time;

	INIT_LIST_HEAD(&e->entry);
	list_add_tail(&e->entry, &io->event);

	return 0;
}

int process_event_merge(struct event *e)
{
	struct io *io;
	struct device *dev;

	io = search_for_io(e, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (e->type - 'A'));
	io->account.time.m = e->time;

	INIT_LIST_HEAD(&e->entry);
	list_add_tail(&e->entry, &io->event);

	dev = search_for_device(e, &device_list);
	if (!dev)
		return -ENODEV;

	list_move_tail(&io->entry, &dev->io);

	return 0;
}

int process_event_insert(struct event *e)
{
	struct io *io;

	io = search_for_io(e, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (e->type - 'A'));
	io->account.time.i = e->time;

	INIT_LIST_HEAD(&e->entry);
	list_add_tail(&e->entry, &io->event);

	return 0;
}

int process_event_issue(struct event *e)
{
	struct io *io;

	io = search_for_io(e, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (e->type - 'A'));
	io->account.time.d = e->time;

	INIT_LIST_HEAD(&e->entry);
	list_add_tail(&e->entry, &io->event);

	return 0;
}

int process_event_complete(struct event *e)
{
	struct io *io;
	struct device *dev;

	io = search_for_io(e, &queue_list);
	if (!io)
		return -ENOENT;

	io->account.mark |= (1 << (e->type - 'A'));
	io->account.time.c = e->time;

	INIT_LIST_HEAD(&e->entry);
	list_add_tail(&e->entry, &io->event);

	dev = search_for_device(e, &device_list);
	if (!dev)
		return -ENODEV;

	list_move_tail(&io->entry, &dev->io);

	return 0;
}

int process_event(struct event *e)
{
	if (!e->valid)
		return -EINVAL;
	
	switch (e->type) {
		case 'Q':
			return process_event_queue(e); 
		case 'S':
			return process_event_split(e);
		case 'G':
			return process_event_getrq(e);
		case 'M':
			return process_event_merge(e);
		case 'I':
			return process_event_insert(e);
		case 'D':
			return process_event_issue(e);
		case 'C':
			return process_event_complete(e);
		default:
			return -EINVAL;
	}

	return 0;
}

int regex_init(void)
{
	int i, ret;

	for (i = 0; patterns[i].expr; i++) {
		ret = regcomp(&patterns[i].regex, patterns[i].expr, REG_EXTENDED);
		if (ret) {
			fprintf(stderr, "compile regex pattern fail: %d\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

void regex_free(void)
{
	int i;

	for (i = 0; patterns[i].expr; i++) {
		regfree(&patterns[i].regex);
	}
}

int is_marked(struct io *io, char mark)
{
	return !!(io->account.mark & (1 << (mark - 'A')));
}

void update_summary_q2q_origin(struct device *dev, struct io *io)
{
	double q2q;

	if (!is_marked(io, 'Q'))
		return;

	/* only the 1st queue */ 
	if (!dev->summary.rw.q2q_iter_origin) {
		/* set q2q_min the time of 1st queue for initializaton */
		dev->summary.rw.q2q_min_origin = io->account.time.q;
		/* save the the time of 1st queue for the next cycle */
		dev->summary.rw.q2q_iter_origin = io->account.time.q;
		/* do nothing else for the 1st queue */
		printf("io->account.time.q:%llu dev->summary.rw.q2q_iter_origin:%llu\n", io->account.time.q, dev->summary.rw.q2q_iter_origin);
		return;
	}

	/* obtain the diff between last and this queue */
	q2q = io->account.time.q - dev->summary.rw.q2q_iter_origin;
	if (io->account.time.q < dev->summary.rw.q2q_iter_origin)
		printf("io->account.time.q:%.6lf dev->summary.rw.q2q_iter_origin:%.6lf q2q:%.6lf\n", io->account.time.q / 1000000.0, dev->summary.rw.q2q_iter_origin / 1000000.0, q2q / 1000000.0);
	/* update q2q_iter for the next cycle */
	dev->summary.rw.q2q_iter_origin = io->account.time.q;

	dev->summary.rw.nr_q2q_origin++;
	dev->summary.rw.q2q_acc_origin += q2q;

	if (q2q > dev->summary.rw.q2q_max_origin)
		dev->summary.rw.q2q_max_origin = q2q;

	if (q2q < dev->summary.rw.q2q_min_origin)
		dev->summary.rw.q2q_min_origin = q2q;

	dev->summary.rw.q2q_avg_origin
		= dev->summary.rw.q2q_acc_origin / dev->summary.rw.nr_q2q_origin;
}



void update_summary_q2q(struct device *dev, struct io *io)
{
	double q2q;

	if (!is_marked(io, 'Q') && !is_marked(io, 'S'))
		return;

	/* only the 1st queue */ 
	if (!dev->summary.rw.q2q_iter) {
		/* set q2q_min the time of 1st queue for initializaton */
		dev->summary.rw.q2q_min = io->account.time.q;
		/* save the the time of 1st queue for the next cycle */
		dev->summary.rw.q2q_iter = io->account.time.q;
		/* do nothing else for the 1st queue */
		printf("io->account.time.q:%llu dev->summary.rw.q2q_iter:%llu\n", io->account.time.q, dev->summary.rw.q2q_iter);
		return;
	}

	/* obtain the diff between last and this queue */
	q2q = io->account.time.q - dev->summary.rw.q2q_iter;
	if (io->account.time.q < dev->summary.rw.q2q_iter)
		printf("io->account.time.q:%.6lf dev->summary.rw.q2q_iter:%.6lf q2q:%.6lf\n", io->account.time.q / 1000000.0, dev->summary.rw.q2q_iter / 1000000.0, q2q / 1000000.0);
	/* update q2q_iter for the next cycle */
	dev->summary.rw.q2q_iter = io->account.time.q;

	dev->summary.rw.nr_q2q++;
	dev->summary.rw.q2q_acc += q2q;

	if (q2q > dev->summary.rw.q2q_max)
		dev->summary.rw.q2q_max = q2q;

	if (q2q < dev->summary.rw.q2q_min)
		dev->summary.rw.q2q_min = q2q;

	dev->summary.rw.q2q_avg
		= dev->summary.rw.q2q_acc / dev->summary.rw.nr_q2q;
}

void update_summary_q2g(struct device *dev, struct io *io)
{
	double q2g;

	if (!is_marked(io, 'G') || (!is_marked(io, 'Q') && !is_marked(io, 'S')))
		return;

	q2g = io->account.time.g - io->account.time.q;

	/* for the first time */
	if (!dev->summary.rw.nr_q2g)
		dev->summary.rw.q2g_min = q2g;

	dev->summary.rw.nr_q2g++;
	dev->summary.rw.q2g_acc += q2g;

	if (q2g > dev->summary.rw.q2g_max)
		dev->summary.rw.q2g_max = q2g;

	if (q2g < dev->summary.rw.q2g_min)
		dev->summary.rw.q2g_min = q2g;

	dev->summary.rw.q2g_avg
		= dev->summary.rw.q2g_acc / dev->summary.rw.nr_q2g;
}

void update_summary_q2c(struct device *dev, struct io *io)
{
	double q2c;

	if (!is_marked(io, 'C') || (!is_marked(io, 'Q') && !is_marked(io, 'S')))
		return;

	q2c = io->account.time.c - io->account.time.q;

	/* for the first time */
	if (!dev->summary.rw.nr_q2c)
		dev->summary.rw.q2c_min = q2c;

	dev->summary.rw.nr_q2c++;
	dev->summary.rw.q2c_acc += q2c;

	if (q2c > dev->summary.rw.q2c_max)
		dev->summary.rw.q2c_max = q2c;

	if (q2c < dev->summary.rw.q2c_min)
		dev->summary.rw.q2c_min = q2c;

	dev->summary.rw.q2c_avg
		= dev->summary.rw.q2c_acc / dev->summary.rw.nr_q2c;
}

void update_summary_d2c(struct device *dev, struct io *io)
{
	double d2c;

	if (!is_marked(io, 'C') || !is_marked(io, 'D'))
		return;

	d2c = io->account.time.c - io->account.time.d;

	/* for the first time */
	if (!dev->summary.rw.nr_d2c)
		dev->summary.rw.d2c_min = d2c;

	dev->summary.rw.nr_d2c++;
	dev->summary.rw.d2c_acc += d2c;

	if (d2c > dev->summary.rw.d2c_max)
		dev->summary.rw.d2c_max = d2c;

	if (d2c < dev->summary.rw.d2c_min)
		dev->summary.rw.d2c_min = d2c;

	dev->summary.rw.d2c_avg
		= dev->summary.rw.d2c_acc / dev->summary.rw.nr_d2c;
}

void update_summary_queue_origin(struct device *dev, struct io *io)
{
	if (!is_marked(io, 'Q'))
		return;

	dev->summary.rw.nr_queue_origin++;
	dev->summary.rw.nr_queue_sector_acc_origin += io->account.nr_sector_origin;
}

void update_summary_queue(struct device *dev, struct io *io)
{
	if ((!is_marked(io, 'Q') && !is_marked(io, 'S')))
		return;

	/* for the first time */
	if (!dev->summary.rw.nr_queue)
		dev->summary.rw.nr_sector_min = io->account.nr_sector;

	dev->summary.rw.nr_queue++;
	dev->summary.rw.nr_queue_sector_acc += io->account.nr_sector;

	if (io->account.nr_sector > dev->summary.rw.nr_sector_max)
		dev->summary.rw.nr_sector_max = io->account.nr_sector;

	if (io->account.nr_sector < dev->summary.rw.nr_sector_min)
		dev->summary.rw.nr_sector_min = io->account.nr_sector;

	dev->summary.rw.nr_sector_avg
		= dev->summary.rw.nr_queue_sector_acc / dev->summary.rw.nr_queue;
}

void update_summary_split(struct device *dev, struct io *io, struct domain *d)
{
	if ((!is_marked(io, 'S')))
		return;

	dev->summary.rw.nr_split++;
	dev->summary.rw.nr_split_sector_acc += io->account.nr_sector;
}

void update_summary_merge(struct device *dev, struct io *io)
{
	if ((!is_marked(io, 'M')))
		return;

	dev->summary.rw.nr_merge++;
	dev->summary.rw.nr_merge_sector_acc += io->account.nr_sector;
}

void update_summary_complete(struct device *dev, struct io *io)
{
	if ((!is_marked(io, 'C')))
		return;

	dev->summary.rw.nr_complete++;
	dev->summary.rw.nr_complete_sector_acc += io->account.nr_sector;
}

void update_rw_summary(struct device *dev, struct io *io)
{
/*
struct io {
	struct list_head entry;
	struct list_head event;
	struct {
		int rw;
		int mark;
		int sector;
		int nr_sector;
		int nr_sector_origin;
		struct {
			double q;
			double g;
			double m;
			double i;
			double d;
			double c;
		} time;
	} account;
};

struct device {
	struct list_head entry;
	struct list_head io;
	int major;
	int minor;

	struct {
		struct {
			double q2q_acc, q2q_max, q2q_avg, q2q_min;
			double q2g_acc, q2g_max, q2g_avg, q2g_min;
			double q2c_acc, q2c_max, q2c_avg, q2c_min;
			double d2c_acc, d2c_max, d2c_avg, d2c_min;

			int nr_sector_max, nr_sector_avg, nr_sector_min;
			int nr_queue_origin, nr_queue_sector_acc_origin;
			int nr_queue, nr_queue_sector_acc;
			int nr_split, nr_split_sector_acc;
			int nr_merge, nr_merge_sector_acc;
			int nr_complete, nr_complete_sector_acc;
			int nr_q2q, nr_q2g, nr_q2c, nr_d2c;
		} rw, r, w;
	} summary;
};
 */

	if (!io->account.rw)
		return;

	update_summary_queue_origin(dev, io);
	update_summary_queue(dev, io);
	update_summary_split(dev, io, NULL);
	update_summary_merge(dev, io);
	update_summary_complete(dev, io);

	update_summary_q2q_origin(dev, io);
	update_summary_q2q(dev, io);
	update_summary_q2g(dev, io);
	update_summary_q2c(dev, io);
	update_summary_d2c(dev, io);
}

__attribute__((unused))
void update_r_summary(struct device *dev, struct io *io)
{

}

__attribute__((unused))
void update_w_summary(struct device *dev, struct io *io)
{

}

static int compare(void *priv, const struct list_head *a, const struct list_head *b) {
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
			update_rw_summary(dev, io);
			update_r_summary(dev, io);
			update_w_summary(dev, io);
		}
	}

	list_for_each_entry(dev, &device_list, entry) {
		printf(" \
			%d,%d summary.rw.q2q_acc_origin:                     %.6lf\n \
			%d,%d summary.rw.q2q_max_origin:                     %.6lf\n \
			%d,%d summary.rw.q2q_min_origin:                     %.6lf\n \
			%d,%d summary.rw.q2q_avg_origin:                     %.6lf\n \
			%d,%d summary.rw.nr_q2q_origin:                      %llu\n \
			%d,%d summary.rw.q2q_acc:                            %.6lf\n \
			%d,%d summary.rw.q2q_max:                            %.6lf\n \
			%d,%d summary.rw.q2q_min:                            %.6lf\n \
			%d,%d summary.rw.q2q_avg:                            %.6lf\n \
			%d,%d summary.rw.nr_q2q:                             %llu\n \
			%d,%d summary.rw.q2g_acc:                            %.6lf\n \
			%d,%d summary.rw.q2g_max:                            %.6lf\n \
			%d,%d summary.rw.q2g_min:                            %.6lf\n \
			%d,%d summary.rw.q2g_avg:                            %.6lf\n \
			%d,%d summary.rw.nr_q2g:                             %llu\n \
			%d,%d summary.rw.q2c_acc:                            %.6lf\n \
			%d,%d summary.rw.q2c_max:                            %.6lf\n \
			%d,%d summary.rw.q2c_min:                            %.6lf\n \
			%d,%d summary.rw.q2c_avg:                            %.6lf\n \
			%d,%d summary.rw.nr_q2c:                             %llu\n \
			%d,%d summary.rw.d2c_acc:                            %.6lf\n \
			%d,%d summary.rw.d2c_max:                            %.6lf\n \
			%d,%d summary.rw.d2c_min:                            %.6lf\n \
			%d,%d summary.rw.d2c_avg:                            %.6lf\n \
			%d,%d summary.rw.nr_d2c:                             %llu\n \
			%d,%d summary.rw.nr_queue_origin:                    %llu\n \
			%d,%d summary.rw.nr_queue_sector_acc_origin:         %llu\n \
			%d,%d summary.rw.nr_queue:                           %llu\n \
			%d,%d summary.rw.nr_queue_sector_acc:                %llu\n \
			%d,%d summary.rw.nr_split:                           %llu\n \
			%d,%d summary.rw.nr_split_sector_acc:                %llu\n \
			%d,%d summary.rw.nr_merge:                           %llu\n \
			%d,%d summary.rw.nr_merge_sector_acc:                %llu\n \
			%d,%d summary.rw.nr_complete:                        %llu\n \
			%d,%d summary.rw.nr_complete_sector_acc:             %llu\n \
			%d,%d summary.rw.nr_sector_max:                      %llu\n \
			%d,%d summary.rw.nr_sector_min:                      %llu\n \
			%d,%d summary.rw.nr_sector_avg:                      %llu\n",
			dev->major, dev->minor, dev->summary.rw.q2q_acc_origin / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2q_max_origin / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2q_min_origin / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2q_avg_origin / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.nr_q2q_origin,
			dev->major, dev->minor, dev->summary.rw.q2q_acc / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2q_max / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2q_min / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2q_avg / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.nr_q2q,
			dev->major, dev->minor, dev->summary.rw.q2g_acc / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2g_max / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2g_min / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2g_avg / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.nr_q2g,
			dev->major, dev->minor, dev->summary.rw.q2c_acc / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2c_max / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2c_min / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.q2c_avg / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.nr_q2c,
			dev->major, dev->minor, dev->summary.rw.d2c_acc / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.d2c_max / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.d2c_min / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.d2c_avg / 1000000.0,
			dev->major, dev->minor, dev->summary.rw.nr_d2c,
			dev->major, dev->minor, dev->summary.rw.nr_queue_origin,
			dev->major, dev->minor, dev->summary.rw.nr_queue_sector_acc_origin,
			dev->major, dev->minor, dev->summary.rw.nr_queue,
			dev->major, dev->minor, dev->summary.rw.nr_queue_sector_acc,
			dev->major, dev->minor, dev->summary.rw.nr_split,
			dev->major, dev->minor, dev->summary.rw.nr_split_sector_acc,
			dev->major, dev->minor, dev->summary.rw.nr_merge,
			dev->major, dev->minor, dev->summary.rw.nr_merge_sector_acc,
			dev->major, dev->minor, dev->summary.rw.nr_complete,
			dev->major, dev->minor, dev->summary.rw.nr_complete_sector_acc,
			dev->major, dev->minor, dev->summary.rw.nr_sector_max,
			dev->major, dev->minor, dev->summary.rw.nr_sector_min,
			dev->major, dev->minor, dev->summary.rw.nr_sector_avg
		);
	}
}

void test(void)
{
	unsigned long long d2c = 0;
	int d2c_count = 0;
	unsigned long long blocks = 0;
	struct event *e;
	struct io *io;
	struct device *dev;


	printf("count0:%d count1:%d line_count:%d\n", count0, count1, line_count);

	list_for_each_entry(dev, &device_list, entry) {
		list_for_each_entry(io, &dev->io, entry) {
			e = list_entry(io->event.next, struct event, entry);
			printf("%c: cpu:%d time:%llu\n", e->type, e->cpu, e->time);
			blocks += e->nr_sector;
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

int main(int argc, char **argv)
{
	int ret;
	char record[512];
	FILE *f;
	struct event *e;


	ret = regex_init();
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

		parse_event(record, e);
		ret = process_event(e);
		if (ret)
			free(e);
	}

	// test();

	process_post();

	fclose(f);

	regex_free();

	return 0;
}
