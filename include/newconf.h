/* This code is in the public domain.
 * $Nightmare: nightmare/include/config.h,v 1.32.2.2.2.2 2002/07/02 03:41:28 ejb Exp $
 * $Id: newconf.h 20489 2005-06-04 16:05:39Z leeh $
 */

#ifndef INCLUDED_newconf_h
#define INCLUDED_newconf_h

struct ConfEntry
{
	const char *cf_name;
	int cf_type;
	void (*cf_func) (void *);
	int cf_len;
	void *cf_arg;
};

struct ConfExtension
{
	const char *name;
	struct ConfEntry *items;
};

struct TopConf
{
	char *tc_name;
	int (*tc_sfunc) (struct TopConf *);
	int (*tc_efunc) (struct TopConf *);
	dlink_list tc_items;
	struct ConfEntry *tc_entries;
	dlink_list extensions;
};

#define CF_QSTRING	0x01
#define CF_INT		0x02
#define CF_STRING	0x03
#define CF_TIME		0x04
#define CF_YESNO	0x05
#define CF_LIST		0x06
#define CF_ONE		0x07

#define CF_MTYPE	0xFF

#define CF_FLIST	0x1000
#define CF_MFLAG	0xFF00

typedef struct conf_parm_t_stru
{
	struct conf_parm_t_stru *next;
	int type;
	union
	{
		char *string;
		int number;
		struct conf_parm_t_stru *list;
	}
	v;
}
conf_parm_t;

extern struct TopConf *conf_cur_block;

int read_config(char *);
int add_top_conf(const char *name, int (*sfunc) (struct TopConf *),
		int (*efunc) (struct TopConf *), struct ConfEntry *);
int add_conf_item(const char *, const char *, int, void (*)(void *));
int remove_conf_item(const char *, const char *);
void PRINTFLIKE(1, 2) conf_report_error(const char *, ...);
void newconf_init(void);
int conf_start_block(const char *, const char *);
int conf_end_block(struct TopConf *);
int conf_call_set(struct TopConf *, const char *, conf_parm_t *, int);


#endif
