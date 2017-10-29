/*
	Copyright © 2017 Wan Wai Ho <me@nestal.net>
	Distributed under the Boost Software License, Version 1.0.
	(See accompanying file LICENSE_1_0.txt or copy at
	http://www.boost.org/LICENSE_1_0.txt)
*/

//
// Created by nestal on 10/2/17.
//

#include <iostream>

#include "BrightFuture.hh"

#include "catch.hpp"

using namespace BrightFuture;
using namespace Catch::Matchers;

TEST_CASE("executors in future and promise")
{
	QueueExecutor exe;
	
	promise<void> subject{&exe};
	auto future = subject.get_future();
	
	REQUIRE(future.ExecutorToUse() == &exe);
	
	auto shared_future = future.share();
	REQUIRE(shared_future.ExecutorToUse() == &exe);
}

TEST_CASE("then() without specifying an executor")
{
	using namespace std::chrono_literals;
	
	QueueExecutor exe;
	auto thread = exe.Spawn();
	auto tid = thread.get_id();

	std::promise<void> ready;
	bool is_ready = false;
	
	auto fi = async([ready=ready.get_future(), &is_ready]() mutable
	{
		// wait until we attach a continuation before proceeding
		ready.get();
		REQUIRE(is_ready);
	}, &exe);
	
	bool run = false;
	
	// If we don't specify an executor, the continuation routine will be run in the same
	// executor as the async task that produce the future.
	auto then_fut = fi.then([tid, &run, &exe](auto fut)
	{
		REQUIRE(fut.ExecutorToUse() == &exe);
		REQUIRE(std::this_thread::get_id() == tid);
		run = true;
	});
	
	is_ready = true;
	ready.set_value();
	
	then_fut.wait();
	exe.Quit();
	thread.join();
}

TEST_CASE( "Inline executor", "[normal]")
{
	auto run = false;
	
	auto subject = DefaultExecutor();
	auto main_thread = std::this_thread::get_id();
	
	SECTION("run in the same thread")
	{
		auto fut = async([main_thread, &run]
		{
			REQUIRE(std::this_thread::get_id() == main_thread);
			run = true;
			return 100;
		}, subject);
		
		// no need to wait() because everything are in this thread
		
		REQUIRE(fut.is_ready());
		REQUIRE(fut.get() == 100);
	}
	SECTION("run in remote thread")
	{
		QueueExecutor exe2;
		auto worker = exe2.Spawn();
		auto q_tid = worker.get_id();
		
		std::promise<void> ready;
		
		auto fut = async([q_tid, &run, ready=ready.get_future()]() mutable
		{
			// Wait until the main thread has called then().
			// InlineExecutor will run the continuation routine inside then() if the
			// promise is already ready _before_ then() is called. We will to
			// avoid that in this test case.
			ready.get();
			
			REQUIRE(std::this_thread::get_id() == q_tid);
			run = true;
			return 100;
		}, &exe2);

		using namespace std::chrono_literals;
		std::this_thread::sleep_for(1s);
		
		// The InlineExecutor will run the callbacks in the same thread that schedule them.
		// In this case, the thread that schedule them is the QueueExecutor.
		bool run2 = false;
		auto end = fut.then([q_tid, main_thread, &run2](auto fint)
		{
			REQUIRE(std::this_thread::get_id() != main_thread);
			REQUIRE(std::this_thread::get_id() == q_tid);
			REQUIRE(fint.get() == 100);
			run2 = true;
		}, subject);
		
		// Set ready _after_ calling then() to make sure the continuation routine
		// is not run inside then().
		ready.set_value();
		end.wait();
		
		REQUIRE(run2);
		
		exe2.Quit();
		worker.join();
	}
	REQUIRE(run);
}

