all: server subscriber

# Compileaza server.c
server: server.cpp
	g++ server.cpp -o server

# Compileaza client.c
client: subscriber.cpp
	g++ subscriber.cpp -o subscriber

clean:
	rm -f server subscriber
