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

class Executor
{
public:
	virtual ~Executor() = default;
	virtual void Execute(std::function<void()>&& task) = 0;
	virtual Token Add(std::function<void()>&& task) = 0;
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

class TokenQueue
{
public:
	TokenQueue() = default;
	
	bool PushBack(const Token& tok)
	{
		std::unique_lock<std::mutex> lock{m_mux};
		if (m_popped)
		{
			assert(m_queue.empty());
			return false;
		}
		
		m_queue.push_back(tok);
		return true;
	}
	
	void TryContinue()
	{
		for (auto&& t : Pop())
		{
			assert(t.host);
			t.host->Schedule(t);
		}
	}
	
private:
	std::vector<Token> Pop()
	{
		std::unique_lock<std::mutex> lock{m_mux};
		assert(!m_popped);
		m_popped = true;
		
		std::vector<Token> result{std::move(m_queue)};
		return result;
	}

private:
	std::mutex m_mux;
	std::vector<Token> m_queue;
	bool m_popped{false};
};

using TokenQueuePtr = std::shared_ptr<TokenQueue>;

template <typename T, typename Inherited>
class BrightFuture
{
public:
	using value_type = T;

private:
	BrightFuture() = default;
	BrightFuture(BrightFuture&&) noexcept = default;
	BrightFuture(const BrightFuture&) = default;
	BrightFuture& operator=(BrightFuture&&) noexcept = default;
	BrightFuture& operator=(const BrightFuture&) noexcept = default;

	explicit BrightFuture(TokenQueuePtr token) noexcept : m_token{std::move(token)}
	{
	}
	~BrightFuture() = default;

	// Only allow Inherited class to construct
	friend Inherited;

public:
	void wait()
	{
		assert(valid());
		static_cast<Inherited*>(this)->InternalFuture().wait();
	}

	bool valid() const
	{
		return static_cast<const Inherited*>(this)->InternalFuture().valid() && m_token;
	}

	bool is_ready() const
	{
		return static_cast<const Inherited*>(this)->InternalFuture().wait_for(std::chrono::seconds::zero()) == std::future_status::ready;
	}

	template <typename Rep, typename Ratio>
	std::future_status wait_for(const std::chrono::duration<Rep, Ratio>& duration)
	{
		assert(valid());
		return static_cast<Inherited*>(this)->InternalFuture().wait_for(duration);
	}

	template <typename Clock, typename Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& time_point)
	{
		assert(valid());
		return static_cast<Inherited*>(this)->InternalFuture().wait_until(time_point);
	}

	template <typename Func, typename Future>
	friend auto Async(Func&& continuation, Future&& future, Executor *host);
	
protected:
	TokenQueuePtr       m_token{std::make_shared<TokenQueue>()};
};

template <typename T>
class shared_future : public BrightFuture<T, shared_future<T>>
{
public:
	using Base = BrightFuture<T, shared_future<T>>;

	shared_future() = default;
	shared_future(shared_future&&) noexcept = default;
	shared_future& operator=(shared_future&&) noexcept = default;

	shared_future(const shared_future& rhs) : Base{rhs}, m_shared_state{rhs.m_shared_state}
	{
	}

	shared_future(const std::shared_future<T>& sf, TokenQueuePtr token) : Base{std::move(token)}, m_shared_state{sf}
	{
	}

	template <typename Func>
	auto then(Func&& continuation, Executor *host)
	{
		return Async(std::forward<Func>(continuation), shared_future<T>{*this}, host);
	}

	template <typename R=T>
	typename std::enable_if<std::is_void<R>::value>::type get()
	{
		assert(Base::valid());
		m_shared_state.get();
	}

	template <typename R=T>
	typename std::enable_if<!std::is_void<R>::value, const T>::type& get()
	{
		assert(Base::valid());
		return m_shared_state.get();
	}

	std::shared_future<T> InternalFuture()
	{
		return m_shared_state;
	}
	const std::shared_future<T>& InternalFuture() const
	{
		return m_shared_state;
	}

private:
	std::shared_future<T>   m_shared_state;
};

template <typename T>
class future : public BrightFuture<T, future<T>>
{
public:
	using Base = BrightFuture<T, future<T>>;

