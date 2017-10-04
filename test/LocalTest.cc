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
	auto exe_up = std::make_unique<QueueExecutor>();
	auto exe    = exe_up.get();
	TaskScheduler sch{std::move(exe_up)};
	
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

	int count = 1;
	while (exe->Run())
		count++;
	
	// Run() called 3 times
	REQUIRE(count == 3);
}
