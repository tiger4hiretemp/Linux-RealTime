# Linux-RealTime

I once worked on a project where we hit a problem...  We needed a highly available thread. The solution to that is to use a FIFO scheduled thread and to have it never yield. This thread is always available to deal with low-latency requests. (When doing this, you must be careful not to call OS level functions that yield the thread). This solved the problem. However, after a few minutes the system becomes unstable.
After much head-scratching we decided the cause must have been the kworker-threads. These threads do work for the OS and the OS normally launches one per CPU. If you don't service these threads the OS slowly degrades...

In the end, we produced a stable system, by inserting yields and hoping the control would be returned in-time to meet our hard deadlines. It worked, but hardly a rock-solid solution. As an alternative, we looked at Linux-RT OS's. These exist for a reason, common concensus is that Linux (non-RT) is simply unable to give garaunteed respnose-times. The scheduler is hard-coded to use a minimum-time quanta for scheduling. (I beleive that quanta is known as the Linux-Jiffy).

However, when trying these RealTime-OS's we quickly discovered that the hardware-driver support was minimal. Most hardware companies simplydon't think the market is big enough to develop drivers for RealTime Linux.

Given the choice between an imperfect solution or having to write drivers for every piece of software, we picked the imperfect solution.

However, it has always bugged me that you can't consume 100% of a CPU on Linux, even when running on a machine with many hardware threads.

Then it hit me... Use more than one thread... (duhh!)

1) Create an object/function that is polled. This object/function represents the task you want to be "highly-available". That is you want it to be waiting in a busy loop for an event. It has to return occasionally, but it knows it will be called again very shortly (less than a "jiffy").
2) Create a pair of threads locked to hardware threads (I've not tested, but I assume only threads on different cores will work - no hyper-threading please). Lock them to those cores using thread-affinity.
3) One thread polls the function/object with no yields. One thread spins in non-realtime priority.
4) When the spinning thread dectects enough time has passed, it changes it's scheduling to Realtime and signals it wants to take up the work.
5) The current RealTime thread, signals it is has seen the flag and relinquishes it's RealTime status. This allows the kworker to run, which makes Linux OS happy.
6) When the incoming thread sees that the current thread has reliquished, it starts running the target object/function.

## An analogy
Imagine the PrimeMinister is visiting a station. Being very important, the station staff want to keep a train with it's engine running at all times, so the prime minister can simply board a train and leave. Running the engine though uses fuel. If the PM does not leave soon enough, he will have to wait for the train to be refueled before he can leave, and you will look silly.
Then it occurs to you, you gaining a new train (for the PM) on top of your normal service train, you know, for less-important-persons (LIPs). Why not swap roles of the trains when the service train returns to the station from it's normal route. It then waits for the PM, while the PM's train is refueled and then runs the normal train schedule.
It's a good analogy, obviously the both trains need to carry enough fuel to both run the normal train service then run the engine while it waits, otherwise the waiting train will still run out fuel waiting for the other train to return. In the same way, the CPU's must be capable of fulfilling the OS functions whilst not in RT-priority. In the CPU case, this is almost certainly true.

## My test code
1) I create some dummy background threads to make the system busy (which would normally cause the scheduler to start time-slicing threads, lowering the "availability" of the all threads)
2) I loop pushing a new "now" clock reading into a global variable.
3) The high-availablity task should always be running, it looks for a new time, and calculates how long ago it was set. It adds this time-difference to a counter. It then resets the time and loops for it to change again.
4) I also measure the time between update-calls. I call this "detectable downtime" and is roughly how much time is spent in the high-level-loop.
On my machine, that is around 6ms/10s, which seems acceptable.


## But wait... Dont CPU's have different clocks!
Yes, but the "steady_clock" is garanteed to be the same between CPUs/threads. Unfortunately the steady clock is not very accurate. It might seem to be counter-intuative, but the accuracy of the clock is unimportant. This is due to the fact the the clock is not syncronised. Say the accuracy is 1s and the task takes 100ms. If you used it like a stop watch, starting the clock, stoping it, and adding the difference, you would always get 0. However, if you just glance a wall-clock, then it may be just about to "tick". It is now random whether the tick happens in 1ms or 999ms time. If you did your tests 100 times, you would expect the clock to tick over 10 times. 10/100 = 1/10 of a second, or 100ms. On average the time is still correct.

# Building the code
I use tup. The code will only work on linux, and tup only works on linux (and is extremely easy to use). You can install from "apt".
> tup init  
> tup  
> sudo RTTests.exe  

you need to be root to use realtime scheduling on Linux. If you don't trust me, there are other ways to run RT processes, see [here](https://unix.stackexchange.com/questions/736481/grant-permission-to-run-process-with-fifo)
