
TARGET = sh111

CXXBASE = g++
CXX = $(CXXBASE) -std=c++17
CXXFLAGS = -ggdb -O -Wall -Werror

CPPFLAGS =
LIBS =

OBJS = sh111.o
HEADERS =

all: $(TARGET)

$(OBJS): $(HEADERS)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LIBS)


clean:
	rm -f $(TARGET) $(LIB) $(OBJS) $(LIBOBJS) *~ .*~ _test_data*

.PHONY: all clean starter
