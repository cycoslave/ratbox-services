/* $Id: email.h 22343 2006-04-10 18:56:26Z leeh $ */
#ifndef INCLUDED_email_h
#define INCLUDED_email_h

int can_send_email(void);

int PRINTFLIKE(3, 4) send_email(const char *address, const char *subject, const char *format, ...);

#endif
