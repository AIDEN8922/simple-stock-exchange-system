#include<iostream>
#include"stock_inventory.h"
#include"../include/server.h"
#include<vector>
#include<fstream>
#include<sstream>
#include<memory>
#include<exception>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

class Job {
//function objects that will be put into the queue of the thread_pool
private:
	int socket;
	StockInventory *p;   //points to the stock inventory inside the catalog server
public:
	Job(){}
	Job(int socket, StockInventory* p)
		:socket(socket), p(p){}
	void operator()(){
		//all operations including receving cmds from clients, parsing the cmds, doing actual processing, builing the reply msgs and sending the reply back
		//are done inside the worker thread, thus makes the main thread able to respond to other request quickly 
		char buffer[128];
		read(socket, buffer, 128);
		
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
		char reply[150] = "-100"; //th first field is always a state code, with -100 representing invaild command
		if (arg_list[0] == "lookup") {
			if (arg_list.size() > 1) {
				auto res = p->LookUp(arg_list[1]);
				sprintf(reply,"%d %s %u %u %u %u",res.first,res.second.name.c_str(),res.second.price,res.second.trade_num,res.second.stock_remaining,res.second.trade_num_limit);
			}
		}else if(arg_list[0] == "trade"){
			if(arg_list.size() >2){
				int num = std::stoi(arg_list[2]);
				auto res = p->TradeStocks(arg_list[1],num);
				sprintf(reply,"%d",res);
			}
		}

		send(socket, reply, strlen(reply), 0);
		close(socket);
		
	}
};

class CatalogServer :public Server<Job>{
	typedef std::unique_ptr<std::fstream,void(*)(std::fstream*)> FileUniptr;
private:
	StockInventory stk_inv;
	std::string save_file;

	Job BuildJob(int incoming_socket){ //implements the callback to provide the job to insert into the thread pool
		return Job(incoming_socket,&stk_inv);
	}
public:
	CatalogServer(std::string save_file)
	: save_file(save_file)
	{
		//load stock catalog
		//the file stores each stock info line by line, with fields seperated by space, all the fiels are in text format.
		FileUniptr file(new std::fstream(save_file,std::ios_base::in),[](std::fstream* file){file->close();delete file;});
		
		if(!file->is_open()) throw std::runtime_error("catalog log read error");
		std::string line;
		while (std::getline(*file, line))
		{
			std::istringstream iss(line);
			std::string name;
			unsigned price,stock_remaining,trade_num,trade_num_limit;
			if (!(iss >> name >> price >> trade_num >> stock_remaining >> trade_num_limit)) throw std::runtime_error("log file parse error");
			stk_inv.AddStock(StockInfo(name,price,trade_num,stock_remaining,trade_num_limit));
			
		}
		
	}

	virtual ~CatalogServer(){
		FileUniptr file(new std::fstream(save_file,std::ios_base::out|std::ios_base::trunc),[](std::fstream* file){file->close();delete file;});

		for(auto iter=stk_inv.cbegin();iter!=stk_inv.cend();iter++){
			StockInfo info = *iter;
			*file<<info.name<<" "<<info.price<<" "<<info.trade_num<<" "<<info.stock_remaining<<" "<<info.trade_num_limit<<std::endl;
		}
	}
};


int main() {
	try{
		CatalogServer server("data/catalog.txt");
		server.Start("0.0.0.0",8080,10);
	}catch(std::exception e){
		perror(e.what());
	}
}
