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

TEST_CASE( "Simple async multithread case", "[normal]" )
{
	QueueExecutor exe;
	
	// use two new threads to run the executor
	auto worker = exe.Spawn(2);

	auto future = async([]
	{
		std::this_thread::sleep_for(100ms);
		return 100;
	}, &exe);
	
	future.then([](int val)
	{
		REQUIRE(val == 100);
		return std::string{"abc"};
	}, &exe).then([](const std::string& s)
	{
		REQUIRE(s == "abc");
		std::this_thread::sleep_for(200ms);
	}, &exe);

	// Quit the worker threads
	exe.Quit();
	for (auto&& w : worker)
		w.join();
	
	// Run() called 3 times
	REQUIRE(exe.Count() == 3U);
}

TEST_CASE( "Simple async single thread case", "[normal]" )
{
	QueueExecutor exe;
	
	bool executed{false};
	auto future = async([&executed]
	{
		std::this_thread::sleep_for(200ms);
		executed = true;
		return 0.5;
	}, &exe);
	
	REQUIRE(!executed);
	REQUIRE(exe.Run() == 1);
	REQUIRE(executed);
	REQUIRE(exe.Count() == 1);
	
	executed = false;
	future.then([&executed](double val)
	{
		REQUIRE(val == 0.5);
		executed = true;
	}, &exe);
	
	REQUIRE(!executed);
	REQUIRE(exe.Run() == 1);
	REQUIRE(executed);
	REQUIRE(exe.Count() == 2);
}