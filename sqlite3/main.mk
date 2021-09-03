###############################################################################
# The following macros should be defined before this script is
# invoked:
#
# TOP              The toplevel directory of the source tree.  This is the
#                  directory that contains this "Makefile.in" and the
#                  "configure.in" script.
#
# BCC              C Compiler and options for use in building executables that
#                  will run on the platform that is doing the build.
#
# USLEEP           If the target operating system supports the "usleep()" system
#                  call, then define the HAVE_USLEEP macro for all C modules.
#
# THREADSAFE       If you want the SQLite library to be safe for use within a 
#                  multi-threaded program, then define the following macro
#                  appropriately:
#
# THREADLIB        Specify any extra linker options needed to make the library
#                  thread safe
#
# OPTS             Extra compiler command-line options.
#
# EXE              The suffix to add to executable files.  ".exe" for windows
#                  and "" for Unix.
#
# TCC              C Compiler and options for use in building executables that 
#                  will run on the target platform.  This is usually the same
#                  as BCC, unless you are cross-compiling.
#
# AR               Tools used to build a static library.
# RANLIB
#
# TCL_FLAGS        Extra compiler options needed for programs that use the
#                  TCL library.
#
# LIBTCL           Linker options needed to link against the TCL library.
#
# READLINE_FLAGS   Compiler options needed for programs that use the
#                  readline() library.
#
# LIBREADLINE      Linker options needed by programs using readline() must
#                  link against.
#
# NAWK             Nawk compatible awk program.  Older (obsolete?) solaris
#                  systems need this to avoid using the original AT&T AWK.
#
# Once the macros above are defined, the rest of this make script will
# build the SQLite library and testing tools.
################################################################################

# This is how we compile
#
TCCX = $(TCC) $(OPTS) $(THREADSAFE) $(USLEEP) -I. -I$(TOP)/src

# Object files for the SQLite library.
#
LIBOBJ+= alter.o analyze.o attach.o auth.o btree.o build.o \
         callback.o complete.o date.o delete.o \
         expr.o func.o hash.o insert.o \
         main.o opcodes.o os.o os_unix.o os_win.o \
         pager.o parse.o pragma.o prepare.o printf.o random.o \
         select.o table.o tclsqlite.o tokenize.o trigger.o \
         update.o util.o vacuum.o \
         vdbe.o vdbeapi.o vdbeaux.o vdbefifo.o vdbemem.o \
         where.o utf.o legacy.o

# All of the source code files.
#
SRC = \
  $(TOP)/src/alter.c \
  $(TOP)/src/analyze.c \
  $(TOP)/src/attach.c \
  $(TOP)/src/auth.c \
  $(TOP)/src/btree.c \
  $(TOP)/src/btree.h \
  $(TOP)/src/build.c \
  $(TOP)/src/callback.c \
  $(TOP)/src/complete.c \
  $(TOP)/src/date.c \
  $(TOP)/src/delete.c \
  $(TOP)/src/expr.c \
  $(TOP)/src/func.c \
  $(TOP)/src/hash.c \
  $(TOP)/src/hash.h \
  $(TOP)/src/insert.c \
  $(TOP)/src/legacy.c \
  $(TOP)/src/main.c \
  $(TOP)/src/os.c \
  $(TOP)/src/os_unix.c \
  $(TOP)/src/os_win.c \
  $(TOP)/src/pager.c \
  $(TOP)/src/pager.h \
  $(TOP)/src/pragma.c \
  $(TOP)/src/prepare.c \
  $(TOP)/src/printf.c \
  $(TOP)/src/random.c \
  $(TOP)/src/select.c \
  $(TOP)/src/shell.c \
  $(TOP)/src/sqlite.h.in \
  $(TOP)/src/sqliteInt.h \
  $(TOP)/src/table.c \
  $(TOP)/src/tclsqlite.c \
  $(TOP)/src/tokenize.c \
  $(TOP)/src/trigger.c \
  $(TOP)/src/utf.c \
  $(TOP)/src/update.c \
  $(TOP)/src/util.c \
  $(TOP)/src/vacuum.c \
  $(TOP)/src/vdbe.c \
  $(TOP)/src/vdbe.h \
  $(TOP)/src/vdbeapi.c \
  $(TOP)/src/vdbeaux.c \
  $(TOP)/src/vdbefifo.c \
  $(TOP)/src/vdbemem.c \
  $(TOP)/src/vdbeInt.h \
  $(TOP)/src/where.c

# Header files used by all library source files.
#
HDR = \
   sqlite3.h  \
   $(TOP)/src/btree.h \
   $(TOP)/src/hash.h \
   opcodes.h \
   $(TOP)/src/os.h \
   $(TOP)/src/os_common.h \
   $(TOP)/src/sqliteInt.h  \
   $(TOP)/src/vdbe.h \
   parse.h

