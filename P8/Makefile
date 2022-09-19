TARGETS = v6 fsckv6 mountv6 mkfsv6 dumplog fusecleanup apply
LIB = liblogfs.a

CXXBASE = g++
CXX = $(CXXBASE) $(ARCH) -std=c++17
CC = $(CXX)
CXXFLAGS = -ggdb -Wall

CPPFLAGS = $$(pkg-config fuse3 --cflags) -MMD
LIBS = -L. -llogfs

OBJS = $(TARGETS:=.o)
ALLOBJS = apply.o bitmap.o blockpath.o buffer.o bufio.o cache.o		\
cursor.o dumplog.o fsckv6.o fsops.o inode.o itree.o log.o logentry.o	\
mkfsv6.o mountv6.o replay.o util.o v6.o v6fs.o
LIBOBJS = $(filter-out $(OBJS), $(ALLOBJS))
HEADERS = bitmap.hh blockpath.hh bufio.hh cache.hh fsops.hh ilist.hh	\
imisc.hh itree.hh layout.hh log.hh logentry.hh replay.hh util.hh	\
v6fs.hh

all: $(TARGETS)

$(LIB): $(LIBOBJS)
	rm -f $@
	$(AR) -crs $(LIB) $(LIBOBJS)

$(filter-out fusecleanup mountv6, $(TARGETS)): %: %.o $(LIB)
	$(CXX) -o $@ $< $(LIBS)

fusecleanup: fusecleanup.cc
	$(CXX) -o $@ fusecleanup.cc

mountv6: mountv6.o $(LIB)
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ \
		mountv6.o $(LIBS) $$(pkg-config fuse3 --libs)

clean:
	rm -f $(TARGETS) $(LIB) $(ALLOBJS) proj_log.html *.d *~ .*~

.PHONY: all clean

-include $(wildcard *.d)


