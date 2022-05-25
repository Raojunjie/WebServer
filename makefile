CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: ./source/main.cpp  ./source/timer/lstTimer.cpp ./source/http/httpconn.cpp ./source/log/log.cpp ./source/mysql/sqlpool.cpp  ./source/server/webserver.cpp
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server