# Header files used by the VDBE submodule
#
VDBEHDR = \
   $(HDR) \
   $(TOP)/src/vdbeInt.h

# This is the default Makefile target.  The objects listed here
# are what get build when you type just "make" with no arguments.
#
all:	sqlite3.h libsqlite3.a sqlite3$(EXE)

# Generate the file "last_change" which contains the date of change
# of the most recently modified source code file
#
last_change:	$(SRC)
	cat $(SRC) | grep '$$Id: ' | sort +4 | tail -1 \
          | $(NAWK) '{print $$5,$$6}' >last_change

libsqlite3.a:	$(LIBOBJ)
	$(AR) libsqlite3.a $(LIBOBJ)
	$(RANLIB) libsqlite3.a

sqlite3$(EXE):	$(TOP)/src/shell.c libsqlite3.a sqlite3.h
	$(TCCX) $(READLINE_FLAGS) -o sqlite3$(EXE) $(TOP)/src/shell.c \
		libsqlite3.a $(LIBREADLINE) $(TLIBS) $(THREADLIB)

objects: $(LIBOBJ_ORIG)

# This target creates a directory named "tsrc" and fills it with
# copies of all of the C source code and header files needed to
# build on the target system.  Some of the C source code and header
# files are automatically generated.  This target takes care of
# all that automatic generation.
#
target_source:	$(SRC) $(VDBEHDR) opcodes.c keywordhash.h
	rm -rf tsrc
	mkdir tsrc
	cp $(SRC) $(VDBEHDR) tsrc
	cp parse.c opcodes.c keywordhash.h tsrc
	cp $(TOP)/sqlite3.def tsrc

# Rules to build the LEMON compiler generator
#
lemon:	$(TOP)/tool/lemon.c $(TOP)/tool/lempar.c
	$(BCC) -o lemon $(TOP)/tool/lemon.c
	cp $(TOP)/tool/lempar.c .

# Rules to build individual files
#
alter.o:	$(TOP)/src/alter.c $(HDR)
	$(TCCX) -c $(TOP)/src/alter.c

analyze.o:	$(TOP)/src/analyze.c $(HDR)
	$(TCCX) -c $(TOP)/src/analyze.c

attach.o:	$(TOP)/src/attach.c $(HDR)
	$(TCCX) -c $(TOP)/src/attach.c

auth.o:	$(TOP)/src/auth.c $(HDR)
	$(TCCX) -c $(TOP)/src/auth.c

btree.o:	$(TOP)/src/btree.c $(HDR) $(TOP)/src/pager.h
	$(TCCX) -c $(TOP)/src/btree.c

build.o:	$(TOP)/src/build.c $(HDR)
	$(TCCX) -c $(TOP)/src/build.c

callback.o:	$(TOP)/src/callback.c $(HDR)
	$(TCCX) -c $(TOP)/src/callback.c

complete.o:	$(TOP)/src/complete.c $(HDR)
	$(TCCX) -c $(TOP)/src/complete.c

date.o:	$(TOP)/src/date.c $(HDR)
	$(TCCX) -c $(TOP)/src/date.c

delete.o:	$(TOP)/src/delete.c $(HDR)
	$(TCCX) -c $(TOP)/src/delete.c

expr.o:	$(TOP)/src/expr.c $(HDR)
	$(TCCX) -c $(TOP)/src/expr.c

func.o:	$(TOP)/src/func.c $(HDR)
	$(TCCX) -c $(TOP)/src/func.c

hash.o:	$(TOP)/src/hash.c $(HDR)
	$(TCCX) -c $(TOP)/src/hash.c

insert.o:	$(TOP)/src/insert.c $(HDR)
	$(TCCX) -c $(TOP)/src/insert.c

legacy.o:	$(TOP)/src/legacy.c $(HDR)
	$(TCCX) -c $(TOP)/src/legacy.c

main.o:	$(TOP)/src/main.c $(HDR)
	$(TCCX) -c $(TOP)/src/main.c

pager.o:	$(TOP)/src/pager.c $(HDR) $(TOP)/src/pager.h
	$(TCCX) -c $(TOP)/src/pager.c

opcodes.o:	opcodes.c
	$(TCCX) -c opcodes.c

opcodes.c:	opcodes.h $(TOP)/mkopcodec.awk
	sort -n -b +2 opcodes.h | $(NAWK) -f $(TOP)/mkopcodec.awk >opcodes.c

opcodes.h:	parse.h $(TOP)/src/vdbe.c $(TOP)/mkopcodeh.awk
	cat parse.h $(TOP)/src/vdbe.c | $(NAWK) -f $(TOP)/mkopcodeh.awk >opcodes.h

