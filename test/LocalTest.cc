/*
	Copyright © 2017 Wan Wai Ho <me@nestal.net>
	Distributed under the Boost Software License, Version 1.0.
	(See accompanying file LICENSE_1_0.txt or copy at
	http://www.boost.org/LICENSE_1_0.txt)
*/

//
// Created by nestal on 10/2/17.
//

#include "BrightFuture.hh"

#include "catch.hpp"

using namespace BrightFuture;
using namespace std::chrono_literals;

TEST_CASE( "Async simple", "[normal]" )
{
	TaskScheduler<QueueExecutor> sch;
	
	// use a new thread to run the executor
	int count = 0;
	std::thread worker{[&sch, &count]
	{
		std::size_t c;
		while ((c = sch.Run()) > 0)
			count += c;
	}};
	
	auto future = Async([]
	{
		std::this_thread::sleep_for(2s);
		return 100;
	}, &sch);
	
	future.Then([](int val)
	{
		REQUIRE(val == 100);
		return std::string{"abc"};
	}, &sch).Then([](const std::string& s)
	{
		REQUIRE(s == "abc");
		std::this_thread::sleep_for(1s);
	}, &sch);

	// Quit the worker thread
	sch.Quit();
	worker.join();
	
	// Run() called 3 times
	REQUIRE(count == 3);
}
