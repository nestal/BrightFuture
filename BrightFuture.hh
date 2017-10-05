/*
	Copyright © 2017 Wan Wai Ho <me@nestal.net>
	Distributed under the Boost Software License, Version 1.0.
	(See accompanying file LICENSE_1_0.txt or copy at
	http://www.boost.org/LICENSE_1_0.txt)
*/

//
// Created by nestal on 10/3/17.
//

#pragma once

#include <cassert>
#include <deque>
#include <future>
#include <map>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

namespace BrightFuture {

struct Token;
class TaskBase;
class Executor;

class TaskBase
{
public:
	virtual ~TaskBase() = default;
	virtual void Execute() = 0;
};

class Executor
{
public:
	virtual ~Executor() = default;
	virtual void Execute(std::shared_ptr<TaskBase>&& task) = 0;
	virtual Token Add(std::shared_ptr<TaskBase>&& task) = 0;
	virtual void Schedule(Token token) = 0;
};
/**
 * \brief A token to represent a task to be called.
 *
 * It is returned by TaskScheduler::Add() to represent the task being added. Pass the token
 * to TaskScheduler::Schedule() to run that task.
 */
struct Token
{
	Executor        *host;
	std::intptr_t   event;
};

template <typename ConcreteExecutor>
class ExecutorBase : public Executor
{
public:
	ExecutorBase() = default;

	Token Add(std::shared_ptr<TaskBase>&& task) override
	{
		std::unique_lock<std::mutex> lock{m_task_mutex};
		auto event = m_seq++;
		m_tasks.emplace(event, std::move(task));
		return {this, event};
	}

	void Schedule(Token token) override
	{
		std::shared_ptr<TaskBase> task;
		{
			std::unique_lock<std::mutex> lock{m_task_mutex};
			auto it = m_tasks.find(token.event);
			if (it != m_tasks.end())
			{
				task = std::move(it->second);
				it = m_tasks.erase(it);
			}
		}
		
		// m_task_mutex does not protect m_executor, which is thread-safe.
		if (task)
			Execute(std::move(task));
	}

	void Execute(std::shared_ptr<TaskBase>&& task) override
	{
		// Use CRTP to call ConcreteExecutor::Execute().
		static_cast<ConcreteExecutor*>(this)->Execute([task=std::move(task)]{ task->Execute(); });
	}

private:
	std::mutex  m_task_mutex;
	std::unordered_map<std::intptr_t, std::shared_ptr<TaskBase>>    m_tasks;
	std::intptr_t m_seq{0};
};

template <typename Arg, typename Callable>
struct ReturnType
{
	using Type = typename std::result_of<Callable(Arg&&)>::type;
};

template <typename Callable>
struct ReturnType<void, Callable>
{
	using Type = typename std::result_of<Callable()>::type;
};

template <typename Arg, typename Callable>
class Continuation : public TaskBase
{
public:
	// Return value of "Function". It may be void.
	using Ret = typename ReturnType<Arg, Callable>::Type;
	
	auto Result()
	{
		return m_return.get_future();
	}

protected:
	Continuation(Callable&& func, std::future<Token>&& cont) :
		m_function{std::move(func)}, m_cont{std::move(cont)}
	{
	}
	
	/**
	 * \brief Call the continuation routine specified by \a token
	 *
	 * Token represent a continuation routine in an executor. This function tries to call the continuation
	 * routine specified by a Token. If the token is not ready yet, that means the continuation routine
	 * is not specified yet, i.e. Future::Then() is not yet called. In this case we do nothing here, the
	 * continuation routine will be scheduled later when Future::Then() is called.
	 */
	void TryContinue()
	{
		assert(m_cont.valid());
		if (m_cont.wait_for(std::chrono::seconds::zero()) == std::future_status::ready)
		{
			auto t = m_cont.get();
			if (t.host)
				t.host->Schedule(t);
		}
	}

	std::promise<Ret>       m_return;   //!< Promise to the return value of the function to be called.
	Callable                m_function; //!< Function to be called in Execute().
	std::future<Token>      m_cont;     //!< Token of the continuation routine that consumes the
										//!< return value, after it is ready.
};

template <typename Arg, typename Function>
class ConcreteTask : public Continuation<Arg, Function>
{
private:
	using Base = Continuation<Arg, Function>;
	
public:
	ConcreteTask(std::future<Arg>&& arg, Function&& func, std::future<Token>&& cont) :
		m_arg{std::move(arg)}, Base{std::move(func), std::move(cont)}
	{
	}

	void Execute() override
	{
		Run();
		Base::TryContinue();
	}
	
private:
	template <typename R=typename Base::Ret>
	typename std::enable_if<!std::is_void<R>::value>::type Run()
	{
		Base::m_return.set_value(Base::m_function(m_arg.get()));
	}

