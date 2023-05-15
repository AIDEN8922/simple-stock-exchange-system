# design documentation
we add a replica_controller to the code of order_server for this lab
replica_controller is responsible for maintaining the consistency of log file accoss all replicas
it handles connection establishment and maintainess between leader and followers, detecting the reconnection of follower.  and handles sending and receiving updates  from the leader. The consistency control idea of it is defined in the following ways:

for simplicity, I intended to reduce the interaction directly between replicas as much as possible(For example,there is no things like replicas pinging each other 
to determine who is the current leader at runtime), so my system were implemented based on the following rules: 
every replica would first become a follower(waiting for the connection requests from leader) onces it starts,
then, maybe very soon, or maybe after a while, it will be choosed by the front end server to become a leader.
Once a replica becomes a leader, it stops listening from potential leader, and it trys to connect to all followers.
A replica will continue being the leader until it die! which means a replica can only go from follower mode to leader mode,not reverse.
when a replica goes up from a failure. it will NOT pull from the leader since it will not know who is the leader unless pinging other replica or the frontend, which
requires implementing additional communication interface. Instead, the leader will keep trying to reconnect to all the down replicas, and it push updates to them once they go up again

More specifically, the node first assume itself to be a follower(including the rebooted old leader) and starts an thread wait for the connection from leader and then read updates from the leader(or receive the whole existing log file from the leader, after leader detected the reconnect of that follower). The frontend server will choose an initial leader, and choose the new leader when the old one goes down. Once a node become a leader, it stops the following thread and establish the connection to all follower.

THe leader detect reconnection of the follower only if: the follower is first not connected and then it comes back again. Which means it cannot 100% sychronized the reconnected follower. (thinking about the case of one follower goes down, then leader goes down, the that follower goes up before the new leader set up the connection with him, which means in this case the leader are not aware of any event of failure of its follower, and will not take the initiative to send its log file to that follower to have itsynchronized). Saying that. the follower are able to detect inconsistency by itself(when the order id of the new log received from the leader is not equal to the local next_order_id the follower calculated according to what is stored on its log file). In this case the follower will shutdown itself and hopefully the next time it comes up the leader will perceive its "reconection" and sychronize the whole log file to him.

We lock the whole process of getting an uniqued order number for a succeeded order and syn that order log to other followers, because we dont want follower to recevied "unorder" order log, which will cause them to feel their log is inconsistent and all of them will shutdwon themself.  

We implement the cache in the frontend server as required in the lab specification and make it a configurable option at start.

In addition to that, our implementation is based on the code of lab 2 and we made big refinement to it. we apply a modern c++ concept called RAII to our design.  we put the resource release operation from the stop() to the destructor of the classes, and we use smart pointers to wrap the resources we allocated. So as to ensure the correct release of the resources in any circumstances. 