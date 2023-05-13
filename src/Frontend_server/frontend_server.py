from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
# Import socket module
import socket            
import json
import threading
import os
import sys
# Define ip and ports
enable_cache=1
if enable_cache:
    cache={}
catalog_ip = os.getenv("CATALOG_SERVER")
if not catalog_ip:
    catalog_ip=socket.gethostbyname("CATALOG_SERVER")
catalog_port=8080
order_server_num=int(os.getenv("ORDER_SERVER_NUM"))
order_ips = []
for i in range(order_server_num):
    server_name="ORDER_SERVER_"+str(i)
    order_ip=os.getenv(server_name)
    if not order_ip:
        order_ip=socket.gethostbyname(server_name)
    order_ips.append(order_ip)
order_port=8081
current_order_server_id=1


class Handler(BaseHTTPRequestHandler):

    def do_GET(self):
        global cache
        print(threading.currentThread().getName())
        stock_name=self.path[1:]
        
        if not enable_cache or not (stock_info:=cache.get(stock_name)):
            s = socket.socket()        
            # connect to the catalog server
            s.connect((catalog_ip,catalog_port))
            s.send(("lookup "+self.path[1:]).encode())#the stock name can be obtained from self.path
            # receive data from the server and decoding to get the string.
            reply=s.recv(1024).decode()
            # close the connection
            s.close()
            #the reply string consists of status code of the operation, along with fields of the corresponding stock, seperated by space
            reply=reply.split(" ") 
        else:
            reply = [1,stock_name]+stock_info
        status_code=(int)(reply[0])
        if status_code > 0:
            if enable_cache:
                cache[reply[1]]=reply[2:]
            reply_json={
                "data": {
                    "name": reply[1],
                    "price": reply[2],
                    "quantity": reply[4],
                    "current trade volume": reply[3],
                    "trade volume limit": reply[5]
                }
            }
        else:
            err_msg={-100:"invaild command",-1:"stock not exists"}
            reply_json={
                "error": {
                    "code": status_code,
                    "message": err_msg[status_code]
                }
            }
        self.send_response(200)
        self.end_headers()
        self.wfile.write(str(reply_json).encode())
    def do_POST(self):
        global current_order_server_id
        print(threading.currentThread().getName())
        content_len = int(self.headers.get('Content-Length'))
        post_body = self.rfile.read(content_len)#get the body of the request
        json_arg=json.loads(post_body)  #convert json string to python dictionary

        s = socket.socket()        
        # try to connect to the order server, if fails, try other replicas in a round-robin manor until all replica are tried, the newly connected replica will become the new leader
        i=0
        while True:
            try:
                s.connect((order_ips[current_order_server_id],order_port))
            except Exception as e:
                print(e)
                i=i+1
                if i>= order_server_num:
                    print("all order server replicas are not available currently")
                    reply=-201
                    break
                current_order_server_id=(current_order_server_id+1)%order_server_num
                print("leader down, trying replica "+str(current_order_server_id))
                continue
            
            quantity=json_arg["quantity"] 
            if json_arg["type"] == "sell":
                quantity=-quantity
            try:    
                s.send(("trade "+json_arg["name"]+" "+str(quantity)).encode())
            except Exception as e:
                continue
            # receive data from the server and decoding to get the string.
            reply=int(s.recv(1024).decode())
            break
        # close the connection
        s.close()
        #the reply for trade operation is a single status code 
        if reply >= 0:#transaction number(an unsigned number start from 0) will be returned if the action is successful, 
            if enable_cache:
                cache.pop(json_arg["name"],None) #if the transaction success, invaildate the coresponding cached value,if there is any.
            reply_json={
                "data": {
                    "transaction_number": reply
                }
            }
        else:
            err_msg={-201:"cant connect to order server", -200:"cant connect to the catalog server",-100:"invaild command",-3:"not enough stock remaining", -2:"trade volume reaches upper bound",-1:"stock not exists"}
            reply_json={
                "error": {
                    "code": reply,
                    "message": err_msg[reply]
                }
            }
        self.send_response(200)
        self.end_headers()
        self.wfile.write(str(reply_json).encode())
        
class ThreadingSimpleServer(ThreadingMixIn, HTTPServer):
    pass

def run():
    server = ThreadingSimpleServer(('0.0.0.0', 8082), Handler)
    server.serve_forever()


if __name__ == '__main__':
    enable_cache=sys.argv[1]

    run()