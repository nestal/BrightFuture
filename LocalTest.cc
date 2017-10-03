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
#include "Async.hh"

#include "catch.hpp"

using namespace exe;
using namespace std::chrono_literals;

TEST_CASE( "Async simple", "[normal]" )
{
	TaskScheduler sch;
	LocalExecutor exec;
	
	auto future = Async([]
	{
		std::this_thread::sleep_for(2s);
		return 100;
	}, &exec);
	
	future.Then([](int val)
	{
		REQUIRE(val == 100);
		return std::string{"abc"};
	}, &sch, &exec).Then([](const std::string& s)
	{
		std::cout << "then2 " << s << std::endl;
		REQUIRE(s == "abc");
		std::this_thread::sleep_for(1s);
	}, &sch, &exec);
	
	using namespace std::chrono_literals;
	while (sch.Count() > 0)
		std::this_thread::sleep_for(1s);

	std::cout << "quitting" << std::endl;
}
