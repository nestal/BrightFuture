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

TEST_CASE( "Simple async multithread case", "[normal]" )
{
	using namespace std::chrono_literals;
	QueueExecutor exe;
	
	// use two new threads to run the executor
	auto worker = exe.Spawn(2);
	REQUIRE(worker.size() == 2);
	std::vector<std::thread::id> tids = {worker.front().get_id(), worker.back().get_id()};
	
	auto future = async([tids]
	{
		REQUIRE_THAT(tids, VectorContains(std::this_thread::get_id()));
		std::this_thread::sleep_for(1000ms);
		return 100;
	}, &exe);
	
	future.then([tids](std::future<int> val)
	{
		REQUIRE(val.get() == 100);
		REQUIRE_THAT(tids, VectorContains(std::this_thread::get_id()));
		return std::string{"abc"};
	}, &exe).then([tids](std::future<std::string> s)
	{
		REQUIRE(s.get() == "abc");
		REQUIRE_THAT(tids, VectorContains(std::this_thread::get_id()));
		std::this_thread::sleep_for(200ms);
	}, &exe).wait();

	// Quit the worker threads
	exe.Quit();
	for (auto&& w : worker)
		w.join();
	
	// Run() called 3 times: once for async() callback, twice for then() callback
	REQUIRE(exe.Count() == 3U);
}

TEST_CASE( "Simple async single thread case", "[normal]" )
{
	using namespace std::chrono_literals;

	QueueExecutor exe;
	auto tid = std::this_thread::get_id();
	
	bool executed{false};
	auto future = async([&executed, tid]
	{
		REQUIRE(tid == std::this_thread::get_id());
		std::this_thread::sleep_for(200ms);
		executed = true;
		return 0.5;
	}, &exe);
	
	REQUIRE(!executed);
	REQUIRE(exe.Run() == 1);
	REQUIRE(executed);
	REQUIRE(exe.Count() == 1);
	
	executed = false;
	future.then([&executed, tid](std::future<double> val)
	{
		REQUIRE(tid == std::this_thread::get_id());
		REQUIRE(val.get() == 0.5);
		executed = true;
	}, &exe);
	
	REQUIRE(!executed);
	REQUIRE(exe.Run() == 1);
	REQUIRE(executed);
	REQUIRE(exe.Count() == 2);
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
	auto future = async([tid=thread1.get_id(), &run1]
	{
		REQUIRE(std::this_thread::get_id() == tid);
		run1 = true;
		return "string"s;
	}, &exe1);
	
	// Similarly, run the continuation routine on exe2 and verify the thread ID.
	future.then([tid=thread2.get_id(), &run2](std::future<std::string> s)
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

TEST_CASE( "WhenAll 2 promises", "[normal]" )
{
	using namespace std::chrono_literals;
	QueueExecutor exe1, exe2;
	auto thread1 = exe1.Spawn();
	auto thread2 = exe2.Spawn();
	
	future<bool> result;
	
	SECTION("2 future<int>'s")
	{
		std::vector<future<int>> futures;
		futures.push_back(async([]{ return 100; }, &exe1));
		futures.push_back(async([]{ return 101; }, &exe2));
		
		result = when_all(futures.begin(), futures.end(), &exe1).then(
			[](std::future<std::vector<int>> fints)
			{
				auto ints = fints.get();
				REQUIRE(ints.size() == 2);
				REQUIRE(ints.front() == 100);
				REQUIRE(ints.back() == 101);
				return true;
			}, &exe2
		);
	}
	SECTION("2 shared_future<int>'s")
	{
		std::vector<shared_future<int>> futures;
		futures.push_back(async([]{ return 100; }, &exe1).share());
		futures.push_back(async([]{ return 101; }, &exe2).share());
		result = when_all(futures.begin(), futures.end(), &exe1).then(
			[](std::future<std::vector<int>> fints)
			{
				auto ints = fints.get();
				REQUIRE(ints.size() == 2);
				REQUIRE(ints.front() == 100);
				REQUIRE(ints.back() == 101);
				return true;
			}, &exe2
		);
	}
	
//	std::cout << "waiting" << std::endl;
	REQUIRE(result.get());
//	std::cout << "wait OK" << std::endl;
	
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
	
	auto future = async([]{std::this_thread::sleep_for(100ms);}, &exe);
	future.then([&run](std::future<void>){
		run = true;
	}, &exe).wait();
	
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
	
	auto future = async([]{std::this_thread::sleep_for(100ms);}, &exe).share();
	auto copy = future;
	
	auto copy_cont = copy.then([&run1](std::shared_future<void>){
		run1 = true;
	}, &exe);

	auto future_cont = future.then([&run2](std::shared_future<void>){
		run2 = true;
	}, &exe);

	copy_cont.wait();
	future_cont.wait();

	REQUIRE(run1);
	REQUIRE(run2);

	exe.Quit();
	thread.join();
}
