# importing the requests library
import requests
import time
import random
  
# api-endpoint
URL = "http://192.168.85.128:8082/AidenCo"
  

  
trade_after_lookup_possibility=0.2
# data to be sent to api
data = "{    \"name\": \"AidenCo\",    \"quantity\": 1,    \"type\": \"sell\" }"


get_i=1
get_sum=0
post_i=1
post_sum=0
while(1):
    time1=time.time()
    r = requests.get(url = URL)
    time2=time.time()
    get_sum=get_sum+time2-time1
    print(r.text,get_sum/get_i)
    get_i=get_i+1

    if random.randrange(0,1)<trade_after_lookup_possibility:
        time1=time.time()
        r = requests.post(url = URL, data = data)
        time2=time.time()
    # extracting data in json format
 
        post_sum=post_sum+time2-time1    
        # printing the output
        print(r.text,post_sum/post_i)
        post_i=post_i+1
URL = "http://192.168.85.128:8082/AidenCo"
  

