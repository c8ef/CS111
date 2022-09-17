
TARGETS = test

CXXBASE = c++
CXX = $(CXXBASE) -std=c++17
CXXFLAGS = -ggdb -Wall -Werror

CPPFLAGS = $$(pkg-config --cflags libcrypto)
LIBS = $$(pkg-config --libs libcrypto)

OBJS = mcryptfile.o cryptfile.o crypto.o vm.o test.o
HEADERS = cryptfile.hh crypto.hh mcryptfile.hh util.hh vm.hh

all: $(TARGETS)

$(OBJS): $(HEADERS)

test: $(OBJS) $(LIB)
	$(CXX) -o $@ $(OBJS) $(LIBS)


clean:
	rm -f $(TARGETS) $(LIB) $(OBJS) *~ .*~

.PHONY: all clean
