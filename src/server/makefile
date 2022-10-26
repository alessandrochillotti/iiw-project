all:
	gcc server.c -o server -lpthread -DADAPTIVE_RTO
	clear
	
no_adaptive_rto:
	gcc server.c -o server -lpthread
	clear
	
test:
	gcc server.c -o server -lpthread -DTEST
	clear
	
test_adaptive_rto:
	gcc server.c -o server -lpthread -DADAPTIVE_RTO -DTEST
	clear

clean:
	-rm server
