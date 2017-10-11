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
			return false;
		
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
	
	auto Result()
	{
		return m_return.get_future();
	}

protected:
	Continuation(Callable&& func, TokenQueuePtr cont) :
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
		m_cont->TryContinue();
	}

	std::promise<Ret>   m_return;       //!< Promise to the return value of the function to be called.
	Callable            m_function;     //!< Function to be called in Execute().
	TokenQueuePtr       m_cont;     //!< Token of the continuation routine that consumes the
											//!< return value, after it is ready.
};

template <typename Arg, typename Function, template <typename> class InternalFuture>
class ConcreteTask : public Continuation<Arg, Function>
{
private:
	using Base = Continuation<Arg, Function>;
	
public:
	ConcreteTask(InternalFuture<Arg>&& arg, Function&& func, const TokenQueuePtr& cont) :
		Base{std::move(func), cont}, m_arg{std::move(arg)}
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
	ConcreteTask(InternalFuture<void>&& arg, Function&& func, const TokenQueuePtr& cont) :
		Base{std::move(func), cont}, m_arg{std::move(arg)}
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
auto MakeTask(InternalFuture<T>&& arg, Function&& func, const TokenQueuePtr& cont)
{
	return std::make_shared<ConcreteTask<T, Function, InternalFuture>>(std::move(arg), std::forward<Function>(func), cont);
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

template <typename T, template <typename> class InternalFuture, typename Inherited>
class BrightFuture
{
public:
	using value_type = T;
	
public:
	BrightFuture() = default;
	BrightFuture(BrightFuture&&) noexcept = default;
	BrightFuture& operator=(BrightFuture&&) noexcept = default;
	
	explicit BrightFuture(InternalFuture<T>&& shared_state, TokenQueuePtr token) noexcept :
		m_shared_state{std::move(shared_state)},
		m_token{std::move(token)}
	{
	}
	~BrightFuture() = default;
	
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
		auto next_token = std::make_shared<TokenQueue>();

		// Prepare the continuation routine as a task. The function of the task is of course
		// the continuation routine itself. The argument of the continuation routine is the
		// result of the last async call, i.e. the variable referred by the m_shared_state future.
		bool is_ready = (m_shared_state.wait_for(std::chrono::seconds::zero()) == std::future_status::ready);
		auto continuation_task = MakeTask(std::move(m_shared_state), std::forward<Func>(continuation), next_token);
		
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
			m_token->PushBack(continuation_token);
		
		return MakeFuture(std::move(continuation_return_value), std::move(next_token));
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
	static auto MakeFuture(std::future<Type>&& shared_state, TokenQueuePtr&& token)
	{
		return future<Type>{std::move(shared_state), std::move(token)};
	}

protected:
	InternalFuture<T>   m_shared_state;
	TokenQueuePtr       m_token;
};

template <typename T>
class shared_future : public BrightFuture<T, std::shared_future, shared_future<T>>
{
public:
	using Base = BrightFuture<T, std::shared_future, shared_future<T>>;
	using Base::BrightFuture;
	
	shared_future(shared_future&&) noexcept = default;
	shared_future& operator=(shared_future&&) noexcept = default;
	
	shared_future(const shared_future& rhs)
	{
		Base::m_shared_state = rhs.Base::m_shared_state;
		Base::m_token        = rhs.Base::m_token;
	}
	
	shared_future<T> share()
	{
		return *this;
	}

	template <typename R=T>
	typename std::enable_if<std::is_void<R>::value>::type get()
	{
		assert(Base::valid());
		Base::m_shared_state.get();
	}
	
	template <typename R=T>
	typename std::enable_if<!std::is_void<R>::value, const T>::type& get()
	{
		assert(Base::valid());
		return Base::m_shared_state.get();
	}
};

template <typename T>
class future : public BrightFuture<T, std::future, future<T>>
{
public:
	using Base = BrightFuture<T, std::future, future<T>>;
	using Base::BrightFuture;
	
	future(future&&) noexcept = default;
	future& operator=(future&&) noexcept = default;
	future(const future& future) = delete;
	future& operator=(const future&) = delete;
	
	shared_future<T> share()
	{
		return shared_future<T>{Base::m_shared_state.share(), std::move(Base::m_token)};
	}

	template <typename R=T>
	typename std::enable_if<std::is_void<R>::value>::type get()
	{
		assert(Base::valid());
		Base::m_shared_state.get();
	}
	
	template <typename R=T>
	typename std::enable_if<!std::is_void<R>::value, T>::type get()
	{
		assert(Base::valid());
		return Base::m_shared_state.get();
	}
};

template <typename Func>
auto async(Func&& func, Executor *exe = DefaultExecutor::Instance())
{
	using T = decltype(func());
	
	auto cont = std::make_shared<TokenQueue>();
	std::promise<void> arg;
	arg.set_value();
	
	auto task   = std::make_shared<ConcreteTask<void, Func, std::future>>(arg.get_future(), std::forward<Func>(func), cont);
	auto result = task->Result();
	
	exe->Execute(std::move(task));
	
	return future<T>::MakeFuture(std::move(result), std::move(cont));
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
	std::map<std::size_t, T>        m_values;
	std::size_t m_total{};
	std::mutex  m_mux;
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