	future() = default;
	future(future&&) noexcept = default;
	future& operator=(future&&) noexcept = default;
	future(const future& future) = delete;
	future& operator=(const future&) = delete;

	future(std::future<T>&& f, TokenQueuePtr token) : Base{std::move(token)}, m_shared_state{std::move(f)}
	{
	}

	auto share()
	{
		return shared_future<T>{m_shared_state.share(), std::move(Base::m_token)};
	}

	template <typename Func>
	auto then(Func&& continuation, Executor *host)
	{
		return Async(std::forward<Func>(continuation), std::move(*this), host);
	}

	template <typename R=T>
	typename std::enable_if<std::is_void<R>::value>::type get()
	{
		assert(Base::valid());
		m_shared_state.get();
	}

	template <typename R=T>
	typename std::enable_if<!std::is_void<R>::value, T>::type get()
	{
		assert(Base::valid());
		return m_shared_state.get();
	}

	std::future<T>&& InternalFuture()
	{
		return std::move(m_shared_state);
	}
	const std::future<T>& InternalFuture() const
	{
		return m_shared_state;
	}

private:
	std::future<T> m_shared_state;
};

template <typename T>
class promise
{
public:
	promise() = default;

	auto get_future()
	{
		return future<T>{m_shared_state.get_future(), m_cont};
	}

	template <typename T1=T>
	typename std::enable_if<!std::is_void<T1>::value>::type set_value(T1&& t)
	{
		m_shared_state.set_value(std::move(t));
		m_cont->TryContinue();
	}

	template <typename T1=T>
	typename std::enable_if<std::is_void<T1>::value>::type set_value()
	{
		m_shared_state.set_value();
		m_cont->TryContinue();
	}
private:
	std::promise<T>     m_shared_state;
	TokenQueuePtr       m_cont{std::make_shared<TokenQueue>()};
};

template <typename ConcreteExecutor>
class ExecutorBase : public Executor
{
public:
	ExecutorBase() = default;

	Token Add(std::function<void()>&& task) override
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		auto event = m_seq++;
		m_tasks.emplace(event, std::move(task));
		return {this, event};
	}

	void Schedule(Token token) override
	{
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lock{m_mutex};
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

	void Execute(std::function<void()>&& task) override
	{
		// Use CRTP to call ConcreteExecutor::Execute().
		static_cast<ConcreteExecutor*>(this)->ExecuteTask(std::move(task));
	}

private:
	std::mutex  m_mutex;
	std::unordered_map<std::intptr_t, std::function<void()>>    m_tasks;
	std::intptr_t m_seq{0};
};

template <typename Future, typename Callable>
class Task
{
public:
	//! Return value of "Callable". It may be void.
	using Ret = typename std::result_of<Callable(Future&&)>::type;

public:
	Task(Future&& arg, Callable&& func) : m_function{std::move(func)}, m_arg{std::move(arg)}
	{
	}

	auto GetResult()
	{
		return m_return.get_future();
	}

	template <typename R=Ret>
	typename std::enable_if<!std::is_void<R>::value>::type Execute()
	{
		m_return.set_value(m_function(std::move(m_arg)));
	}

	template <typename R=Ret>
	typename std::enable_if<std::is_void<R>::value>::type Execute()
	{
		m_function(std::move(m_arg));
		m_return.set_value();
	}

private:
	promise<Ret>    m_return;       //!< Promise to the return value of the function to be called.
	Callable        m_function;     //!< Function to be called in Execute().
	Future          m_arg;          //!< Argument to the function to be called in Execute().
};

template <typename Future, typename Function>
auto MakeTask(Future&& arg, Function&& func)
{
	return std::make_shared<Task<Future, Function>>(std::move(arg), std::forward<Function>(func));
}