TEST_CASE( "Simple async multithread case", "[normal]" )
{
	using namespace std::chrono_literals;
	QueueExecutor exe;
	
	// use two new threads to run the executor
	auto worker = exe.Spawn(2);
	REQUIRE(worker.size() == 2);
	std::vector<std::thread::id> tids{worker.front().get_id(), worker.back().get_id()};
	
	auto fut = async([tids]
	{
		REQUIRE_THAT(tids, VectorContains(std::this_thread::get_id()));
		std::this_thread::sleep_for(1000ms);
		return 100;
	}, &exe);
	
	fut.then([tids, &exe](future<int> val)
	{
		REQUIRE(val.ExecutorToUse() == &exe);
		REQUIRE(val.is_ready());
		REQUIRE(val.get() == 100);
		REQUIRE_THAT(tids, VectorContains(std::this_thread::get_id()));
		return std::string{"abc"};
	}).then([tids, &exe](future<std::string> s)
	{
		REQUIRE(s.ExecutorToUse() == &exe);
		REQUIRE(s.is_ready());
		REQUIRE(s.get() == "abc");
		REQUIRE_THAT(tids, VectorContains(std::this_thread::get_id()));
		std::this_thread::sleep_for(200ms);
	}).wait();

	// Quit the worker threads
	exe.Quit();
	for (auto&& w : worker)
		w.join();
	
	// Run() called 3 times: once for async() callback, twice for then() callback
	REQUIRE(exe.TotalTaskExecuted() == 3U);
}

TEST_CASE( "Simple async single thread case", "[normal]" )
{
	using namespace std::chrono_literals;

	QueueExecutor exe;
	auto tid = std::this_thread::get_id();
	
	bool executed{false};
	auto fut = async([&executed, tid]
	{
		REQUIRE(tid == std::this_thread::get_id());
		std::this_thread::sleep_for(200ms);
		executed = true;
		return 0.5;
	}, &exe);
	
	REQUIRE(!executed);
	REQUIRE(exe.Run() == 1);
	REQUIRE(executed);
	REQUIRE(exe.TotalTaskExecuted() == 1);
	
	executed = false;
	fut.then([&executed, tid](auto fut_val)
	{
		REQUIRE(fut_val.is_ready());
		REQUIRE(tid == std::this_thread::get_id());
		REQUIRE(fut_val.get() == 0.5);
		executed = true;
	});
	
	REQUIRE(!executed);
	REQUIRE(exe.Run() == 1);
	REQUIRE(executed);
	REQUIRE(exe.TotalTaskExecuted() == 2);
}

TEST_CASE( "Two executors", "[normal]" )
{
	using namespace std::string_literals;

	QueueExecutor exe1, exe2;
	auto thread1 = exe1.Spawn();
	auto thread2 = exe2.Spawn();
	
	auto run1 = false, run2 = false;
	
	// Call async() to run something on exe1. Since exe1 has only one thread,
	// the ID of the thread running the task must be thread1.
	auto fut = async([tid=thread1.get_id(), &run1]
	{
		REQUIRE(std::this_thread::get_id() == tid);
		run1 = true;
		return "string"s;
	}, &exe1);
	
	// Similarly, run the continuation routine on exe2 and verify the thread ID.
	fut.then([tid=thread2.get_id(), &run2](auto s)
	{
		REQUIRE(s.get() == "string"s);
		REQUIRE(std::this_thread::get_id() == tid);
		run2 = true;
	}, &exe2).wait();
	
	exe1.Quit();
	exe2.Quit();
	thread1.join();
	thread2.join();
	
	REQUIRE(run1);
	REQUIRE(run2);
}

TEST_CASE( "WhenAll empty input", "[normal]" )
{
	bool run = false;
	std::vector<future<int>> in;
	auto subject = when_all(in.begin(), in.end()).then([&run](auto fut)
	{
		run = true;
	});
	REQUIRE(run);
}

