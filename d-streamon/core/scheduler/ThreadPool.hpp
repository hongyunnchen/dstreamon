#ifndef _THREADPOOL_HPP_
#define _THREADPOOL_HPP_ 

/**
  * ThreadPool.hpp
  * This class manages the threads in a thread pool and binds them to a local scheduler.
  */


#include <mutex>
#include <map>

#include <ThreadHandler.hpp>
#include <Scheduler.hpp>





namespace bm
{
    /**
      * forward declaration
      */
    class Block;

    class ThreadPool
    {
        Scheduler m_scheduler;
        std::vector<unsigned int> m_affinity;
        bool m_running;
        const int m_threadnum;
        std::vector<std::unique_ptr<ThreadHandler> > m_threads;
    public:
        /**
          * object constructor
          * @param tnum the number of threads in the pool
          * @param affinity a vector specifying all of the allowed cores for the threads in this pool. The affinity will be the same for every thread. An empty vector stands for no affinity specification..
          */
        ThreadPool(int tnum, const std::vector<unsigned int>& affinity):
        m_scheduler(tnum), 
        m_affinity(affinity), 
        m_running(false),
        m_threadnum(tnum), 
        m_threads()
        {
            for (int i=0; i<m_threadnum; ++i)
            {
                m_threads.push_back(std::unique_ptr<ThreadHandler> (new ThreadHandler(m_scheduler,i)));
            }

        }

        ~ThreadPool()
        {}	

        /**
          * assigns a block to this thread pool
          * @ param b a shared pointer to the assigned block
          */
        void add_task(std::shared_ptr<Block> b)
        {
            if(m_running) throw std::runtime_error("cannot add task when thread pool is already running");
            m_scheduler.add_block(std::move(b));
        }

        /**
          * removes a block from the pool.
          * Notice that lookup is done based on the block address
          * @param b a const reference to the block to be removed (only used for computing the address)
          */
        void remove_task(const Block& b)
        {
            if(m_running) throw std::runtime_error("cannot remove task when thread pool is already running");
            m_scheduler.remove_block(&b);
        }

        /**
          * starts all of the threads in the pool
          */
        void run()
        {
            if(m_running) throw std::runtime_error("thread pool il already running");

            for (int i = 0; i < m_threadnum; ++i)
            {
                m_threads[i]->run(m_affinity);
            }
            m_running=true;
        }

        /**
          * stops all of the threads in the pool
          */
        void stop()
        {
            if(!m_running) throw std::runtime_error("thread pool il not running");
            for (int i = 0; i < m_threadnum; ++i)
            {
                m_threads[i]->stop();
            }
            m_running=false;
        }

    };

}//namespace bm

#endif /* _THREADPOOL_HPP_ */
