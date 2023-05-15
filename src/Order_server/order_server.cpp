#include<iostream>
#include"../include/server.h"
#include<vector>
#include<fstream>
#include<mutex>
#include<iomanip>
#include<atomic>
#include<exception>
#include<sstream>
#include<memory>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <memory.h>
#include <error.h>
#define _BSD_SOURCE
#include <netdb.h>

class ReplicaController{
//replicaInterface responsible for maintaining the consistency of log file accoss all replicas
//it handles connection establishment and maintainess between leader and followers. also handles sending and receiving updates from the leader.

//for simplicity, I intended to reduce the interaction directly between replicas as much as possible(For example,there is no things like replicas pinging each other 
//to determine who is the current leader at runtime), so my system were implemented based on the following rules: 
//every replica would first become a follower(waiting for the connection requests from leader) onces it starts,
//then, maybe very soon, or maybe after a while, it will be choosed by the front end server to become a leader.
//Once a replica becomes a leader, it stops listening from potential leader, and it trys to connect to all followers.
//A replica will continue being the leader until it die! which means a replica can only go from follower mode to leader mode,not reverse.
//when a replica goes up from a failure. it will NOT pull from the leader since it will not know who is the leader unless pinging other replica or the frontend, which
//requires implementing additional communication interface. Instead, the leader will keep trying to reconnect to all the down replicas, and it push updates to them once 
//they go up again
	typedef std::unique_ptr<std::fstream,void(*)(std::fstream*)> FileUniptr;
	typedef std::unique_ptr<int,void(*)(int*)> SocketUniptr;
	typedef std::unique_ptr<std::thread,void(*)(std::thread*)> ThreadUniptr;
	class LeaderInterface{
	private:
		const in_addr_t* follower_ips;
		const unsigned follower_port;
		const unsigned follower_num;

		std::vector<SocketUniptr> socket2followers;
		std::fstream* file;
	public:
		LeaderInterface(const in_addr_t* follower_ips, const unsigned follower_port, const unsigned follower_num,std::fstream* file)
		:follower_ips(follower_ips),follower_port(follower_port),follower_num(follower_num),file(file)
		{
			
			sockaddr_in follower_addr;
			follower_addr.sin_family = AF_INET;
        	follower_addr.sin_port = htons(follower_port);

			for(int i=0;i<follower_num;i++){
				int *tmp=new int(socket(AF_INET, SOCK_STREAM, 0));
				socket2followers.emplace_back(tmp,[](int* p){close(*p);delete p;});
				
				follower_addr.sin_addr.s_addr = follower_ips[i];
	
				connect(*socket2followers[i], (struct sockaddr*)&follower_addr, sizeof(follower_addr));
				DEBUG("Finish connecting to a follower\n");
			}
		}
		void Syn2followers(const char* log){
			for(int i=0;i<follower_num;i++){
				if(send(*socket2followers[i],log,strlen(log),MSG_NOSIGNAL)<0){
					if(errno==EPIPE||errno==ECONNRESET||errno==ENOTCONN){
						DEBUG("follower disconnection detected\n");
						//follower is down,try to see if it's up this time 
						socket2followers[i].reset(new int(socket(AF_INET, SOCK_STREAM, 0)));
						sockaddr_in follower_addr;
						follower_addr.sin_family = AF_INET;
        				follower_addr.sin_port = htons(follower_port);
						follower_addr.sin_addr.s_addr = follower_ips[i];
	
						if(connect(*socket2followers[i], (struct sockaddr*)&follower_addr, sizeof(follower_addr))==0){
							DEBUG("reconnect success\n");
							//follower goes up again, synchronize everything with him
							send(*socket2followers[i],"-1",2,0);//indicate follower we are going to the whole log file content and let him get prepered
							file->seekg(ID_LENGTH+1,std::ios_base::beg);
							std::string line;
							while(std::getline(*file,line)){
								line+="\n";
								send(*socket2followers[i],line.c_str(),line.size(),0);
							}
							file->clear();
							DEBUG("Finish sending curent log file content to the reconnected follower\n");
						}	else DEBUG("reconnecting failed\n");
					}else throw std::runtime_error("error sending to follower");
				}else {DEBUG("finish sending log to a follower:");DEBUG(log);}
			}
		}
	};
	
private:
	const unsigned host_id;
	unsigned replica_num;
	std::unique_ptr<in_addr_t[]> replica_ips;
	const unsigned follower_port;
	std::unique_ptr<LeaderInterface> leader_inf;//construct only after currrent node becomes a leader
	
	//members related to following behavior
	SocketUniptr socket_from_leader;
	ThreadUniptr following_thread;

