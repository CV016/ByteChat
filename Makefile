CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -g
LDFLAGS = -lssl -lcrypto -lpthread

# Source files
SERVER_SRC = chatRoom.cpp
CLIENT_SRC = client.cpp

# Object files
SERVER_OBJ = $(SERVER_SRC:.cpp=.o)
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)

# Targets
all: chatApp clientApp

chatApp: chatRoom.o encryption.o
	$(CXX) $(CXXFLAGS) chatRoom.o encryption.o -o chatApp $(LDFLAGS)

chatRoom.o: chatRoom.cpp chatroom.hpp message.hpp encryption.hpp logger.hpp rate_limiter.hpp metrics.hpp
	$(CXX) $(CXXFLAGS) -c chatRoom.cpp -o chatRoom.o

encryption.o: encryption.cpp encryption.hpp
	$(CXX) $(CXXFLAGS) -c encryption.cpp -o encryption.o

clientApp: client.cpp message.hpp
	$(CXX) $(CXXFLAGS) client.cpp -o clientApp

clean:
	rm -f *.o chatApp clientApp