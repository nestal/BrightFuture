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
			if (t.host)
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
	
	std::future<Ret> Result()
	{
		return m_return.get_future();
	}

	TokenQueuePtr Token()
	{
		return m_cont;
	}

protected:
	Continuation(Callable&& func) : m_function{std::move(func)}
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
		m_cont->TryContinue();
	}

	std::promise<Ret>   m_return;       //!< Promise to the return value of the function to be called.
	Callable            m_function;     //!< Function to be called in Execute().

	//! Token of the continuation routine that consumes the return value, after it is ready.
	TokenQueuePtr       m_cont{std::make_shared<TokenQueue>()};
};

template <typename Arg, typename Function, template <typename> class InternalFuture>
class ConcreteTask : public Continuation<Arg, Function>
{
private:
	using Base = Continuation<Arg, Function>;
	
public:
	ConcreteTask(InternalFuture<Arg>&& arg, Function&& func) : Base{std::move(func)}, m_arg{std::move(arg)}
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
	InternalFuture<Arg> m_arg;      //!< Argument to the function to be called in Execute().
};

template <typename Function, template <typename> class InternalFuture>
class ConcreteTask<void, Function, InternalFuture> : public Continuation<void, Function>
{
private:
	using Base = Continuation<void, Function>;

public:
	ConcreteTask(InternalFuture<void>&& arg, Function&& func) :
		Base{std::move(func)}, m_arg{std::move(arg)}
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
		m_arg.get();
		Base::m_return.set_value(Base::m_function());
	}

	template <typename R=typename Base::Ret>
	typename std::enable_if<std::is_void<R>::value>::type Run()
	{
		m_arg.get();
		Base::m_function();
		Base::m_return.set_value();
	}
	
private:
	InternalFuture<void> m_arg;
};

template <typename T, typename Function, template <typename> class InternalFuture>
auto MakeTask(InternalFuture<T>&& arg, Function&& func)
{
	return std::make_shared<ConcreteTask<T, Function, InternalFuture>>(std::move(arg), std::forward<Function>(func));
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
class future;

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
	template <typename Func>
	auto then(Func&& continuation, Executor *host = DefaultExecutor::Instance())
	{
		assert(valid());
		
		// Prepare the continuation routine as a task. The function of the task is of course
		// the continuation routine itself. The argument of the continuation routine is the
		// result of the last async call, i.e. the variable referred by the m_shared_state future.

		// The "InternalFuture()" returned by the inherited class may be a const std::shared_future&
		// or a std::future&&. MakeTask() will perfectly forward both.
		auto continuation_task = MakeTask(
			static_cast<Inherited&>(*this).InternalFuture(),
			std::forward<Func>(continuation)
		);

		// The future to the return of the continuation routine to support chaining. It's the return
		// value of this function.
		auto continuation_future = MakeFuture(continuation_task);

		// Add the continuation routine to the executor, which will run the task and set its return
		// value to the promise above. A token to the task is returned. We use this token to schedule
		// the continuation routine.
		auto continuation_token = host->Add(std::move(continuation_task));
		
		assert(continuation_token.host);
		assert(continuation_token.host == host);

		// There is a race condition here: whether or not the last async call has finished or not.
		
		// If it has finished, it should have already Scheduled() all continuation tokens in the
		// m_token queue. In this case, PushBack() will return false, indicating no one will ever
		// look at the queue again. We need to schedule our continuation_token here.
		
		// Otherwise, PushBack() will returns true, that means the last async call has not finished
		// and the continuation_token is added to the queue. In this case the token will be Scheduled()
		// by the async call when it finishes. We don't need to call Schedule() ourselves.
		if (!m_token->PushBack(continuation_token))
			host->Schedule(continuation_token);
		
		return continuation_future;
	}

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
		assert(valid());
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
	
	shared_future(const shared_future& rhs) :Base{rhs}, m_shared_state{rhs.m_shared_state}
	{
	}
	
	shared_future(const std::shared_future<T>& sf, TokenQueuePtr&& token) : Base{std::move(token)}, m_shared_state{sf}
	{
	}
	
	shared_future<T> share()
	{
		return *this;
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

	future(std::future<T>&& f, TokenQueuePtr&& token) : Base{std::move(token)}, m_shared_state{std::move(f)}
	{
	}
	
	shared_future<T> share()
	{
		return shared_future<T>{m_shared_state.share(), std::move(Base::m_token)};
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

template <typename Task>
auto MakeFuture(const std::shared_ptr<Task>& task)
{
	return future<typename Task::Ret>{task->Result(), task->Token()};
}

template <typename Func>
auto async(Func&& func, Executor *exe = DefaultExecutor::Instance())
{
	using T = decltype(func());
	
	// The argument to func is already ready, because there is none.
	std::promise<void> arg;
	arg.set_value();

	auto task   = MakeTask(arg.get_future(), std::forward<Func>(func));
	auto result = MakeFuture(task);

	exe->Execute(std::move(task));
	
	return result;
}

template <typename T>
class IntermediateResultOfWhenAll
{
public:
	explicit IntermediateResultOfWhenAll(TokenQueuePtr token, std::size_t total) :
		m_token{std::move(token)}, m_total{total}
	{
	}

	template <typename U>
	void Process(U&& val, std::size_t index)
	{
		// The executor may run this function in different threads.
		// make sure they don't step in each others.
		std::unique_lock<std::mutex> lock{m_mux};
		
		m_values.emplace(index, std::forward<U>(val));
		if (m_values.size() == m_total)
		{
			std::vector<T> result;
			for (auto&& p : m_values)
				result.push_back(std::move(p.second));
			lock.unlock();

			// m_promise and m_token are both thread-safe, no need to protect them
			m_promise.set_value(std::move(result));
			m_token->TryContinue();
		}
	}
	
	auto Result()
	{
		return m_promise.get_future();
	}
	
private:
	TokenQueuePtr                   m_token;
	std::promise<std::vector<T>>    m_promise;
	
	std::mutex  m_mux;
	std::map<std::size_t, T>        m_values;
	std::size_t m_total{};
};

template < class InputIt >
auto when_all(InputIt first, InputIt last, Executor *exe = DefaultExecutor::Instance())
{
	using T = typename std::iterator_traits<InputIt>::value_type::value_type;
	
	// move all futures to a vector first, because we need to know how many
	// and InputIt only allows us to iterate them once.
	std::vector<shared_future<T>> futures;
	for (auto it = first ; it != last; it++)
		futures.push_back(it->share());
	
	auto token_promise = std::make_shared<TokenQueue>();
	auto intermediate  = std::make_shared<IntermediateResultOfWhenAll<T>>(token_promise, futures.size());
	future<std::vector<T>> future_vec{intermediate->Result(), std::move(token_promise)};
	
	for (auto i = futures.size()*0 ; i < futures.size(); i++)
		futures[i].then([intermediate, i](auto&& val)
		{
			intermediate->Process(std::forward<decltype(val)>(val), i);
		}, exe);
	
	return future_vec;
}

} // end of namespace