	template <typename R=typename Base::Ret>
	typename std::enable_if<std::is_void<R>::value>::type Run()
	{
		Base::m_function(m_arg.get());
		Base::m_return.set_value();
	}

private:
	std::future<Arg> m_arg;      //!< Argument to the function to be called in Execute().
};

template <typename Function>
class ConcreteTask<void, Function> : public Continuation<void, Function>
{
private:
	using Base = Continuation<void, Function>;

public:
	ConcreteTask(Function&& func, std::future<Token>&& cont) :
		Base{std::move(func), std::move(cont)}
	{
	}

	void Execute() override
	{
		Run();
		Base::TryContinue();
	}
	
private:
	template <typename R=typename Base::Ret>
	typename std::enable_if<!std::is_void<R>::value>::type Run()
	{
		Base::m_return.set_value(Base::m_function());
	}

	template <typename R=typename Base::Ret>
	typename std::enable_if<std::is_void<R>::value>::type Run()
	{
		Base::m_function();
		Base::m_return.set_value();
	}
};

template <typename T, typename Function>
auto MakeTask(std::future<T>&& arg, Function&& func, std::future<Token>&& cont)
{
	return std::make_shared<ConcreteTask<T, Function>>(std::move(arg), std::forward<Function>(func), std::move(cont));
}

class DefaultExecutor : public ExecutorBase<DefaultExecutor>
{
public:
	// Called by ExecutorBase using CRTP
	template <typename Func>
	void Execute(Func&& func)
	{
		std::thread{std::forward<Func>(func)}.detach();
	}
	
	static DefaultExecutor* Instance()
	{
		static DefaultExecutor exe;
		return &exe;
	}
};

class QueueExecutor : public ExecutorBase<QueueExecutor> // CRTP
{
public:
	QueueExecutor() = default;
	
	auto Run()
	{
		// Grab the whole queue while holding the lock, and then iterate
		// each function in the queue after releasing the lock.
		// This design optimizes throughput and minimize contention, but scarifies latency
		// and concurrency.
		std::deque<std::function<void()>> queue;
		{
			std::unique_lock<std::mutex> lock{m_mux};
			m_cond.wait(lock, [this] { return !m_queue.empty() || m_quit; });
			queue.swap(m_queue);
		}
		
		// Execute the function after releasing the lock to reduce contention
		for (auto&& func : queue)
			func();
		
		m_count += queue.size();
		return queue.size();
	}
	
	void Quit()
	{
		m_quit = true;
	}

	auto Spawn()
	{
		return std::thread([this]{while (Run()>0);});
	}
	
	auto Spawn(std::size_t count)
	{
		std::vector<std::thread> threads;
		std::generate_n(std::back_inserter(threads), count, [this]{return Spawn();});
		return threads;
	}
	
	auto Count() const
	{
		return m_count.load();
	}
	
	// Called by ExecutorBase using CRTP
	template <typename Func>
	void Execute(Func&& func)
	{
		std::unique_lock<std::mutex> lock{m_mux};
		m_queue.push_back(std::forward<Func>(func));
		m_cond.notify_one();
	}
	
private:
	std::mutex                          m_mux;
	std::condition_variable             m_cond;
	std::deque<std::function<void()>>   m_queue;
	std::atomic<bool> m_quit{false};
	std::atomic<decltype(m_queue.size())> m_count{0};
};

template <typename T>
class future
{
public:
	using value_type = T;
	
public:
	future() = default;
	
	// move only type
	future(future&&) noexcept = default;
	future(const future&) = delete;
	future& operator=(future&&) noexcept= default;
	future& operator=(const future&) = delete;
	
	explicit future(std::future<T>&& shared_state, std::promise<Token>&& token = {}) noexcept :
		m_shared_state{std::move(shared_state)},
		m_token{std::move(token)}
	{
	}
	
	~future()
	{
		// This is a bit tricky. If the future is destroyed without called Then(), then
		// no one will set the m_token promise. When the executor finishes running the
		// async function, it will need to wait for m_token to signal the continuation
		// routine to be called. If we have destroyed the m_token promise already,
		// the future to m_token will throw a broken_promise exception. We can't let
		// that happen.
		// So we need to check if Then() is called, and set the m_token promise to a
		// null token to tell the executor that no continuation routine is needed.
		if (m_shared_state.valid())
			m_token.set_value({});
	}

