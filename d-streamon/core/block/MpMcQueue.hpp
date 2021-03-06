
/**
  * MpMcQueue.hpp
  * simple multi-consumer multi-producer template queue
  * It is basically made up of a list plus a mutex
  * The queue accepts and returns shared_pointers and retains shared ownership of the enqueued objects.
  */

#ifndef _MPMCQUEUE_H_
#define _MPMCQUEUE_H_ 

#include <list>
#include <iostream>
#include <memory>
#include <thread>
#include <algorithm>

namespace bm { 

    template<typename T>
    class MpMcQueue
    {
        std::list<std::shared_ptr<T>> m_queue;
        std::mutex m_mutex;

        /**
          * helper callable object for removing an object bases in its address
          */
        struct remover
        {
            const T& cmp;
            remover( const T & r): cmp(r)
            {}
            bool operator ()(const std::shared_ptr<T>& in)
            {
                return (&cmp==in.get());
            }
        };

    public:
        
        MpMcQueue()
        : m_queue(),
          m_mutex()
        {}

        ~MpMcQueue()
        {}
        /**
          * non-copiable
          */
        MpMcQueue(const MpMcQueue&) = delete;
        MpMcQueue& operator=(const MpMcQueue&) = delete;

        void swap(MpMcQueue &other)
        {
            std::swap(m_queue, other.m_queue);
            std::swap(m_mutex, other.m_mutex);
        }

        MpMcQueue(MpMcQueue&& other)
        : m_queue(std::move(other)),
          m_mutex(std::move(m_mutex))
        {}                            

        MpMcQueue& operator=(MpMcQueue&& other)
        {
            MpMcQueue tmp(std::move(other));
            tmp.swap(*this);
            return *this;
        }

        /**
          * returns the object at the head of the queue
          * @return if a queue is not empty a shared pointer to the head object, otherwise a default-constructed shared pointer
          */
        
        std::shared_ptr<T> pop()
        {
            std::lock_guard<std::mutex> _lock_(m_mutex);
            if(m_queue.empty())
            {
                return std::shared_ptr<T>();
            }
            else
            {
                std::shared_ptr<T> tmpm(std::move(m_queue.front()));
                m_queue.pop_front();

                return tmpm;
            }
        }

        
        /**
          * removes all of the pointers to a given object
          * @param in const reference to the object to be removed (for computing the address only)
          */
        void remove(const T& in)
        {
            std::lock_guard<std::mutex> _lock_(m_mutex);
            m_queue.erase(std::remove_if(m_queue.begin(),m_queue.end(),remover(in)), m_queue.end());
        }
        /**
          * pushes a new object to the back of the queue
          * @param in a shared pointer to the object (as r-value reference)
         */ 
        void push(std::shared_ptr<T>&& in)
        {
            std::lock_guard<std::mutex> _lock_(m_mutex);
            m_queue.push_back(std::move(in));
        }

    };


} // namespace bm


#endif /* _MSGQUEUE_H_ */
