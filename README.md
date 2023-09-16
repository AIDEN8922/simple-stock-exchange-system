# simple stock exchange system

## overview
This work implements a simple stock exchange system that supports basic stock operations(stock lookup, trading, logging, etc) .
it uses a hand-implemented threadpool to implement thread-per-request model. Also applys locks to synchronize the access to s
tocks. 
* Applied micro-service architecture. Broke the backend functionality(order logging and stock info management)into two
individual micro-service. Plus the frontend services, each one of these three micro-services can be deployed into a
designated container and run separately. By changing the project into micro-service architecture, we add more modularity
to the system. Also each one of these services can be expanded individually as needed,adding scalability.
* Replicated the order servers(responsible for order logging) to add robustness to the system and in the mean time handled
log data synchronization between replicas. Also designed a fault tolerance mechanism to automatically pick other order
server when the current one is down, and automatically synchronize data with the re-connected server.
* Applied C++ RAII into our design by putting the resource release operation to the destructor of the classes and use smart
pointers to wrap the resources we allocated. So as to ensure the correct release of the resources in any circumstan
## how to run
in the setup_environment.sh, modify the environmental variable setting: set the ip address of catalog server, and order servers respectly. copy the edited scripts and run it on the terminal(uses source or .) of every machines where order servers and frontend server will be running, they all use the same environmental setup.

the server programs are complied already in the subfolders of src directory, you are boot the servers in any order, but before issuing the requests, make sure all servers are up(unless in test fault tolarence test setting). run catalog server as ./catalog server. run order servers as ./order_server ORDER_SERVER_ID, where the ORDER_SERVER_ID is a integer that correspond to what is configured in the setup_environment.sh. run frontend server as python3 frontend_server.py ENABLE_CACHE. replace ENABLE_CACHE with 1 or 0. 

Noted that
1.order server will detect the inconsistency in the order_log. Once detected, it will shutdown itself. It is user's job to bring it up again so that it can be synchronized by the leader.
2.for a graceful termination, terminated the leading order server first. then terminate the following order server in any order. The leader are choosed in a round-robin manor if the exsiting one goes down, there will be a output if one of the order_server becomes the leader and user may use it as a clue to terminate the system gracefully
3.An error saying: "bind failed, address already in use" will pomp up if one shut down order server and restarted it to quickly, that's because even if we close the socket and release the resource when program end, TCP will still entering time_wait state for a minute or two just to make sure any lingering packet that later arrive would cause any program to the newly created connection. Just wait and sip some coffee before you bring up the order server again. 
## design ideas
see README.md located in the src subfolder
## project background setting

The Gauls have really taken to stock trading and trading has become their village pass time. To ensure 
high performance and tolerance to failures, they have decided to adopt modern design practices. 

### Caching

add caching to the front-end service to reduce the latency of the stock query
requests. The front-end server starts with an empty in-memory cache. Upon receiving a stock query
request, it first checks the in-memory cache to see whether it can be served from the cache. If not,
the request will then be forwarded to the catalog service, and the result returned by the catalog
service will be stored in the cache. Cache consistency needs to be addressed whenever a stock is bought or sold. 

### Replication

To make sure that our stock bazaar doesn't lose any order information due to crash failures, we want
to replicate the order service. we want three replicas of the order service, each with a unique id
number and its own database file. There should always be 1 leader node and the rest are follower
nodes.

When the front-end service starts, it will read the id number and address of each replica of the
order service . It will ping the replica to determine who is the current leader

When a trade request or an order query request arrives, the front-end service only forwards the
request to the leader. In case of a successful trade (a new order number is generated), the leader
node will propagate the information of the new order to the follower nodes to maintain data
consistency.

### Fault Tolerance

we want to handle failures of the order service. 

First We want to make sure that when any replica crashes (including the leader), trade requests and
order query requests can still be handled and return the correct result. To achieve this, when the
front-end service finds that the leader node is unresponsive, it will redo the leader selection
algorithm 

We also want to make sure that when a crashed replica is back online, it can synchronize with the
other replicas to retrieve the order information that it has missed during the offline time. .

### Deploy to the cloud

we want our stock service to be available on the cloud

we want to deploy our application on an `m5a.xlarge` instance in the `us-east-1` region on AWS. 



## References

1. Learn about Gaul (the region) https://en.wikipedia.org/wiki/Gaul and the Gauls (the people) https://en.wikipedia.org/wiki/Gauls
2. Learn about the comics that these labs are based on https://en.wikipedia.org/wiki/Asterix
3. Learn about Web framework such as Flask (python) https://flask.palletsprojects.com/en/2.2.x/  There are many python and java web frameworks - choose carefully if you decide to use one.
4. Learn about model-view-controller (MVC) paradigm of writing web apps using frameworks https://en.wikipedia.org/wiki/Model–view–controller
