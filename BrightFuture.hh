/*
	Copyright © 2017 Wan Wai Ho <me@nestal.net>
	Distributed under the Boost Software License, Version 1.0.
	(See accompanying file LICENSE_1_0.txt or copy at
	http://www.boost.org/LICENSE_1_0.txt)
*/

//
// Created by nestal on 10/3/17.
//

/// \file   BrightFuture.hh
/// \brief  Main header file for BrightFuture
///
/// BrightFuture is a header-only library. You only need to include BrightFuture.hh in your
/// project to use it. There is only one header to include so it is easy to be used in any
/// projects.

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
class Executor;

class TaskBase
{
public:
	virtual void Execute() = 0;
};

///
/// Schedule callbacks and execute them.
///
/// The Executor serves two purposes: to schedule callback functions and to execute them.
/// This is the abstract interface of an Executor. It's to be used with a Token.
///
class Executor
{
public:
	virtual ~Executor() = default;
	virtual void Execute(std::shared_ptr<TaskBase>&& task) = 0;
	virtual Token Add(std::shared_ptr<TaskBase>&& task) = 0;
	virtual void Schedule(Token token) = 0;
	virtual std::shared_ptr<Executor> ShareFromThis() {return {};}
	virtual std::shared_ptr<const Executor> ShareFromThis() const {return {};}
};

///
/// A token to represent a task to be called.
///
/// It is returned by TaskScheduler::Add() to represent the task being added. Pass the token
/// to TaskScheduler::Schedule() to run that task.
///
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
		SharedState().wait();
	}

	bool valid() const
	{
		return SharedState().valid() && m_token;
	}

	bool is_ready() const
	{
		return SharedState().wait_for(std::chrono::seconds::zero()) == std::future_status::ready;
	}

	template <typename Rep, typename Ratio>
	std::future_status wait_for(const std::chrono::duration<Rep, Ratio>& duration)
	{
		assert(valid());
		return SharedState().wait_for(duration);
	}

	template <typename Clock, typename Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& time_point)
	{
		assert(valid());
		return SharedState().wait_until(time_point);
	}

	template <typename Func, typename Future>
	friend auto Async(Func&& function, Future&& fut_arg, Executor *host);

private:
	auto& SharedState() {return static_cast<Inherited*>(this)->m_shared_state;}
	const auto& SharedState() const {return static_cast<const Inherited*>(this)->m_shared_state;}

	TokenQueuePtr       m_token{std::make_shared<TokenQueue>()};
};

template <typename T>
class shared_future : public BrightFuture<T, shared_future<T>>
{
private:
	using Base = BrightFuture<T, shared_future<T>>;
	friend Base;

public:
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
	auto then(Func&& continuation, Executor *host = nullptr)
	{
#if __cplusplus >= 201703L
		static_assert(std::is_invocable<Func, shared_future&&>::value);
#endif
		return Async(std::forward<Func>(continuation), shared_future{*this}, host);
	}

	template <typename R=T>
	typename std::enable_if<std::is_void<R>::value>::type get()
	{
		assert(Base::valid());
		m_shared_state.get();
	}

	template <typename R=T>
	typename std::enable_if<!std::is_void<R>::value, const T>::type& get() const
	{
		assert(Base::valid());
		return m_shared_state.get();
	}

private:
	friend Base;
	std::shared_future<T>   m_shared_state;
};

/// Refers to a value that will be available in the future.
///
/// A future refers to a value that will be available in the future. It is typically created by async()
/// which run something in another thread. Since the value in question would be created by running
/// some code in a separate thread, it is called the _shared state_ of the future.
///
/// A future is said to be _valid_ if it refers to a shared state. For example, default constructed
/// futures are invalid, because it doesn't refer to a shared state. Future returned by async() will
/// always be valid. After the shared state is ready, accessing the shared state will also invalidate
/// the future. In other words, futures are supposed to be used once. If you want to use it multiple
/// times, convert it into a shared_future.
///
/// The future class provides functions to access to the stared state. In C++11, you can call get()
/// to retrieve the shared state. As mentioned above, the future will be invalid after you call get().
/// BrightFuture::future supports then(), in addition to all other features supported by C++11 futures.
///
/// \tparam T   The type of the future value.
template <typename T>
class future : public BrightFuture<T, future<T>>
{
private:
	using Base = BrightFuture<T, future<T>>;
	friend Base;

public:
	future() = default;
	future(future&&) noexcept = default;
	future& operator=(future&&) noexcept = default;
	future(const future& future) = delete;
	future& operator=(const future&) = delete;

	future(future<future<T>>&& wrapped);
	
