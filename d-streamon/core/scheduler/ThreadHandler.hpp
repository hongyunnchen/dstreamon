#ifndef _THREADHANDLER_HPP_
#define _THREADHANDLER_HPP_ 

/**
  * ThreadHandler.hpp
  * This class represents the interface to the single thread in a thread pool and also implements the 
  * actual functor which is executed by the thread.
  * It is in charge of retrieving from the pool scheduler the next block to be executed, as well as to start up the actual thread.
  * Notice that this is a callable object and the functor method is executed within the context of the thread itself, 
  * while the other functions (mainly concerning configuration) are executed in the context of BlockMon's main thread.
  FIXME this class is not portable as it uses pthread in order to manage the affinity. To my knowledge, there is no portable way of doing that
*/  


#include <thread>
#include <memory>
#include <vector>

#if __GNUC__ == 4 &&  __GNUC_MINOR__ == 4  
#include <cstdatomic>
#else 
#include <atomic>
#endif

#include <MpMcQueue.hpp>
#include <pthread.h>
#include <Block.hpp>

#include<Scheduler.hpp>

namespace bm
{

    class ThreadHandler
    {

        /**
          * this is a reference to the unique scheduler in the ThreadPool object
          */
        Scheduler& m_scheduler;    
        cpu_set_t m_mask;
        std::atomic_bool m_stop;
        std::unique_ptr<std::thread> m_thread;
        int m_id;

    public: 
        /**
          * object constructor
          * @param sched reference to the unique scheduler of this pool (which is owned by the calling ThreadPool object)
          * @param id this threads id (needed by the scheduler depending on the policy)
         */
        ThreadHandler(Scheduler& sched, unsigned int id)
        :m_scheduler(sched),m_mask(), m_stop(false), m_thread(),m_id(id)
        {}

        /**
          * This function starts up the actual thread and gives it *this as a callable object to execute.
          * This will cause the new thread to execute the operator()() method of this class
          * @param aff the CPUs the thread is allowed to run on  (an empty vector means no affinity specification)
          */
        void run(const std::vector<unsigned int>& aff)
        {
            CPU_ZERO(&m_mask);
            for(unsigned int i = 0; i<aff.size(); ++i)
            {
                if(aff[i]>CPU_SETSIZE)
                    throw std::runtime_error("affinity value hogher than maximum allowed");
                CPU_SET(aff[i],&m_mask);

            }
            m_scheduler.register_thread(m_id, aff);
            m_thread=std::unique_ptr<std::thread> (new std::thread(std::ref(*this)));
        }

        /**
          * This is the actual function that the thread executes.
          * It receives a block from the scheduler, class its run() method and returns it to the scheduler.
          * The thread runs until the it is told to stop by setting the m_stop boolean flag
          */
        void operator()()
        {
            if(CPU_COUNT(&m_mask)>0)
            {
                pthread_t id = pthread_self();
               int ret = pthread_setaffinity_np(id, sizeof(m_mask), &m_mask);
                if(ret != 0)
                {
                    perror("setaffinity");
                    throw(std::runtime_error("set affinity failed"));
                }
            }
            while(!m_stop.load())
            {

                std::shared_ptr<Block>torun(m_scheduler.next_task(m_id));
                if(torun)
                {
                    torun->run();
                    m_scheduler.task_done(m_id, std::move(torun));
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            m_stop.store(false);
        }

        /**
          * Tells the running thread to stop and blocks until it terminates
          */
        void stop()
        {
            m_stop.store(true);
            m_thread->join();
            m_thread.reset();
        }

    };

}

#endif /* _THREADHANDLER_HPP_ */
