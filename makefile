CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

server: ./source/main.cpp  ./source/timer/twTimer.cpp ./source/http/httpconn.cpp ./source/log/log.cpp ./source/mysql/sqlpool.cpp  ./source/server/webserver.cpp ./source/server/utils.cpp 
	$(CXX) -o server  $^ $(CXXFLAGS) -lpthread -lmysqlclient

clean:
	rm  -r server
