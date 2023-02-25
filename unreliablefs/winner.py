import time

with open("/users/Girish/fusemnt/winner", "w+") as f:
    #f.write("No winner yet!")
    f.flush()

cooldown_sec = 5
node = "A"
common_file = "/users/Girish/fusemnt/winner"
start_time = int(time.time())
print ("start time - ", start_time)

msgsz_set = [400*i for i in range(1,41)] # in bytes
msgsz_set = [15]

# start expt
for msgsz in msgsz_set:
    for i in range(1,101):
        # start each round @ approx fixed times
        # this will help us find who the winner is in each round
        while time.time() < start_time+cooldown_sec*i: continue
        print(time.time())
        f = open(common_file, "w+")
        msg = "A" * msgsz
        f.write(msg)
        f.flush()
        f.close()