CXX = g++ -std=c++17
CXXFLAGS = -ggdb -O -Wall -Werror

TARGETS = caltrain_test party_test
OBJS = 

all: $(TARGETS)
	
test: $(TARGETS)
	./run_tests
	
caltrain_test: caltrain_test.cc caltrain.cc
	$(CXX) $(CXXFLAGS) $< -pthread -o $@
	
party_test: party_test.cc party.cc
	$(CXX) $(CXXFLAGS) $< -pthread -o $@

$(OBJS): $(HEADERS)

clean::
	rm -f $(TARGETS) $(OBJS) *~ .*~

.PHONY: all clean

