#include "threadpool.h"


Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{}

void Thread::start()
{
	// ����һ���߳���ִ��һ���̺߳��� pthread_create
	std::thread t(func_, threadId_);  // C++11��˵ �̶߳���t  ���̺߳���func_
	t.detach(); // ���÷����߳�   pthread_detach  pthread_t���óɷ����߳�
}

int Thread::getId()const
{
	return threadId_;
}

int Thread::generateId_ = 0;

ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;

	// �ȴ��̳߳��������е��̷߳���  ������״̬������ & ����ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threshhold;
	}
}

void ThreadPool::start(int initThreadSize)
{
	// �����̳߳ص�����״̬
	isPoolRunning_ = true;

	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// �����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		// ����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// threads_.emplace_back(std::move(ptr));
	}

	// ���������߳�  std::vector<Thread*> threads_;
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); // ��Ҫȥִ��һ���̺߳���
		idleThreadSize_++;    // ��¼��ʼ�����̵߳�����
	}
}

void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();

	// �����������ִ����ɣ��̳߳زſ��Ի��������߳���Դ
	for (;;)
	{
		Task task;
		{
			// �Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id()
				<< "���Ի�ȡ����..." << std::endl;

			// cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����߳�
			// �������յ�������initThreadSize_�������߳�Ҫ���л��գ�
			// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s

			// ÿһ���з���һ��   ��ô���֣���ʱ���أ������������ִ�з���
			// �� + ˫���ж�
			while (taskQue_.size() == 0)
			{
				// �̳߳�Ҫ�����������߳���Դ
				if (!isPoolRunning_)
				{
					threads_.erase(threadid); // std::this_thread::getid()
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
						<< std::endl;
					exitCond_.notify_all();
					return; // �̺߳����������߳̽���
				}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// ������������ʱ������
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							// ��ʼ���յ�ǰ�߳�
							// ��¼�߳���������ر�����ֵ�޸�
							// ���̶߳�����߳��б�������ɾ��   û�а취 threadFunc��=��thread����
							// threadid => thread���� => ɾ��
							threads_.erase(threadid); // std::this_thread::getid()
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
								<< std::endl;
							return;
						}
					}
				}
				else
				{
					// �ȴ�notEmpty����
					notEmpty_.wait(lock);
				}
			}

			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id()
				<< "��ȡ����ɹ�..." << std::endl;

			// �����������ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// �����Ȼ��ʣ�����񣬼���֪ͨ�������߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// ȡ��һ�����񣬽���֪ͨ��֪ͨ���Լ����ύ��������
			notFull_.notify_all();
		} // ��Ӧ�ð����ͷŵ�

		// ��ǰ�̸߳���ִ���������
		if (task != nullptr)
		{
			task(); // ִ��function<void()> 
		}

		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now(); // �����߳�ִ���������ʱ��
	}
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}