	future(std::future<T>&& f, TokenQueuePtr token) : Base{std::move(token)}, m_shared_state{std::move(f)}
	{
	}

	/// Convert a future to a shared_future.
	///
	///
	auto share()
	{
		return shared_future<T>{m_shared_state.share(), std::move(Base::m_token)};
	}

	template <typename Func>
	auto then(Func&& continuation, Executor *host = nullptr)
	{
#if __cplusplus >= 201703L
		static_assert(std::is_invocable<Func, future&&>::value);
#endif
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

	void set_exception( std::exception_ptr p )
	{
		m_shared_state.set_exception(p);
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

	Token Add(std::shared_ptr<TaskBase>&& task) override
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		auto event = m_seq++;
		m_tasks.emplace(event, std::move(task));
		return {this, event};
	}

	void Schedule(Token token) override
	{
		std::shared_ptr<TaskBase> task;
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

	void Execute(std::shared_ptr<TaskBase>&& task) override
	{
		// Use CRTP to call ConcreteExecutor::Execute().
		static_cast<ConcreteExecutor*>(this)->Post(std::move(task));
	}

private:
	std::mutex  m_mutex;
	std::unordered_map<std::intptr_t, std::shared_ptr<TaskBase>>    m_tasks;
	std::intptr_t m_seq{0};
};

///
/// A temporary executor that runs tasks directly in the same thread that schedule them.
///
/// It is a tricky executor: it is designed to be used temporarily when no other executor
/// are available, e.g. when the executor passed to then() is null. Using InlineExecutor
/// basically means that:
/// - If the previous async task is not finished, attach the current function to the same
///   thread as the previous async task.
/// - If the previous async task has been finished, then run the current function immediately.
///
class InlineExecutor : public ExecutorBase<InlineExecutor>, public std::enable_shared_from_this<InlineExecutor>
{
private:
	InlineExecutor() = default;
	
public:
	static auto New() { return std::shared_ptr<InlineExecutor>(new InlineExecutor);}
	
	void Post(std::shared_ptr<TaskBase>&& task)
	{
		task->Execute();
	}
	
	std::shared_ptr<Executor> ShareFromThis() override
	{
		return shared_from_this();
	}
	
	std::shared_ptr<const Executor> ShareFromThis() const override
	{
		return shared_from_this();
	}
	
	static auto Alternative(Executor *& exec)
	{
		auto replacement{exec ? exec->ShareFromThis() : std::shared_ptr<Executor>{}};
		if (!exec && !replacement)
		{
			replacement = New();
			exec = replacement.get();
		}
		return replacement;
	}
};

/// \brief Unwrapping constructor for future
///
/// Construct a future<T> from a future<future<T>>, i.e. unwraps the future. This
/// constructor is useful when returning a future in the continuation routine in
/// then().
///
/// \tparam T   The type of the shared state.
/// \param fut  The "wrapped" future to a future to T. After calling this function
///             \a fut will be invalid, i.e. fut.valid() will return false.
template <typename T>
future<T>::future(future<future<T>>&& fut)
{
	promise<T> fwd;
	*this = fwd.get_future();
	
	auto exe = InlineExecutor::New();
	
	// It's OK to capture a shared_ptr to InlineExecutor in the callback that is queued to
	// itself, because the InlineExecutor will not destroy the std::function passed to it
	// in ExecuteTask(). When the last std::function in the InlineExecutor is destroyed,
	// the InlineExecutor itself will be freed.
	fut.then([fwd=std::move(fwd), exe](future<future<T>> fut) mutable
	{
		try
		{
			// Although we don't need the executor in this lambda, we need to keep it
			// captured, otherwise the shared_ptr will destroy the executor.
			fut.get().then([fwd=std::move(fwd), exe](future<T> fut_get) mutable
			{
				try
				{
					fwd.set_value(fut_get.get());
				}
				catch (...)
				{
					fwd.set_exception(std::current_exception());
				}
			}, exe.get());
		}
		catch (...)
		{
			fwd.set_exception(std::current_exception());
		}
	}, exe.get());
}

template <typename Future, typename Callable>
class Task : public TaskBase
{
public:
	//! Return value of "Callable". It may be void.
	using Ret = typename std::result_of<Callable(Future&&)>::type;

public:
	Task(Future&& arg, Callable&& func, std::shared_ptr<Executor>&& exe = {}) :
		m_exec{std::move(exe)}, m_function{std::forward<Callable>(func)}, m_arg{std::forward<Future>(arg)}
	{
	}

	auto GetResult()
	{
		return m_return.get_future();
	}

	void Execute() override
	{
		Run();
	}
	
	template <typename R=Ret>
	typename std::enable_if<!std::is_void<R>::value, void>::type Run()
	{
		try
		{
			m_return.set_value(m_function(std::move(m_arg)));
		}
		catch (...)
		{
			m_return.set_exception(std::current_exception());
		}
	}

	template <typename R=Ret>
	typename std::enable_if<std::is_void<R>::value, void>::type Run()
	{
		try
		{
			m_function(std::move(m_arg));
			m_return.set_value();
		}
		catch (...)
		{
			m_return.set_exception(std::current_exception());
		}
	}

private:
	std::shared_ptr<Executor>   m_exec; //!< Only valid if a shared-ownership executor. This is just to make
										//!< sure the executor's lifetime exeed the task.
	
	promise<Ret>    m_return;       //!< Promise to the return value of the function to be called.
	Callable        m_function;     //!< Function to be called in Execute().
	Future          m_arg;          //!< Argument to the function to be called in Execute().
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
		std::deque<std::shared_ptr<TaskBase>> queue;
		{
			std::unique_lock<std::mutex> lock{m_mux};
			m_cond.wait(lock, [this] { return !m_queue.empty() || m_quit; });
			queue.swap(m_queue);
		}
		
		// Execute the function after releasing the lock to reduce contention
		for (auto&& task : queue)
			task->Execute();
		
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
	void Post(std::shared_ptr<TaskBase>&& task)
	{
		std::unique_lock<std::mutex> lock{m_mux};
		m_queue.push_back(std::move(task));
		m_cond.notify_one();
	}
	
private:
	std::mutex                              m_mux;
	std::condition_variable                 m_cond;
	std::deque<std::shared_ptr<TaskBase>>   m_queue;
	std::atomic<bool>                       m_quit{false};
	std::atomic<std::size_t>                m_count{0};
};

template <typename Func, typename Future>
auto Async(Func&& function, Future&& fut_arg, Executor *host)
{
	assert(fut_arg.valid());
	
	auto token_queue = fut_arg.m_token;
	assert(token_queue);
	
	// If host is null, create an InlineExecutor as an alternative.
	// We need to capture that InlineExecutor in the task function that will be Add()'ed,
	// because we need to keep the executor alive until the task function has returned.
	auto task      = std::make_shared<Task<Future, Func>>(
		std::forward<Future>(fut_arg),
		std::forward<Func>(function),
		InlineExecutor::Alternative(host)
	);
	assert(host);
	
	auto result    = task->GetResult();
	auto token     = host->Add(std::move(task));

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
	// Add 1st argument to avoid overriding copy/move ctor.
	template <typename F>
	explicit AdaptToUnary(std::nullptr_t, F&& f) : m_func{std::forward<F>(f)}{}

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

/// Invoke a function in an executor and get a future to the result.
///
///
/// \tparam Func    The type of the function object. Must be a _Callable_ object (i.e.
///                 supports operator(void)).
/// \param func     The function object. func() will be invoked in the Executor \a exe.
/// \param exe      The executor to invoke the function.
/// \return         A future to the return value of func().
template <typename Func>
auto async(Func&& func, Executor *exe)
{
	// The argument to func is already ready, because there is none.
	promise<void> arg;
	arg.set_value();

	return Async(AdaptToUnary<Func>{nullptr, std::forward<Func>(func)}, arg.get_future(), exe);
}

template <typename T>
class IntermediateResultOfWhenAll
{
public:
	explicit IntermediateResultOfWhenAll(std::size_t total) : m_total{total} {}

	template <typename Future> // Should be future<T> or shared_future<T>
	void Process(Future&& fut, std::size_t index)
	{
		// What if this line throw? Where should we store the exception
		// pointer? We only have one promise.
		assert(fut.valid());
		auto&& val = fut.get();
		
		// The executor may run this function in different threads.
		// make sure they don't step in each others.
		std::unique_lock<std::mutex> lock{m_mux};
		assert(index < m_total);
		
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
	
	auto Executor()
	{
		return m_exec.get();
	}
	
private:
	std::shared_ptr<InlineExecutor>             m_exec{InlineExecutor::New()};
	promise<std::vector<T>>     m_promise;
	
	std::mutex                  m_mux;
	std::map<std::size_t, T>    m_values;
	const std::size_t           m_total;
};

template < class InputIt >
auto when_all(InputIt first, InputIt last)
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
			intermediate->Process(std::forward<decltype(fut)>(fut), i);
		}, intermediate->Executor());
	
	return intermediate->Result();
}

} // end of namespace
