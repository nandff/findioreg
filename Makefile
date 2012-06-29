# $Id: Makefile,v 1.2 2012/06/28 23:29:44 nand Exp $
# Copyright (C) 2012 Nozomu Ando <nand@mac.com> and others, see file forum.txt

FRAMEWORKS = -framework CoreFoundation -framework IOKit 

PROGRAM = findioreg
PROGRAM2 = findusbuart

${PROGRAM}: ${PROGRAM}.c
	${CC} -g -Wall ${FRAMEWORKS} $< -o $@

${PROGRAM2}: ${PROGRAM2}.c
	${CC} -g -Wall ${FRAMEWORKS} $< -o $@

clean:
	rm -rf *.dSYM