	template <typename Func>
	auto then(Func&& continuation, Executor *host = DefaultExecutor::Instance())
	{
		assert(valid());
		
		// The promise of the next continuation routine's token. It is not _THIS_ continuation
		// routine's token. We are adding the _THIS_ continuation routine (i.e. the "continuation"
		// argument) right here, so we knows the its token.
		//
		// We don't know the next continuation routine's token yet. It will be known when Then()
		// is called with the future returned by this function.
		std::promise<Token> next_token;

		// Prepare the continuation routine as a task. The function of the task is of course
		// the continuation routine itself. The argument of the continuation routine is the
		// result of the last async call, i.e. the variable referred by the m_shared_state future.
//		auto continuation_arg  = m_shared_state.share();
		bool is_ready = (m_shared_state.wait_for(std::chrono::seconds::zero()) == std::future_status::ready);
		auto continuation_task = MakeTask(std::move(m_shared_state), std::forward<Func>(continuation), next_token.get_future());
		
		// We also need to know the promise to return value of the continuation routine as well.
		// It will be passed to the future returned by this function.
		auto continuation_return_value = continuation_task->Result();
		
		// Add the continuation routine to the executor, which will run the task and set its return
		// value to the promise above. A token to the task is returned. We use this token to schedule
		// the continuation routine.
		auto continuation_token = host->Add(std::move(continuation_task));
		
		assert(continuation_token.host);
		assert(continuation_token.host == host);

		// "arg" is the argument of the continuation function, i.e. the result of the previous async
		// call. If it is ready, that means the previous async call is finished. We can directly
		// invoke the continuation function here.
		if (is_ready)
			host->Schedule(continuation_token);
		else
			m_token.set_value(continuation_token);
		
		return MakeFuture(std::move(continuation_return_value), std::move(next_token));
	}
	
	std::shared_future<T> share()
	{
		assert(valid());
		return m_shared_state.share();
	}
	
	template <typename R=T>
	typename std::enable_if<std::is_void<R>::value>::type get()
	{
		assert(valid());
		m_shared_state.get();
	}
	
	template <typename R=T>
	typename std::enable_if<!std::is_void<R>::value, T>::type& get()
	{
		assert(valid());
		return m_shared_state.get();
	}
	
	void wait()
	{
		assert(valid());
		m_shared_state.wait();
	}
	
	bool valid() const
	{
		return m_shared_state.valid();
	}
	
	bool is_ready() const
	{
		assert(valid());
		return m_shared_state.wait_for(std::chrono::seconds::zero()) == std::future_status::ready;
	}
	
	template <typename Rep, typename Ratio>
	std::future_status wait_for(const std::chrono::duration<Rep, Ratio>& duration)
	{
		assert(valid());
		return m_shared_state.wait_for(duration);
	}
	
	template <typename Clock, typename Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& time_point)
	{
		assert(valid());
		return m_shared_state.wait_until(time_point);
	}
	
	template <typename Type>
	static auto MakeFuture(std::future<Type>&& shared_state, std::promise<Token>&& token)
	{
		return future<Type>{std::move(shared_state), std::move(token)};
	}

private:
	std::future<T>      m_shared_state;
	std::promise<Token> m_token;
};

template <typename Func>
auto async(Func&& func, Executor *exe = DefaultExecutor::Instance())
{
	using T = decltype(func());
	
	std::promise<Token> cont;
	
	auto task   = std::make_shared<ConcreteTask<void, Func>>(std::forward<Func>(func), cont.get_future());
	auto result = task->Result();
	
	exe->Execute(std::move(task));
	
	return future<T>::MakeFuture(std::move(result), std::move(cont));
}

template < class InputIt >
auto when_all(InputIt first, InputIt last, Executor *exe = DefaultExecutor::Instance())
{
	using T = typename std::iterator_traits<InputIt>::value_type::value_type;
	
	struct SharedResult
	{
		std::promise<std::vector<T>>    promise;
		std::map<std::size_t, T>        values;
		std::size_t size{};
		std::mutex  mux;
	};
	auto intermediate  = std::make_shared<SharedResult>();
	auto current_index = intermediate->values.size();
	
	// copy all futures to a vector first, because we need to know how many
	// and InputIt only allows us to iterate them once.
	std::vector<future<T>> futures;
	for (auto it = first ; it != last; it++)
		futures.push_back(std::move(*it));
	intermediate->size = futures.size();
	
	for (auto&& future : futures)
		future.then([intermediate, current_index](auto&& val)
		{
			// The executor may run this function in different threads.
			// make sure they don't step in each others.
			std::unique_lock<std::mutex> lock{intermediate->mux};
			
			intermediate->values.emplace(current_index, std::move(val));
			if (intermediate->values.size() == intermediate->size)
			{
				std::vector<T> result;
				for (auto&& p : intermediate->values)
					result.push_back(std::move(p.second));
				intermediate->promise.set_value(std::move(result));
			}
		}, exe);

	return future<std::vector<T>>{intermediate->promise.get_future()};
}

} // end of namespace