os.o:	$(TOP)/src/os.c $(HDR)
	$(TCCX) -c $(TOP)/src/os.c

os_unix.o:	$(TOP)/src/os_unix.c $(HDR)
	$(TCCX) -c $(TOP)/src/os_unix.c

os_win.o:	$(TOP)/src/os_win.c $(HDR)
	$(TCCX) -c $(TOP)/src/os_win.c

parse.o:	parse.c $(HDR)
	$(TCCX) -c parse.c

pragma.o:	$(TOP)/src/pragma.c $(HDR)
	$(TCCX) $(TCL_FLAGS) -c $(TOP)/src/pragma.c

prepare.o:	$(TOP)/src/prepare.c $(HDR)
	$(TCCX) $(TCL_FLAGS) -c $(TOP)/src/prepare.c

printf.o:	$(TOP)/src/printf.c $(HDR)
	$(TCCX) $(TCL_FLAGS) -c $(TOP)/src/printf.c

random.o:	$(TOP)/src/random.c $(HDR)
	$(TCCX) -c $(TOP)/src/random.c

select.o:	$(TOP)/src/select.c $(HDR)
	$(TCCX) -c $(TOP)/src/select.c

sqlite3.h:	$(TOP)/src/sqlite.h.in 
	sed -e s/--VERS--/`cat ${TOP}/VERSION`/ \
	    -e s/--VERSION-NUMBER--/`cat ${TOP}/VERSION | sed 's/[^0-9]/ /g' | $(NAWK) '{printf "%d%03d%03d",$$1,$$2,$$3}'`/ \
                 $(TOP)/src/sqlite.h.in >sqlite3.h

table.o:	$(TOP)/src/table.c $(HDR)
	$(TCCX) -c $(TOP)/src/table.c

tclsqlite.o:	$(TOP)/src/tclsqlite.c $(HDR)
	$(TCCX) $(TCL_FLAGS) -c $(TOP)/src/tclsqlite.c

tokenize.o:	$(TOP)/src/tokenize.c keywordhash.h $(HDR)
	$(TCCX) -c $(TOP)/src/tokenize.c

keywordhash.h:	$(TOP)/tool/mkkeywordhash.c
	$(BCC) -o mkkeywordhash $(OPTS) $(TOP)/tool/mkkeywordhash.c
	./mkkeywordhash >keywordhash.h

trigger.o:	$(TOP)/src/trigger.c $(HDR)
	$(TCCX) -c $(TOP)/src/trigger.c

update.o:	$(TOP)/src/update.c $(HDR)
	$(TCCX) -c $(TOP)/src/update.c

utf.o:	$(TOP)/src/utf.c $(HDR)
	$(TCCX) -c $(TOP)/src/utf.c

util.o:	$(TOP)/src/util.c $(HDR)
	$(TCCX) -c $(TOP)/src/util.c

vacuum.o:	$(TOP)/src/vacuum.c $(HDR)
	$(TCCX) -c $(TOP)/src/vacuum.c

vdbe.o:	$(TOP)/src/vdbe.c $(VDBEHDR)
	$(TCCX) -c $(TOP)/src/vdbe.c

vdbeapi.o:	$(TOP)/src/vdbeapi.c $(VDBEHDR)
	$(TCCX) -c $(TOP)/src/vdbeapi.c

vdbeaux.o:	$(TOP)/src/vdbeaux.c $(VDBEHDR)
	$(TCCX) -c $(TOP)/src/vdbeaux.c

vdbefifo.o:	$(TOP)/src/vdbefifo.c $(VDBEHDR)
	$(TCCX) -c $(TOP)/src/vdbefifo.c

vdbemem.o:	$(TOP)/src/vdbemem.c $(VDBEHDR)
	$(TCCX) -c $(TOP)/src/vdbemem.c

where.o:	$(TOP)/src/where.c $(HDR)
	$(TCCX) -c $(TOP)/src/where.c

# Rules for building test programs and for running tests
#
tclsqlite3:	$(TOP)/src/tclsqlite.c libsqlite3.a
	$(TCCX) $(TCL_FLAGS) -DTCLSH=1 -o tclsqlite3 \
		$(TOP)/src/tclsqlite.c libsqlite3.a $(LIBTCL) $(THREADLIB)

# Standard install and cleanup targets
#
install:	sqlite3 libsqlite3.a sqlite3.h
	mv sqlite3 /usr/bin
	mv libsqlite3.a /usr/lib
	mv sqlite3.h /usr/include

clean:	
	rm -f *.o sqlite3 libsqlite3.a sqlite3.h opcodes.*
	rm -f lemon lempar.c parse.* sqlite*.tar.gz mkkeywordhash keywordhash.h
	rm -f $(PUBLISH)
	rm -f *.da *.bb *.bbg gmon.out
	rm -rf tsrc