	//members related to local log 
	std::mutex mutex; //used to synchroniza log operation
	unsigned long long next_order_no;
    FileUniptr file;
	enum{ID_LENGTH=20,NAME_LENGTH=30,TRADE_NUM_LENGTH=4,LOG_LENGTH=57};

public:
	ReplicaController(unsigned id,unsigned port,std::string order_log_file):
	host_id(id),
	follower_port(port),
	replica_num(stoi(std::string(getenv("ORDER_SERVER_NUM")))),
	replica_ips(new in_addr_t[replica_num]),
	socket_from_leader(new int(socket(AF_INET, SOCK_STREAM, 0)),[](int* s){close(*s);delete s;}),
	following_thread(nullptr,[](std::thread* t){}),
	file(new std::fstream(order_log_file,std::ios_base::in|std::ios_base::out),[](std::fstream* file){file->close();delete file;})
	{		
		if (*socket_from_leader<0) throw std::runtime_error("error creating socket_from_leader");
		if(!file->is_open())throw std::runtime_error("error openning order log");
		*file>>next_order_no; //read the historical order no because we need to increment based on that number
		file->seekp(0, std::ios_base::end);//set the write position to the end of file.

		for(int i=0,j=0;i<replica_num;i++){
			char buf[20];
			sprintf(buf,"ORDER_SERVER_%d",i);
			char *tmp=getenv(buf);
			in_addr_t ip;
			if(!tmp){
				struct hostent *host;
				if ((host = gethostbyname(buf)) == NULL) throw std::runtime_error("replica host resolution error");
				ip = *(long *)(host->h_addr_list[0]);
			}else{
				ip = inet_addr(tmp);
			}
			i==host_id?replica_ips[replica_num-1]=ip:replica_ips[j++]=ip;
		}

		following_thread=ThreadUniptr(new std::thread(&ReplicaController::HearingFromLeader,this),[](std::thread* t){
			pthread_cancel(t->native_handle());
			t->join();
			delete t;
		});
		
	}

	void StartLeading(){
		static std::atomic_int lock;
		if(!lock.fetch_add(1)){
			StopFollowing();
			leader_inf=std::make_unique<LeaderInterface>(replica_ips.get(),follower_port,replica_num-1,file.get());
			std::cout<<"node"<<host_id<<" pid:"<<getpid()<<" becomes the leader";
		}
	}
	unsigned long long WriteLogAndSync(std::string stock_name,std::string trade_num){
		std::stringstream s;
		std::unique_lock<std::mutex> lock(mutex);
		s<<std::setfill('0')<<std::setw(ID_LENGTH)<<next_order_no<<std::setfill(' ')<<std::setw(NAME_LENGTH+1)<<stock_name<<std::setfill(' ')<<std::setw(TRADE_NUM_LENGTH+1)<<trade_num<<std::endl;
		std::string tmp=s.str();
		*file<<tmp;
		leader_inf->Syn2followers(tmp.c_str());
		return next_order_no++;
	}
	virtual ~ReplicaController(){
		DEBUG("replica_controller destructed\n");
		file->seekp(std::ios_base::beg);
		*file<<std::setfill('0') << std::setw(20)<<next_order_no; //make sure the beginning order no takes fix space, otherwise data could be collapsed.
	}
private:
	void StopFollowing(){
		following_thread.reset();
	}
	void HearingFromLeader(){
		try{
			struct sockaddr_in address;
			int addrlen=sizeof(address);
			address.sin_family = AF_INET;
			address.sin_addr.s_addr = replica_ips[replica_num-1];
			address.sin_port = htons(follower_port);
		
			if (bind(*socket_from_leader, (struct sockaddr*)&address,sizeof(address))< 0) throw std::runtime_error("error binding socket_from_leader");
			if (listen(*socket_from_leader, 3) < 0) throw std::runtime_error("error listening socket_from_leader");

			int incoming_sock;
			char buf[LOG_LENGTH+1];
			memset(buf,0,LOG_LENGTH+1);
			while(1){
				DEBUG("follower ready accepting connection from new leader\n");
				//accept a connection, call the user defined "BuildJob" to get a Job and push it into the queue
				if ((incoming_sock = accept(*socket_from_leader, (struct sockaddr*)&address,(socklen_t*)&addrlen))< 0) {
					if(errno==EINTR){
						break;
					}
					throw std::runtime_error("error accepting socket_from_leader");
				}
				SocketUniptr incoming_sock_ptr(new int(incoming_sock),[](int* p){close(*p);delete p;});
				while(1){
					int ret;
					if((ret=read(*incoming_sock_ptr, buf, LOG_LENGTH))<=0){
						if(ret==0||errno==ECONNRESET||errno==EINTR){
							//leader crashed, waiting for new leader to setup connection again, otherwise a SIGINT has received, ready to stop.
							break;
						}
						throw std::runtime_error("error read from leader");
					}else{
						DEBUG("Successfuly read something from leader:");
						DEBUG(buf);
						pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);
						std::stringstream s(buf);
						long long order_id;
						s>>order_id;
						if(order_id<0){
							
							//synchronization from leader
							//erase the entire log file and wait for leader to send its copy
							file->seekp(std::ios_base::beg);
							*file<<std::setfill('0') << std::setw(ID_LENGTH)<<0<<std::endl;
							next_order_no=0;
							DEBUG("Local log being reset\n");
						}else{
							//a replica can be following and leading at the same time, and since the follower get log from leader in a serial manner,no synchronization
							//is needed here
							if(next_order_no!=order_id) {
								char* buf =new char[128];
								sprintf(buf,"data inconsistent detected, local(expected) next_order_id:%llu, received order id:%llu",next_order_no,order_id);
								delete[] buf;
								throw std::runtime_error(buf);
							}
							next_order_no=++order_id;
							*file<<buf;
						}
						pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
					}
				}
			}
		}catch(std::exception& e){
			perror(e.what());
			kill(getpid(),SIGINT);
		}
	}
};

