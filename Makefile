PORT=5554

all:clean server client

server:
	g++ server.cpp -o server -g
client:
	g++ client.cpp -o client -g
clean:
	rm -rf server client
	rm -rf ./client*.log
