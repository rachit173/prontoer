AR=ar
CXX=g++
CXXFLAGS=-std=c++17
CXXFLAGS+=-ggdb
CXXFLAGS+=-fno-stack-protector
CXXFLAGS+=-o3
LDFLAGS=-I./ -pthread -lpmem -luuid
TARGET=libprontoer.a

ifdef DEBUG
CXXFLAGS+=-DDEBUG
endif

ifdef LOG_SIZE
CXXFLAGS+=-DLOG_SIZE="((off_t)$(LOG_SIZE) << 30)"
endif

ifdef DISABLE_HT_PINNING
CXXFLAGS+=-DNO_HT_PINNING
endif


ifdef PRONTOER_SYNC
CXXFLAGS+=-DSYNC_SL # no ASL
endif



nv_log.o: nv_log.cc nv_log.h constants.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

nv_object.o: nv_object.cc nv_object.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

worker.o: worker.cc worker.h constants.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

$(TARGET): nv_object.o nv_log.o worker.o
	$(AR) rvs $@ $^

main.o: main.cc
	$(CXX) -c $(CXXFLAGS) -o $@ $^

tester: main.o ${TARGET} persistent_ordered_set.h
	$(CXX) $(CXXFLAGS) $^ -o $@ -luuid -pthread -lpmem 

clean:
	rm -f *.o
	rm -f ${TARGET}
	rm -f ordered_map
	rm -f tester