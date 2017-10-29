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

class BoostAsioExecutor : public BrightFuture::ExecutorBase<BoostAsioExecutor>
{
public:
	BoostAsioExecutor(boost::asio::io_service& ios) : m_ios{ios}
	{
	}
	
	void Post(TaskPointer&& task)
	{
		m_ios.post([task = std::move(task)] { task->Execute(); });
	}

private:
	boost::asio::io_service& m_ios;
};

class BoostAsioExecutorService : public boost::asio::io_service::service
{
public:
	explicit BoostAsioExecutorService(boost::asio::io_service& ios) :
		service{ios},
		m_exec{std::make_shared<BoostAsioExecutor>(ios)}
	{
	}
	
	ExecutorPointer Get() const
	{
		return m_exec;
	}
	
	static boost::asio::io_service::id id;
	
	void shutdown_service() override {}
	
private:
	std::shared_ptr<BoostAsioExecutor>  m_exec;
};

//boost::asio::io_service::id BoostAsioExecutorService::id;

} // end of namespace
