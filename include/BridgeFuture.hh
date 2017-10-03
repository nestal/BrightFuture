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
	virtual void Execute() = 0;
};

class TaskScheduler
{
public:
	explicit TaskScheduler(std::unique_ptr<Executor>&& exe) : m_executor{std::move(exe)}
	{
	}

	Token Add(std::shared_ptr<TaskBase>&& task);
	void Schedule(Token token);
	
	void Execute(std::shared_ptr<TaskBase>&& task);

	std::size_t Count() const
	{
		return m_tasks.size();
	}

private:
	std::mutex m_task_mutex;
	
	std::unique_ptr<Executor> m_executor;
	
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
class Task : public TaskBase
{
public:
	// Return value of "Function". It may be void.
	using Ret = decltype(std::declval<Function>()(std::declval<Arg>()));

	Task(const std::shared_future<Arg>& arg, Function&& func, std::future<Token>&& cont) :
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
	std::shared_future<Arg> m_arg;
	std::promise<Ret>       m_return;
	Function                m_function;
	std::future<Token>      m_cont;
};

template <typename Function>
class Task<void, Function> : public TaskBase
{
public:
	// Return value of "Function". It may be void.
	using Ret = decltype(std::declval<Function>()());

	Task(Function&& func, std::future<Token>&& cont) :
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
	return std::make_shared<Task<T, Function>>(std::move(arg), std::forward<Function>(func), std::move(cont));
}

class Executor
{
public:
	virtual void Execute(const std::function<void()>& func) = 0;
};

inline Token TaskScheduler::Add(std::shared_ptr<TaskBase>&& task)
{
	auto event = m_seq++;

	std::unique_lock<std::mutex> lock{m_task_mutex};
	m_tasks.emplace(event, std::move(task));
	return {this, event};
}

inline void TaskScheduler::Schedule(Token token)
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
	if (task)
		Execute(std::move(task));
}

void TaskScheduler::Execute(std::shared_ptr<TaskBase>&& task)
{
	m_executor->Execute([task=std::move(task)]
	{
		task->Execute();
	});
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
		std::promise<Token> cont_token;

		auto result = m_shared_state.share();
		auto task   = MakeTask(result, std::forward<Func>(continuation), cont_token.get_future());
		auto cont   = task->Result();
		auto token  = host->Add(std::move(task));
		
		assert(token.host);
		assert(token.host == host);

		if (result.wait_for(std::chrono::system_clock::duration::zero()) == std::future_status::ready)
			host->Schedule(token);
		else
			m_token.set_value(token);
		
		return MakeFuture(std::move(cont), std::move(cont_token));
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
	
	auto task   = std::make_shared<Task<void, Func>>(std::forward<Func>(func), cont.get_future());
	auto future = task->Result();
	
	exe->Execute(std::move(task));
	
	return Future<T>::MakeFuture(std::move(future), std::move(cont));
}

} // end of namespace
