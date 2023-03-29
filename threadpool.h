#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
// using namespace std;

//线程安全的队列
template<typename T>
class SafeQueue
{
private:
    std::queue<T> m_queue;  //利用模板函数构造队列
    std::mutex m_mutex; //互斥锁
public:
    SafeQueue(){};
    SafeQueue(SafeQueue &&other){};
    ~SafeQueue(){};

    bool empty(){
        std::unique_lock<std::mutex> lock(m_mutex);     //加锁，防止被修改
        return m_queue.empty();
    }

    int size(){
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    //队列中添加元素
    void enqueue(T &t){
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.emplace(t);
    }

    //队列中取出元素
    bool dequeue(T &t){
        std::unique_lock<std::mutex> lock(m_mutex);
        if(m_queue.empty()){
            return false;
        }
        t = std::move(m_queue.front()); //取出队首元素，返回队首元素值，进行右值应用
        m_queue.pop();
        return true;
    }
};



class ThreadPool
{
private:
    bool m_shutdown;    //线程池是否关闭
    SafeQueue<std::function<void()>> m_queue;   //任务队列
    std::vector<std::thread> m_threads;     //工作线程队列
    std::mutex m_mutex; //互斥锁
    std::condition_variable m_cond; //条件变量
    class ThreadWorker  //内置工作线程类
    {   
    private:
        int m_id;   //工作id
        ThreadPool* m_pool; //所属线程池
    public:
        //构造函数
        ThreadWorker(ThreadPool* pool, const int id): m_id(id), m_pool(pool){};

        //重载()操作
        void operator()(){
            std::function<void()> func; //定义基础函数类
            bool dequeued;  //是否正在取出队列元素
            while(!m_pool->m_shutdown)
            {
                {
                    //为线程环境加锁
                    std::unique_lock<std::mutex> lock(m_pool->m_mutex);
                    //如果任务队列为空，阻塞当前线程
                    if(m_pool->m_queue.empty())
                    {
                        m_pool->m_cond.wait(lock);  //等待条件变量通知，开启线程
                    }

                    //取出队列中的元素
                    dequeued = m_pool->m_queue.dequeue(func);
                }
                //如果成功取出，执行工作函数
                if(dequeued)
                    func();
            }
        }
        
    };
public:
    //线程池构造函数
    ThreadPool(const int n_threads = 4)
    : m_threads(std::vector<std::thread>(n_threads)), m_shutdown(false){}

    /*禁止C++中线程池（ThreadPool）类的复制和移动构造函数以及复制和移动赋值运算符的使用。
    具体地说，这些代码是用C++11中的删除函数语法来实现的，它们指定了不允许编译器自动生成默认的拷贝构造函数和拷贝赋值运算符。
    使用=delete语法可以让编译器在编译期间发出错误，提示程序员该函数是禁止使用的。
    这种设计可能是出于线程池的资源管理原则，避免了线程池复制或移动后资源的重复分配，造成资源浪费和线程安全性问题。*/
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&) = delete;
    ThreadPool &operator = (const ThreadPool&) = delete;
    ThreadPool &operator = (ThreadPool&&) = delete;

    void init()
    {
        for(int i = 0; i < m_threads.size(); ++i)
        {
            m_threads.at(i) = std::thread(ThreadWorker(this, i));   //分配工作线程
        }
    }

    void shutdown()
    {
        m_shutdown = true;
        m_cond.notify_all();    //通知，唤醒所有工作线程
        for(int i = 0; i < m_threads.size(); ++i)
        {
            if(m_threads[i].joinable()) //判断线程是否在等待
            {
                m_threads[i].join();    //是的话将线程加入到等待队列
            }
        }


    }

    //负责向任务队列中添加任务，线程池中最重要的方法
    template<typename F, typename... Args>  //可变模板参数
    auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>
    {
        //连接函数和参数定义，特殊函数类型
        std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

        std::function<void()> warpper_func = [task_ptr](){
            (*task_ptr)();
        };

        //队列通用安全封包函数，并压入安全队列
        m_queue.enqueue(warpper_func);

        m_cond.notify_one();

        //返回先前注册的任务指针
        return task_ptr->get_future();
    }
};