TEST_CASE( "WhenAll 2 promises", "[normal]" )
{
	using namespace std::chrono_literals;
	QueueExecutor exe1;
	QueueExecutor exe2;
	auto thread1 = exe1.Spawn();
	auto thread2 = exe2.Spawn();
	
	future<bool> result;
	
	SECTION("2 future<int>'s")
	{
		std::vector<future<int>> futures;
		futures.push_back(async([]{ return 100; }, &exe1));
		futures.push_back(async([]{ return 101; }, &exe2));
		
		result = when_all(futures.begin(), futures.end()).then([](auto fut)
		{
			REQUIRE(fut.is_ready());
			auto vec_ints = fut.get();
			REQUIRE(vec_ints.size() == 2);
			REQUIRE(vec_ints.front() == 100);
			REQUIRE(vec_ints.back() == 101);
			return true;
		}, &exe2);
	}
	SECTION("2 shared_future<int>'s")
	{
		std::vector<shared_future<int>> futures;
		futures.push_back(async([]{ return 100; }, &exe1).share());
		futures.push_back(async([]{ return 101; }, &exe2).share());
		result = when_all(futures.begin(), futures.end()).then([](auto fut)
		{
			REQUIRE(fut.is_ready());
			auto vec_ints = fut.get();
			REQUIRE(vec_ints.size() == 2);
			REQUIRE(vec_ints.front() == 100);
			REQUIRE(vec_ints.back() == 101);
			return true;
		}, &exe2);
	}
	
	REQUIRE(result.get());
	
	exe1.Quit();
	exe2.Quit();
	thread1.join();
	thread2.join();
}


TEST_CASE("future<void>::then()", "[normal]")
{
	using namespace std::chrono_literals;
	QueueExecutor exe;
	auto thread = exe.Spawn();
	
	bool run{false};
	
	auto fut = async([]{std::this_thread::sleep_for(100ms);}, &exe);
	fut.then([&run](future<void>){
		run = true;
	}).wait();
	
	exe.Quit();
	thread.join();
	
	REQUIRE(run);
}

TEST_CASE("test shared_future::then()", "[normal]")
{
	using namespace std::chrono_literals;
	QueueExecutor exe;
	auto thread = exe.Spawn();
	
	bool run1{false}, run2{false};
	
	auto fut = async([]{std::this_thread::sleep_for(100ms);}, &exe).share();
	auto copy = fut;

	auto future_cont = fut.then([&run2](auto shut)
	{
		REQUIRE(shut.is_ready());
		shut.get();
		run2 = true;
	});
#ifndef _MSC_VER
	static_assert(std::is_same<decltype(future_cont), future<void>>::value);
#endif
	
	SECTION("call then() on the same shared_future")
	{
		bool run3 = false;
		fut.then([&run3](auto f)
		{
			REQUIRE(f.is_ready());
			run3 = true;
		}).wait();
		REQUIRE(run3);
	}
	SECTION("call then() on a copy")
	{
		copy.then([&run1](auto shut)  // "shut" = "SHared fUTure"
		{
			REQUIRE(shut.is_ready());
			run1 = true;
		}).wait();
		REQUIRE(run1);
	}
	future_cont.wait();

	REQUIRE(run2);

	exe.Quit();
	thread.join();
}

TEST_CASE("test exception in async", "[normal]")
{
	QueueExecutor exe;
	auto thread = exe.Spawn();
	
	bool run1{false}, run2{false};
	
	auto fut = async([]{throw -1;}, &exe);
	SECTION("call then() with a future with an exception")
	{
		fut.then([](auto fut)
		{
			REQUIRE_THROWS_AS(fut.get(), int&);
		});
	}
	SECTION("the returned future contains an exception")
	{
		REQUIRE_THROWS_AS(fut.get(), int&);
	}
	
	exe.Quit();
	thread.join();
}

TEST_CASE( "unwrap future", "[normal]" )
{
	QueueExecutor exe;
	auto thread = exe.Spawn();

	auto ffi = async([&exe]
	{
		return async([]
		{
			return 100;
		}, &exe);
	}, &exe);
	
	SECTION("then() on the future<future<T>>")
	{
		future<int>{std::move(ffi)}.then([](auto f)
		{
			REQUIRE(f.get() == 100);
		}, &exe).wait();
	}
	SECTION("get() and wait() on the future")
	{
		future<int> f{std::move(ffi)};
		REQUIRE(f.get() == 100);
	}
	
	exe.Quit();
	thread.join();
}
