/* $Id: modebuild.h 19992 2005-01-04 21:01:16Z leeh $ */
#ifndef INCLUDED_modebuild_h
#define INCLUDED_modebuild_h

void modebuild_start(struct client *, struct channel *);
void modebuild_add(int dir, const char *mode, const char *arg);
void modebuild_finish(void);

void kickbuild_start(void);
void kickbuild_add(const char *nick, const char *reason);
void kickbuild_finish(struct client *service_p, struct channel *chptr);

#endif