class Job {
//function objects that will be put into the queue of the thread_pool
private:
    static struct sockaddr_in catalog_serv_addr;
	static std::shared_ptr<ReplicaController> rep_controller;
    int socket_to_front;//member variable of each Job object
public:
	static void InitJob(std::shared_ptr<ReplicaController>& rep_ctrler, const char* catalog_serv_ip, unsigned catalog_serv_port){
		rep_controller=rep_ctrler;
		
        catalog_serv_addr.sin_family = AF_INET;
        catalog_serv_addr.sin_port = htons(catalog_serv_port);
		if(catalog_serv_ip){
        	catalog_serv_addr.sin_addr.s_addr = inet_addr(catalog_serv_ip);
		}else{
			struct hostent *host;
			if ((host = gethostbyname("CATALOG_SERVER")) == NULL)
			{
				perror("catalog server hostname resolution failed");
				exit(EXIT_FAILURE);
			}
			catalog_serv_addr.sin_addr.s_addr = *(long *)(host->h_addr_list[0]);
		}
    }
	Job(){}
	Job(int socket)
		:socket_to_front(socket){}
	void operator()(){
		//we received requests from the frontend server, which means we become the leader, create a leader_interface inside the replica controller if we haven't
		rep_controller->StartLeading();
		//all operations including receving cmds from clients, parsing the cmds, doing actual processing, builing the reply msgs and sending the reply back
		//are done inside the worker thread, thus makes the main thread able to respond to other request quickly 
		char buffer[128];
		read(socket_to_front, buffer, 128);
		
		std::string cmd(buffer);
		std::vector<std::string> arg_list;
		size_t pos = 0;
		std::string delimiter = " ";
		while ((pos = cmd.find(delimiter)) != std::string::npos) {
			arg_list.push_back(cmd.substr(0, pos));
			cmd.erase(0, pos + delimiter.length());
		}
		arg_list.push_back(cmd);

		//the reply is a text string built from converting each fields of data to string and concatenate them with space as seperater 
		//the first field is always a state code, with -100 representing invaild command,-200 means cannot connect to the catalog server,other return code are
		//subjected to specific operation
		char reply[128]; 
		memset((void*)reply,'\0',sizeof(reply));
		int socket_to_catalog_server;
		if(arg_list[0] == "trade"){
			if ((socket_to_catalog_server = socket(AF_INET, SOCK_STREAM, 0)) < 0) throw std::runtime_error("socket2catalogserver creation error");
			if ((connect(socket_to_catalog_server, (struct sockaddr*)&catalog_serv_addr, sizeof(catalog_serv_addr)))< 0) {
				perror("cannot connect to catalog server");
				memcpy((void*)reply,(const void*)"-200",5);
			}else{
				send(socket_to_catalog_server, buffer, strlen(buffer), 0);//forward trade cmd to the catalog server and get reply
				read(socket_to_catalog_server, reply, 128);
				if(reply[0]=='1'){
					//if the trade operation success
					auto order_id = rep_controller->WriteLogAndSync(arg_list[1],arg_list[2]);
					sprintf(reply,"%llu",order_id); //send the order no back if transaction is successful
				}
			}
			close(socket_to_catalog_server);
		}else{
			memcpy((void*)reply,(const void*)"-100",5);
		}

		send(socket_to_front, reply, strlen(reply), 0);
		close(socket_to_front);
	}
};
struct sockaddr_in Job::catalog_serv_addr;
std::shared_ptr<ReplicaController> Job::rep_controller;

class OrderServer :public Server<Job>{
private:
	std::shared_ptr<ReplicaController> rep_controller;
	Job BuildJob(int socket){//implements the callback to provide the job to insert into the thread pool
		return Job(socket);
	}
public:
	OrderServer(const unsigned id, const unsigned replica_port, const char* catalog_serv_ip, const unsigned catalog_serv_port,const std::string log_file)
	:rep_controller(new ReplicaController(id,replica_port,log_file))
	{
		//the parent class constructor will be called implicitly	
		Job::InitJob(rep_controller,catalog_serv_ip,catalog_serv_port);
	}

};

int main(int argc, char** argv){
	char* catalog_server_ip=getenv("CATALOG_SERVER");
	unsigned id=stoi(std::string(argv[1]));
	const unsigned order_server_num=stoi(std::string(getenv("ORDER_SERVER_NUM")));
	
	try{
		OrderServer server(id,9000,catalog_server_ip,8080,"data/order_log.txt");
		server.Start("0.0.0.0",8081,10);
	}catch(std::exception& e){
		perror(e.what());
	}  
	
}