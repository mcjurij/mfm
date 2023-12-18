mfm_server: server.o proto.o conn_slots.o process_data.o file_mgr.o
	gcc -pthread -g -o mfm_server server.o proto.o conn_slots.o process_data.o file_mgr.o -lm

proto.h: conf.h

proto.o: proto.c proto.h
	gcc -pthread -g -c -Wall -o proto.o proto.c

server.o: server.c proto.h conf.h conn_slots.h process_data.h
	gcc -pthread -g -c -Wall -o server.o server.c

conn_slots.o: conn_slots.c conn_slots.h process_data.h
	gcc -pthread -g -c -Wall -o conn_slots.o conn_slots.c

conn_slots.h: conf.h

process_data.o: process_data.c process_data.h conn_slots.h file_mgr.h conf.h
	gcc -pthread -g -c -Wall -o process_data.o process_data.c

process_data.h: proto.h

#process_ringbuffer.o: process_ringbuffer.c process_ringbuffer.h
#	gcc -pthread -g -c -Wall -o process_ringbuffer.o process_ringbuffer.c

#process_ringbuffer.h:

file_mgr.o: file_mgr.c file_mgr.h conf.h process_data.h
	gcc -pthread -g -c -Wall -o file_mgr.o file_mgr.c

file_mgr.h:

clean:
	rm mfm_server server.o proto.o conn_slots.o process_data.o file_mgr.o
