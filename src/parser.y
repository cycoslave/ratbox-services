/* This code is in the public domain.
 * $Nightmare: nightmare/src/main/parser.y,v 1.2.2.1.2.1 2002/07/02 03:42:10 ejb Exp $
 * $Id: parser.y 26668 2009-09-19 22:09:16Z leeh $
 */

%{
#include <sys/types.h>
#include <netinet/in.h>

#define WE_ARE_MEMORY_C
#include "stdinc.h"
#include "setup.h"
#include "config.h"
#include "rserv.h"
#include "newconf.h"
#include "conf.h"

#define YY_NO_UNPUT

int yyparse();
int yylex();

static time_t conf_find_time(char*);

static struct {
	const char *	name;
	const char *	plural;
	time_t 	val;
} times[] = {
	{"second",     "seconds",    1},
	{"minute",     "minutes",    60},
	{"hour",       "hours",      60 * 60},
	{"day",        "days",       60 * 60 * 24},
	{"week",       "weeks",      60 * 60 * 24 * 7},
	{"fortnight",  "fortnights", 60 * 60 * 24 * 14},
	{"month",      "months",     60 * 60 * 24 * 7 * 4},
	{"year",       "years",      60 * 60 * 24 * 365},
	/* ok-- we now do sizes here too. they aren't times, but 
	   it's close enough */
	{"byte",	"bytes",	1},
	{"kb",		NULL,		1024},
	{"kbyte",	"kbytes",	1024},
	{"kilobyte",	"kilobytes",	1024},
	{"mb",		NULL,		1024 * 1024},
	{"mbyte",	"mbytes",	1024 * 1024},
	{"megabyte",	"megabytes",	1024 * 1024},
	{NULL},
};

time_t conf_find_time(char *name)
{
  int i;

  for (i = 0; times[i].name; i++)
    {
      if (strcasecmp(times[i].name, name) == 0 ||
	  (times[i].plural && strcasecmp(times[i].plural, name) == 0))
	return times[i].val;
    }

  return 0;
}

static struct
{
	const char *word;
	int yesno;
} yesno[] = {
	{"yes",		1},
	{"no",		0},
	{"true",	1},
	{"false",	0},
	{"on",		1},
	{"off",		0},
	{NULL,		0}
};

static int	conf_get_yesno_value(char *str)
{
	int i;

	for (i = 0; yesno[i].word; i++)
	{
		if (strcasecmp(str, yesno[i].word) == 0)
		{
			return yesno[i].yesno;
		}
	}

	return -1;
}

static void	free_cur_list(conf_parm_t* list)
{
	switch (list->type & CF_MTYPE)
	{
		case CF_STRING:
		case CF_QSTRING:
			my_free(list->v.string);
			break;
		case CF_LIST:
			free_cur_list(list->v.list);
			break;
		default: break;
	}

	if (list->next)
		free_cur_list(list->next);
}

		
conf_parm_t *	cur_list = NULL;

static void	add_cur_list_cpt(conf_parm_t *new)
{
	conf_parm_t *end;

	if (cur_list == NULL)
	{
		cur_list = my_malloc(sizeof(conf_parm_t));
		cur_list->v.list = new;
	}
	else
	{
		cur_list->type |= CF_FLIST;

		for(end = cur_list->v.list; end->next; end = end->next)
			;

		end->next = new;
		new->next = NULL;
	}
}

static void	add_cur_list(int type, char *str, int number)
{
	conf_parm_t *new;

	new = my_malloc(sizeof(conf_parm_t));
	new->next = NULL;
	new->type = type;

	switch(type)
	{
	case CF_INT:
	case CF_TIME:
	case CF_YESNO:
		new->v.number = number;
		break;
	case CF_STRING:
	case CF_QSTRING:
		new->v.string = my_strdup(str);
		break;
	}

	add_cur_list_cpt(new);
}


%}

%union {
	int 		number;
	char 		string[BUFSIZE + 1];
	conf_parm_t *	conf_parm;
}

%token TWODOTS

%token <string> QSTRING STRING
%token <number> NUMBER

%type <string> qstring string
%type <number> number timespec 
%type <conf_parm> oneitem single itemlist

%start conf

%%

conf: 
	| conf conf_item 
	| error
	;

conf_item: block
         ;

block: string 
         { 
           conf_start_block($1, NULL);
         }
       '{' block_items '}' ';' 
         {
	   if (conf_cur_block)
           	conf_end_block(conf_cur_block);
         }
     | string qstring 
         { 
           conf_start_block($1, $2);
         }
       '{' block_items '}' ';'
         {
	   if (conf_cur_block)
           	conf_end_block(conf_cur_block);
         }
     ;

block_items: block_items block_item 
           | block_item 
           ;

block_item:	string '=' itemlist ';'
		{
			conf_call_set(conf_cur_block, $1, cur_list, CF_LIST);
			free_cur_list(cur_list);
			cur_list = NULL;
		}
		;

itemlist: itemlist ',' single
	| single
	;

single: oneitem
	{
		add_cur_list_cpt($1);
	}
	| oneitem TWODOTS oneitem
	{
		/* "1 .. 5" meaning 1,2,3,4,5 - only valid for integers */
		if (($1->type & CF_MTYPE) != CF_INT ||
		    ($3->type & CF_MTYPE) != CF_INT)
		{
			conf_report_error("Both arguments in '..' notation must be integers.");
			break;
		}
		else
		{
			int i;

			for (i = $1->v.number; i <= $3->v.number; i++)
			{
				add_cur_list(CF_INT, 0, i);
			}
		}
	}
	;

oneitem: qstring
            {
		$$ = my_malloc(sizeof(conf_parm_t));
		$$->type = CF_QSTRING;
		$$->v.string = my_strdup($1);
	    }
          | timespec
            {
		$$ = my_malloc(sizeof(conf_parm_t));
		$$->type = CF_TIME;
		$$->v.number = $1;
	    }
          | number
            {
		$$ = my_malloc(sizeof(conf_parm_t));
		$$->type = CF_INT;
		$$->v.number = $1;
	    }
          | string
            {
		/* a 'string' could also be a yes/no value .. 
		   so pass it as that, if so */
		int val = conf_get_yesno_value($1);

		$$ = my_malloc(sizeof(conf_parm_t));

		if (val != -1)
		{
			$$->type = CF_YESNO;
			$$->v.number = val;
		}
		else
		{
			$$->type = CF_STRING;
			$$->v.string = my_strdup($1);
		}
            }
          ;

qstring: QSTRING { strcpy($$, $1); } ;
string: STRING { strcpy($$, $1); } ;
number: NUMBER { $$ = $1; } ;

timespec:	number string
         	{
			time_t t;

			if ((t = conf_find_time($2)) == 0)
			{
				conf_report_error("Unrecognised time type/size '%s'", $2);
				t = 1;
			}
	    
			$$ = $1 * t;
		}
		| timespec timespec
		{
			$$ = $1 + $2;
		}
		| timespec number
		{
			$$ = $1 + $2;
		}
		;