class DefaultExecutor : public ExecutorBase<DefaultExecutor>
{
public:
	// Called by ExecutorBase using CRTP
	void ExecuteTask(std::function<void()>&& task)
	{
		std::thread{std::move(task)}.detach();
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
		m_cond.notify_all();
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
	void ExecuteTask(std::function<void()>&& task)
	{
		std::unique_lock<std::mutex> lock{m_mux};
		m_queue.push_back(std::move(task));
		m_cond.notify_one();
	}
	
private:
	std::mutex                          m_mux;
	std::condition_variable             m_cond;
	std::deque<std::function<void()>>   m_queue;
	std::atomic<bool>                   m_quit{false};
	std::atomic<decltype(m_queue.size())> m_count{0};
};

template <typename Func, typename Future>
auto Async(Func&& continuation, Future&& future, Executor *host)
{
	assert(future.valid());
	assert(host);

	auto token_queue = future.m_token;
	assert(token_queue);
	
	auto task      = MakeTask(std::move(future), std::forward<Func>(continuation));
	auto result    = task->GetResult();
	auto token     = host->Add([task=std::move(task)]{task->Execute();});

	// There is a race condition here: whether or not the last async call has finished or not.

	// If it has finished, it should have already Scheduled() all continuation tokens in the
	// m_token queue. In this case, PushBack() will return false, indicating no one will ever
	// look at the queue again. We need to schedule our cont_token here.

	// Otherwise, PushBack() will returns true, that means the last async call has not finished
	// and the cont_token is added to the queue. In this case the token will be Scheduled()
	// by the async call when it finishes. We don't need to call Schedule() ourselves.
	if (!token_queue->PushBack(token))
		host->Schedule(token);

	return result;
}

template <typename NullaryFunc> class AdaptToUnary
{
private:
	NullaryFunc m_func;

public:
	template <typename F>
	AdaptToUnary(F&& f) : m_func{std::forward<F>(f)}{}

	template <typename R=decltype(m_func())>
	typename std::enable_if<std::is_void<R>::value>::type operator()(future<void>)
	{
		m_func();
	}
	template <typename R=decltype(m_func())>
	typename std::enable_if<!std::is_void<R>::value, R>::type operator()(future<void>)
	{
		return m_func();
	}
};

template <typename Func>
auto async(Func&& func, Executor *exe = DefaultExecutor::Instance())
{
	// The argument to func is already ready, because there is none.
	promise<void> arg;
	arg.set_value();

	return Async(AdaptToUnary<Func>{std::forward<Func>(func)}, arg.get_future(), exe);
}

template <typename T>
class IntermediateResultOfWhenAll
{
public:
	explicit IntermediateResultOfWhenAll(std::size_t total) : m_total{total}
	{
	}

	template <typename Future> // Should be future<T> or shared_future<T>
	void Process(Future&& fut, std::size_t index)
	{
		// What if this line throw? Where should we store the exception
		// pointer? We only have one promise.
		auto&& val = fut.get();
		
		// The executor may run this function in different threads.
		// make sure they don't step in each others.
		std::unique_lock<std::mutex> lock{m_mux};
		
		// Add the transient results to the map.
		assert(m_values.size() < m_total);
		m_values.emplace(index, std::move(val));

		// If all results are ready, transfer them to the promise.
		if (m_values.size() == m_total)
		{
			std::vector<T> result;
			for (auto&& p : m_values)
				result.push_back(std::move(p.second));

			// m_promise is thread-safe, no need to protect it.
			lock.unlock();
			m_promise.set_value(std::move(result));
		}
	}
	
	auto Result()
	{
		return m_promise.get_future();
	}
	
private:
	promise<std::vector<T>>     m_promise;
	
	std::mutex                  m_mux;
	std::map<std::size_t, T>    m_values;
	const std::size_t           m_total;
};

template < class InputIt >
auto when_all(InputIt first, InputIt last, Executor *exe = DefaultExecutor::Instance())
{
	using Future = typename std::iterator_traits<InputIt>::value_type;
	using T      = typename Future::value_type;
	
	// move all futures to a vector first, because we need to know how many
	// and InputIt only allows us to iterate them once.
	std::vector<Future> futures;
	std::move(first, last, std::back_inserter(futures));

	auto intermediate  = std::make_shared<IntermediateResultOfWhenAll<T>>(futures.size());
	for (auto i = futures.size()*0 ; i < futures.size(); i++)
		futures[i].then([intermediate, i](auto&& fut)
		{
			intermediate->Process(std::move(fut), i);
		}, exe);
	
	return intermediate->Result();
}

} // end of namespace
