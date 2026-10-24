CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -g -pthread -I. -Ithird_party
LDFLAGS  ?= -pthread
TARGET    = server

# 所有模块一起编译、链接
SRCS = main.cpp \
       auth/UserStore.cpp \
       room/RoomManager.cpp \
       common/Net.cpp \
       session/SessionManager.cpp \
       session/ClientSession.cpp \
       media/UdpRelay.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
