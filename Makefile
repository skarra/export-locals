##
## Sriram Karra <karra@shakti.homelinux.net>
##
## Copyright (C) 2004, Sriram Karra.  All Rights Reserved.
##
## $Id$
##

CC=gcc
CFLAGS=-Wall -g
LDFLAGS=-lbfd

export_locals: export_locals.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f export_locals *.o

superclean: clean
	rm -f *~ a.out
