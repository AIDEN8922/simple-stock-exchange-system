#pragma once
#include<thread>
#include<vector>
#include<queue>
#include<condition_variable>
#include<utility>
#include<iostream>
#include<unistd.h>
#include<signal.h>
#include<memory>

template<typename Job>
class ThreadPool {
    typedef std::unique_ptr<std::thread> ThreadUniptr;
public:
    void Start(int initial_thread_num) {
        threads.reserve(initial_thread_num);
        for (int i = 0; i < initial_thread_num; i++) {
            threads.emplace_back(new std::thread(&ThreadPool<Job>::ThreadLoop, this));   
        }
    }
    void QueueJob(const Job& job) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            jobs.push(job);
        }
        mutex_condition.notify_one();
    }
    
    virtual ~ThreadPool(){
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            should_terminate = true;
        }
        mutex_condition.notify_all();
        for (int i = 0; i < threads.size();i++) {
            threads[i]->join();
        }
        
    }
    bool IsBusy() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        return !jobs.empty();
    }

private:
    void ThreadLoop() {
        sigset_t set;
        // Block SIGINT in worker thread
        sigemptyset(&set);
        sigaddset(&set, SIGINT);  
        //we need to make sure the signal handling only happens at main thread, bacause to are going to notify the worker threads and waits for them to terminate
        //in the signal handler. if the handler is executed within a worker thread, that thread is to waiting for every worker thread to terminate(including itself)
        //before terminated itself, which essentially causes a deadlock(which is why we see segmentfault printed at the end of output had we not add these few line of codes)
        pthread_sigmask(SIG_BLOCK, &set, NULL);
        while (true) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                if (jobs.empty() && !should_terminate) {
                    mutex_condition.wait(lock, [this] {
                        return !jobs.empty() || should_terminate;
                        });
                }
                if (should_terminate) {
                    std::cout << "worker thread terminated" << std::endl;
                    return;
                }
                job = jobs.front();
                jobs.pop();
            }
            job();
        }
    }
   
    bool should_terminate = false;           // Tells threads to stop looking for jobs
    std::mutex queue_mutex;                  // Prevents data races to the job queue
    std::condition_variable mutex_condition; // Allows threads to wait on new jobs or termination 
    std::vector<ThreadUniptr> threads;       // has to be pointers because vector need to "copy" threads, but threads dont have copy ctor
    std::queue<Job> jobs;
};