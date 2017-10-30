/*
	Copyright © 2017 Wan Wai Ho <me@nestal.net>
	Distributed under the Boost Software License, Version 1.0.
	(See accompanying file LICENSE_1_0.txt or copy at
	http://www.boost.org/LICENSE_1_0.txt)
*/

//
// Created by nestal on 10/19/17.
//

#pragma once

#include "../BrightFuture.hh"

#include <boost/asio.hpp>

namespace BrightFuture {

class BoostAsioExecutor :
	public BrightFuture::ExecutorBase<BoostAsioExecutor>,
	public boost::asio::io_service::service
{
public:
	explicit BoostAsioExecutor(boost::asio::io_service& ios) : service{ios}
	{
	}
	
	void Post(TaskPointer&& task)
	{
		get_io_service().post([task = std::move(task)] { task->Execute(); });
	}
	
	void shutdown_service() override {}
	
	static boost::asio::io_service::id id;
};

} // end of namespace
