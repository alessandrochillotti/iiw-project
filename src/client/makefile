all:
	gcc client.c -o client -lpthread -DADAPTIVE_RTO
	clear
	
no_adaptive_rto:
	gcc client.c -o client -lpthread
	clear
	
test:
	gcc client.c -o client -lpthread -DTEST
	clear
	
test_adaptive_rto:
	gcc client.c -o client -lpthread -DADAPTIVE_RTO -DTEST
	clear
clean:
	-rm client
