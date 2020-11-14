/**
* 通过条件变量与queue实现一个线程安全的队列
*/

#ifndef SERVER_THREAD_SAFE_QUEUE_HPP
#define SERVER_THREAD_SAFE_QUEUE_HPP

#include <iostream>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace zhaocc {

    template<class T>
    class ThreadSafeQueue {
    private:
        std::queue<T> data_queue;
        mutable std::mutex queue_mutex; // 队列互斥元，mutable代表永久可变
        std::condition_variable queue_cond; // 队列条件变量

    public:
        ThreadSafeQueue() = default;

        ThreadSafeQueue(const ThreadSafeQueue& other) {
            // std::cout << "ThreadSafeQueue copy constructor" << std::endl;
            std::lock_guard<std::mutex> lock(other.queue_mutex); // 锁定被拷贝对象的数据队列
            data_queue = other.data_queue;
        }

        ThreadSafeQueue(ThreadSafeQueue&& other)  noexcept {
            // std::cout << "ThreadSafeQueue move constructor" << std::endl;
            std::lock_guard<std::mutex> lock(other.queue_mutex); // 锁定被拷贝对象的数据队列
            data_queue = std::move(other.data_queue);
        }

        /**
        * empty判断队列是否为空
        * @return true代表为空，false则代表非空
        */
        bool empty() const { // const表示该函数无法修改任何对象的属性，queue_mutex用mutable描述，代表它可变。
            std::lock_guard<std::mutex> lock(queue_mutex);
            return data_queue.empty();
        }

        /**
        * 往尾部添加元素
        * @param new_value: 新元素
        */
        void push(const T& new_val) {
            std::lock_guard<std::mutex> lock(queue_mutex);
            data_queue.push(new_val);
            queue_cond.notify_one(); // 随机唤醒一个线程
        }

        /**
        * 往尾部添加元素
        * @param new_value: 新元素
        */
        void push(T&& new_val) {
            std::lock_guard<std::mutex> lock(queue_mutex);
            data_queue.push(new_val);
            queue_cond.notify_one(); // 随机唤醒一个线程
        }


        /**
        * 阻塞的等待有元素并返回弹出头部元素
        * @param value: 用于获取元素的引用
        */
        void pop(T& new_val) {
            std::unique_lock<std::mutex> lock(queue_mutex); // 这里锁定不能使用lock_guard，因为lock_guard锁定后无法灵活的解锁，只能析构时解锁
            queue_cond.wait(lock, [this]() { return !data_queue.empty(); }); // 锁会在wait过程中解锁，一直等到lambda表达式成立
            new_val = data_queue.front();
            data_queue.pop();
        }

        /**
         * 阻塞的等待有元素并返回弹出头部元素
         * @return shared pointer指向元素
         */
        std::shared_ptr<T> pop() {
            std::unique_lock<std::mutex> lock(queue_mutex); // 这里锁定不能使用lock_guard，因为lock_guard锁定后无法灵活的解锁，只能析构时解锁
            queue_cond.wait(lock, [this]() { return !data_queue.empty(); }); // 锁会在wait过程中解锁，一直等到lambda表达式成立
            std::shared_ptr<T> res(data_queue.front());
            data_queue.pop();
            return res;
        }

        /**
        * 尝试返回弹出头部元素
        * @param value: 用于获取元素的引用
        * @return true代表有元素，false则代表没有
        */
        bool try_pop(T& new_val) {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (data_queue.empty()) {
                return false;
            }
            new_val = data_queue.front();
            data_queue.pop();
            return true;
        }

        /**
        * 尝试返回弹出头部元素
        * @return shared pointer指向元素，如果没元素则返回空指针
        */
        std::shared_ptr<T> try_pop() {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (data_queue.empty()) {
                return std::shared_ptr<T>();
            }
            std::shared_ptr<T> res(data_queue.front());
            data_queue.pop();
            return res;
        }
    };
}

#endif //SERVER_THREAD_SAFE_QUEUE_HPP
