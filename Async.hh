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
	virtual void Execute(Executor *executor) = 0;
};

class TaskScheduler
{
public:
	TaskScheduler() = default;

	template <typename T, typename Function>
	auto Add(const std::shared_future<T>& val, Function&& func, Token& out, std::future<Token>&& cont);

	void Schedule(Token token, Executor *executor);

	std::size_t Count() const
	{
		return m_tasks.size();
	}

private:
	std::unordered_map<std::intptr_t, std::shared_ptr<TaskBase>>    m_tasks;
	std::intptr_t m_seq{0};
};

template <typename T, typename Function>
class Task : public TaskBase
{
public:
	using R = decltype(std::declval<Function>()(std::declval<T>()));

	Task(const std::shared_future<T>& val, Function&& func, std::future<Token>&& cont) :
		m_val{val}, m_callback{std::move(func)}, m_cont{std::move(cont)}
	{
	}

	std::future<R> Result()
	{
		return m_result.get_future();
	}

	void Execute(Executor *executor) override
	{
		Continue(m_val.get());

		std::cout << "pre m_cont.wait_for() " << m_cont.valid() << std::endl;
		// notify
		if (m_cont.valid() && m_cont.wait_for(std::chrono::system_clock::duration::zero()) == std::future_status::ready)
		{
			std::cout << "post m_cont.wait_for() " << std::endl;
			auto t = m_cont.get();
			std::cout << "post m_cont.get() " << std::endl;
			
			if (t.host)
			{
				std::cout << "schedule cont" << std::endl;
				t.host->Schedule(t, executor);
			}
		}
		else
			std::cout << "not one called then() yet" << std::endl;
	}

private:
	template <typename Arg, typename Q=R>
	typename std::enable_if<!std::is_void<Q>::value>::type Continue(Arg&& val)
	{
		std::cout << "setting value " << val << std::endl;
		m_result.set_value(m_callback(std::forward<Arg>(val)));
	}

	template <typename Arg, typename Q=R>
	typename std::enable_if<std::is_void<Q>::value>::type Continue(Arg&& val)
	{
		std::cout << "setting value (void)" << std::endl;

		m_callback(std::forward<Arg>(val));
		m_result.set_value();
	}

private:
	std::shared_future<T>   m_val;
	std::promise<R>         m_result;
	Function                m_callback;
	std::future<Token>      m_cont;
};

class Executor
{
public:
	virtual void Execute(const std::function<void()>& func) = 0;
};

template <typename T, typename Function>
auto TaskScheduler::Add(const std::shared_future<T>& val, Function&& func, Token& out, std::future<Token>&& cont)
{
	out.event = m_seq++;
	out.host  = this;

	auto task = std::make_shared<Task<T, Function>>(std::move(val), std::forward<Function>(func), std::move(cont));
	auto result = task->Result();

	m_tasks.emplace(out.event, std::move(task));
	return result;
}

inline void TaskScheduler::Schedule(Token token, Executor *executor)
{
	auto it = m_tasks.find(token.event);
	if (it != m_tasks.end())
	{
		executor->Execute([task=std::move(it->second), executor]
		{
			task->Execute(executor);
		});
		it = m_tasks.erase(it);
	}
}

class LocalExecutor : public Executor
{
public:
	void Execute(const std::function<void()>& func) override
	{
		func();
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
	auto Then(Func&& continuation, TaskScheduler *host, Executor *exe)
	{
		std::promise<Token> cont_token;

		Token token{};
		auto result = m_shared_state.share();
		auto cont   = host->Add(result, std::forward<Func>(continuation), token, cont_token.get_future());

		if (result.wait_for(std::chrono::system_clock::duration::zero()) == std::future_status::ready)
		{
			std::cout << "result ready" << std::endl;
			host->Schedule(token, exe);
		}
		else
		{
			std::cout << "result not ready" << std::endl;
			m_token.set_value(token);
		}
		
		return MakeFuture(std::move(cont), std::move(cont_token));
	}

private:
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
auto Async(Func&& func, Executor *exe)
{
	using T = decltype(func());
	
	std::promise<T> shared_state;
	Future<T> ret{shared_state.get_future()};

	std::thread{[
		func    = std::forward<Func>(func),
		result  = std::move(shared_state),
		token   = ret.GetToken(),
		exe
	] () mutable
	{
		// Run the function "func" in the spawned thread. The return value will be
		// moved to the lambda closure to be used later.
		result.set_value(func());
		
		// notify
		if (token.valid() && token.wait_for(std::chrono::system_clock::duration::zero()) == std::future_status::ready)
		{
			auto t = token.get();
			if (t.host)
				t.host->Schedule(t, exe);
		}

		std::cout << "detach" << std::endl;
	}}.detach();
	
	return ret;
}

} // end of namespace
