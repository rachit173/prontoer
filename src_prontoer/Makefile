AR=ar
CXX=g++
CXXFLAGS=-std=c++17
CXXFLAGS+=-ggdb
CXXFLAGS+=-fno-stack-protector
CXXFLAGS+=-o3
LDFLAGS=-I./ -pthread -lpmem -luuid
TARGET=libsavitar.a

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

nv_catalog.o: nv_catalog.cc nv_catalog.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

nvm_manager.o: nvm_manager.cc nvm_manager.h recovery_context.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

nv_factory.o: nv_factory.cc nv_factory.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

thread.o: thread.cc thread.h constants.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<

cpu_info.o: cpu_info.cc
	$(CXX) -c $(CXXFLAGS) -o $@ $^

ordered_map: ordered_map.cc ${TARGET}
	$(CXX) $(CXXFLAGS) -lpmemobj -luuid -pthread ${TARGET} -o $@ $<

$(TARGET): nv_object.o nv_log.o thread.o cpu_info.o
	$(AR) rvs $@ $^

main.o: main.cc
	$(CXX) -c $(CXXFLAGS) -o $@ $^

tester: main.o ${TARGET}
	$(CXX) $(CXXFLAGS) $^ -o $@ -luuid -pthread -lpmem 

clean:
	rm -f *.o
	rm -f ${TARGET}
	rm -f ordered_map
	rm -f tester