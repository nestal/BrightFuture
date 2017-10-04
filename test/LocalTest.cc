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
	QueueExecutor exe;
	
	// use two new threads to run the executor
	std::vector<std::thread> worker;
	worker.push_back(exe.Spawn());
	worker.push_back(exe.Spawn());
	
	auto future = Async([]
	{
		std::this_thread::sleep_for(2s);
		return 100;
	}, &exe);
	
	future.Then([](int val)
	{
		REQUIRE(val == 100);
		return std::string{"abc"};
	}, &exe).Then([](const std::string& s)
	{
		REQUIRE(s == "abc");
		std::this_thread::sleep_for(1s);
	}, &exe);

	// Quit the worker threads
	exe.Quit();
	for (auto&& w : worker)
		w.join();
	
	// Run() called 3 times
	REQUIRE(exe.Count() == 3U);
}
