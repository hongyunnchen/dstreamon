/**
  * TimerThread.hpp
  * this class represent the callable object which is executed by the timer thread and that handles
  * timers for the whole BlockMon instance.
  * It keeps an ordered timer heap, waits until the next timer is to be executed and calls the associated block's handle_timer function. 
  * In order not to waste too much CPU on busy waiting, the threads checks the timer heap every  millisecond and handles all of the expired timers.
  * This is an obvious trade-off between precision and resource consumption that can be tuned by changing the sampling interval.
  * When a block sets up a new timer, this is inserted into a thread-safe timer queue. When the threads wakes up, all of the timers in the queue are pushed into the timer heap.
  * Notice that, if the block is passive, the block's timer handling function is executed in the context of this thread.
  * Notice also that this is a singleton class (there can be only one instance): therefore only one timer thread is allowed, thus guaranteeing that the timers are strictly ordered.
  * For these reason, timer handling functions that involve a lot of processing should be avoided (unless the block is always meant to be active).
  */


#ifndef _TIMERTHREAD_HPP_
#define _TIMERTHREAD_HPP_ 

#include <iostream>
#include <algorithm>
#include <vector>
#include <thread>
#include <mutex>

#include <Timer.hpp>

namespace bm { 

    class TimerThread
    {
        /**
          * class constructor
          * private,as in Meyer's singleton
          */
        TimerThread()
        : m_stop(false),
          m_running(false),
          m_queue(),
          m_heap(),
          m_queue_mutex()
        {
            std::make_heap(m_heap.begin(),m_heap.end(),hsorter());
        }

        ~TimerThread()
        {}

        /**
          * this object is non-moveable and non-copiable
          */
        TimerThread(const TimerThread&) = delete;
        TimerThread& operator=(const TimerThread&) = delete;
        TimerThread(TimerThread&&) = delete;
        TimerThread& operator=(TimerThread&&) = delete;

        /**
          * Callable object class used to sort the timer heap
          */
        struct hsorter
        {
            bool operator()(const std::shared_ptr<Timer>& t1,const std::shared_ptr<Timer>& t2) const
            {
                return t1->time_point() > t2->time_point();
            }
        };

        /**
          * private helper function used to insert a timer in the heap
          * @param in a shared pointer to the timer to be inserted (as r-value reference)
          */
        void insert_in_heap(std::shared_ptr<Timer>&& in)
        {
            m_heap.push_back(std::move(in));
            std::push_heap(m_heap.begin(),m_heap.end(),hsorter());
        }

        /**
          * private helper function used to pop from the heap the timer with the earliest expiration time point
          * @return  a shared pointer to the timer popped from the heap
          */
        std::shared_ptr<Timer> remove_from_heap()
        {
            std::pop_heap(m_heap.begin(),m_heap.end(),hsorter());
            std::shared_ptr<Timer> tmp(std::move(m_heap.back()));
            m_heap.pop_back();
            return tmp;
        }

        /** 
          * helper callable objects
          * used in order to remove from the heap all of the timers associated to a given block
          */
        struct remover
        {
            const Block& m_ref;
            remover(const Block& i):m_ref(i)
            {}
            bool operator()(const std::shared_ptr<Timer>& in)
            {
                return (&m_ref==&(in->owner()));
            }
        };
    public:
        
        /**
          * unique instance accessor as in Meyer's singleton
          * @return a reference to the only instance of this class
          */
        static TimerThread& 
        instance()
        {
            static TimerThread t;
            return t;
        }
        
        /**
          * This is called by the block (through its base class functions) in order to schedule a timer
          * @param in a shared pointer to the timer to be scheduled (as r-value reference).
          * The timer is provisionally stored into a timer queue and will be pushed into the heap the next time this thread wakes up.
          */
        void schedule_timer(std::shared_ptr<Timer>&& in)
        {
            std::lock_guard<std::mutex> tmp(m_queue_mutex);
            m_queue.push_back(std::move(in));
        }

        /**
          * Notifies the timer thread functor that is should stop by setting a boolean flag
          */
        void stop()
        {
            m_stop=true;
        }

        /**
          * This is the actual function which is executed by the timer thread
          * It checks the timer queue every millisecond and handles all of the expired timers.
          * After handling one timer (i.e. calling the handle_timer method of the associated block) it 
          * checks whether it needs to be rescheduled by calling its next_time virtual function.
          * It runs until it is signalled to stop through the stop() method
          */
        void operator()()
        {
            if(!m_running)
            {
                m_running=true;
            }
            else
            {
                throw std::runtime_error("trying to start timer thread while it is already running");
            }

            while(!m_stop)
            {
                {
                    std::lock_guard<std::mutex> tmp(m_queue_mutex);
                    while (!m_queue.empty())
                    {
                        insert_in_heap(std::move(m_queue.back()));
                        m_queue.pop_back();
                    }
                }
                std::chrono::system_clock::time_point tnow(std::chrono::system_clock::now());
                while((!m_heap.empty())&&(m_heap.front()->time_point() <= tnow))
                {
                    std::shared_ptr<Timer> t(remove_from_heap());
                    t->owner().handle_timer(t);
                    std::chrono::system_clock::time_point ttmp(t->time_point());
                    t->next_time();
                    if(t->time_point()!= ttmp )
                    {
                        insert_in_heap(std::move(t));
                    }
                }
                /**
                  * this time interval can be changed in order to tune the precision/performance trade off
                  */
                std::this_thread::sleep_for(std::chrono::milliseconds(1));//timers queue is sampled every 1 msec
            }

            m_running=false;
            m_stop=false;
        }

        /**
          * Removes both from the timer heap and the timer queue all of the timers associated to a given block.
          * This is needed when a block is removed, otherwise invalid references to is would be left.
          * This function is called by the destructor of the block superclass and can only be executed when the timer thread is stopped
          * @param a const reference to the Block whose timers are to be removed (it is only used to derive the block address)
          */
        void remove_references(const Block& b)
        {
            if(m_running)
            {
                throw std::runtime_error("timer thread::cannot remove references while the thread is running");
            }

            m_heap.erase( std::remove_if(m_heap.begin(),m_heap.end(),remover(b)),m_heap.end());
            m_queue.erase( std::remove_if(m_queue.begin(),m_queue.end(),remover(b)),m_queue.end());
        }

    private:
        bool m_stop;
        bool m_running;
        std::vector<std::shared_ptr<Timer> > m_queue;
        std::vector<std::shared_ptr<Timer> > m_heap;
        std::mutex m_queue_mutex;
    };

} // namespace bm

#endif /* _TIMERTHREAD_HPP_ */
