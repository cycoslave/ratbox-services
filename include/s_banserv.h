/* $Id: s_nickserv.h 20344 2005-05-06 21:51:24Z leeh $ */
#ifndef INCLUDED_banserv_h
#define INCLUDED_banserv_h

extern dlink_list regexp_list;

struct regexp_ban
{
	char *regexp_str;
	char *reason;
	char *oper;

	time_t hold;
	time_t create_time;

	unsigned int id;

	dlink_node ptr;
	dlink_list negations;

	struct regexp_ban *parent;

	pcre *regexp;
};

#endif
