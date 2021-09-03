/* $Id: cache.h 23906 2007-05-02 19:13:03Z leeh $ */
#ifndef INCLUDED_filecache_h
#define INCLUDED_filecache_h

#define CACHELINELEN	81
#define CACHEFILELEN	30

struct lconn;

struct BlockHeap *cacheline_heap;

struct cachefile
{
	char name[CACHEFILELEN];
	dlink_list contents;
};

struct cacheline
{
	char data[CACHELINELEN];
	dlink_node linenode;
};

extern struct cacheline *emptyline;

extern void init_cache(void);
extern struct cachefile *cache_file(const char *, const char *, int add_blank);
extern void free_cachefile(struct cachefile *);

extern void send_cachefile(struct cachefile *, struct lconn *);

#endif

