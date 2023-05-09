#include<iostream>
#include<exception>
#include<atomic>

#include"thread_pool.h"

#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>	//inet_addr
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#define DEBUG(x) ((std::cout<<x).flush())
template<typename Job>
class Server {
    //a abstract base class which implements common steps for a thread_pool base server
    //it handles connection reception, use the incoming connection to build a job and push it into the thread_pool job queue for execution
    //the class who inherits this class are responsible to specify the logic for creates a Job.
private:
	ThreadPool<Job> t_pool;
	int server_sock;

    virtual Job BuildJob(int incoming_socket)=0;

    static std::atomic_bool should_terminate;//for accessing in signal handler, it should be declared as static
    static void SignalHandler(int signum){
        DEBUG("enter signal handler");
        should_terminate=1;
    }
public:

	Server() {
        if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) throw std::runtime_error("error server socket create");
    };
	virtual void Start(const std::string server_ip, const unsigned port, const unsigned initial_thread_num) {
        //starts the thread pool
		t_pool.Start(initial_thread_num);
        //use the ctrl c to terminate the server(s)
        struct sigaction a;
        a.sa_handler=&SignalHandler;
        a.sa_flags=0;
        sigemptyset(&a.sa_mask);
        sigaction(SIGINT,&a,NULL);

        //setup the server socket and start listening
		struct sockaddr_in address;
		int addrlen = sizeof(address);

		address.sin_family = AF_INET;
		address.sin_addr.s_addr = inet_addr(server_ip.c_str());
		address.sin_port = htons(port);
	
		// Forcefully attaching socket to the port 8080
		
		if (bind(server_sock, (struct sockaddr*)&address,sizeof(address))< 0) throw std::runtime_error("error server socket bind");
		if (listen(server_sock, 7) < 0) throw std::runtime_error("error server socket listen");
		int incoming_sock;
		while(!should_terminate){
			//accept a connection, call the user defined "BuildJob" to get a Job and push it into the queue
			if ((incoming_sock= accept(server_sock, (struct sockaddr*)&address,(socklen_t*)&addrlen))< 0) {
				if(errno == EINTR){ //if the errno is EINTR, which means if the system call is interupt by a signal.
					break;
				}
				throw std::runtime_error("error server socket accept");
			}
			t_pool.QueueJob(BuildJob(incoming_sock));
		}
	
	}
	virtual ~Server() {
		shutdown(server_sock,SHUT_RDWR);
	}
};
template<typename Job> std::atomic_bool Server<Job>::should_terminate(0);

