#
#	Makefile for mar.
#	Read the start of mar.c for notes etc.
#

CFLAGS	=	-O -std=c89

# CFLAGS =	-O -DNOSWAB -Dstrchr=index 

#	Installation directories.
BIN	=	/usr/contrib/bin
MANSECT	=	1
MAN	=	/usr/man/man$(MANSECT)

all:	mar

clean:
	rm -f mar

man:
	@nroff -man mar.1

lint:
	lint -p mar.c

shar dist:	mar.shar
mar.shar:    Makefile ReadMe mar.1 mar.c 
	shar Makefile ReadMe mar.1 mar.c >mar.shar

install:
	cp mar $(BIN)/mar
	chown bin $(BIN)/mar
	chgrp bin $(BIN)/mar
	chmod 755 $(BIN)/mar
	cp mar.1 $(MAN)/man.$(MANSECT)
	chown man $(MAN)/man.$(MANSECT)
	chgrp man $(MAN)/man.$(MANSECT)
	chmod 644 $(MAN)/man.$(MANSECT)
