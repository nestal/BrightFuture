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

#include <future>
#include <utility>
#include <type_traits>
#include <unordered_map>
#include <cassert>

namespace exe {

class Executor;
class TaskScheduler;

struct Token
{
	TaskScheduler   *host;
	std::intptr_t   event;
};

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
	virtual void Execute(const std::function<void()>& func) = 0;
};

class TaskScheduler
{
public:
	explicit TaskScheduler(std::unique_ptr<Executor>&& exe) : m_executor{std::move(exe)}
	{
	}

	Token Add(std::shared_ptr<TaskBase>&& task)
	{
		std::unique_lock<std::mutex> lock{m_task_mutex};
		auto event = m_seq++;
		m_tasks.emplace(event, std::move(task));
		return {this, event};
	}

	void Schedule(Token token)
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
		
		// m_task_mutex does not protect the m_executor, which is thread-safe.
		if (task)
			Execute(std::move(task));
	}

	void Execute(std::shared_ptr<TaskBase>&& task)
	{
		m_executor->Execute([task=std::move(task)]
		{
			task->Execute();
		});
	}

	std::size_t Count() const
	{
		return m_tasks.size();
	}

private:
	//! Executor is thread-safe. There is no need to protect it with a mutex.
	std::unique_ptr<Executor> m_executor;
	
	std::mutex m_task_mutex;
	std::unordered_map<std::intptr_t, std::shared_ptr<TaskBase>>    m_tasks;
	std::intptr_t m_seq{0};
};

inline void NotifyTaskFinished(std::future<Token>& token)
{
	assert(token.valid());
	if (token.wait_for(std::chrono::system_clock::duration::zero()) == std::future_status::ready)
	{
		auto t = token.get();
		if (t.host)
			t.host->Schedule(t);
	}
}

template <typename Arg, typename Function>
class ConcreteTask : public TaskBase
{
public:
	// Return value of "Function". It may be void.
	using Ret = decltype(std::declval<Function>()(std::declval<Arg>()));

	ConcreteTask(const std::shared_future<Arg>& arg, Function&& func, std::future<Token>&& cont) :
		m_arg{arg}, m_function{std::move(func)}, m_cont{std::move(cont)}
	{
	}

	std::future<Ret> Result()
	{
		return m_return.get_future();
	}

	void Execute() override
	{
		Run();
		NotifyTaskFinished(m_cont);
	}
	
private:
	template <typename R=Ret>
	typename std::enable_if<!std::is_void<R>::value>::type Run()
	{
		m_return.set_value(m_function(m_arg.get()));
	}

	template <typename R=Ret>
	typename std::enable_if<std::is_void<R>::value>::type Run()
	{
		m_function(m_arg.get());
		m_return.set_value();
	}

private:
	std::shared_future<Arg> m_arg;      //!< Argument to the function to be called in Execute().
	std::promise<Ret>       m_return;   //!< Promise to the return value of the function to be called.
	Function                m_function; //!< Function to be called in Execute().
	std::future<Token>      m_cont;     //!< Token of the continuation routine that consumes the
										//!< return value, after it is ready.
};

template <typename Function>
class ConcreteTask<void, Function> : public TaskBase
{
public:
	// Return value of "Function". It may be void.
	using Ret = decltype(std::declval<Function>()());

	ConcreteTask(Function&& func, std::future<Token>&& cont) :
		m_function{std::move(func)}, m_cont{std::move(cont)}
	{
	}

	std::future<Ret> Result()
	{
		return m_return.get_future();
	}

	void Execute() override
	{
		Run();
		NotifyTaskFinished(m_cont);
	}
	
private:
	template <typename R=Ret>
	typename std::enable_if<!std::is_void<R>::value>::type Run()
	{
		m_return.set_value(m_function());
	}

	template <typename R=Ret>
	typename std::enable_if<std::is_void<R>::value>::type Run()
	{
		m_function();
		m_return.set_value();
	}

private:
	std::promise<Ret>       m_return;
	Function                m_function;
	std::future<Token>      m_cont;
};

template <typename T, typename Function>
auto MakeTask(const std::shared_future<T>& arg, Function&& func, std::future<Token>&& cont)
{
	return std::make_shared<ConcreteTask<T, Function>>(std::move(arg), std::forward<Function>(func), std::move(cont));
}

class ThreadExecutor : public Executor
{
public:
	void Execute(const std::function<void()>& func) override
	{
		std::thread{func}.detach();
	}
};

template <typename T>
class Future
{
public:
	Future() = default;
	
	// move only type
	Future(Future&&) noexcept = default;
	Future(const Future&) = delete;
	Future& operator=(Future&&) = default;
	Future& operator=(const Future&) = delete;
	
	explicit Future(std::future<T>&& shared_state, std::promise<Token>&& token = {}) noexcept :
		m_shared_state{std::move(shared_state)},
		m_token{std::move(token)}
	{
	}
	~Future()
	{
		if (m_shared_state.valid())
			m_token.set_value({});
	}

	std::future<Token> GetToken()
	{
		return m_token.get_future();
	}

	template <typename Func>
	auto Then(Func&& continuation, TaskScheduler *host)
	{
		// The promise of the next continuation routine's token. It is not _THIS_ continuation
		// routine's token. We are adding the _THIS_ continuation routine (i.e. the "continuation"
		// argument) right here, so we knows the its token.
		//
		// We don't know the next continuation routine's token yet. It will be known when Then()
		// is called with the Future returned by this function.
		std::promise<Token> next_token;

		// Prepare the continuation routine as a task. The function of the task is of course
		// the continuation routine itself. The argument of the continuation routine is the
		// result of the last async call, i.e. the variable referred by the m_shared_state future.
		auto arg    = m_shared_state.share();
		auto task   = MakeTask(arg, std::forward<Func>(continuation), next_token.get_future());
		
		// We also need to know the promise to return value of the continuation routine as well.
		// It will be passed to the future returned by this function.
		auto ret    = task->Result();
		auto token  = host->Add(std::move(task));
		
		assert(token.host);
		assert(token.host == host);

		// "arg" is the argument of the continuation function, i.e. the result of the previous async
		// call. If it is ready, that means the previous async call is finished. We can directly
		// invoke the continuation function here.
		if (arg.wait_for(std::chrono::system_clock::duration::zero()) == std::future_status::ready)
			host->Schedule(token);
		else
			m_token.set_value(token);
		
		return MakeFuture(std::move(ret), std::move(next_token));
	}

	template <typename Type>
	static auto MakeFuture(std::future<Type>&& shared_state, std::promise<Token>&& token)
	{
		return Future<Type>{std::move(shared_state), std::move(token)};
	}

private:
	std::future<T>      m_shared_state;
	std::promise<Token> m_token;
};

template <typename Func>
auto Async(Func&& func, TaskScheduler *exe)
{
	using T = decltype(func());
	
	std::promise<Token> cont;
	
	auto task   = std::make_shared<ConcreteTask<void, Func>>(std::forward<Func>(func), cont.get_future());
	auto result = task->Result();
	
	exe->Execute(std::move(task));
	
	return Future<T>::MakeFuture(std::move(result), std::move(cont));
}

} // end of namespace
