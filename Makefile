PORT=5554

all:clean server client

server:
	g++ server.cpp -o server -g
client:
	g++ client.cpp -o client -g
run_server:
	./server $(PORT) users_config shared_files
run_client:
	./client 127.0.0.1 $(PORT)
clean:
	rm -rf server client
