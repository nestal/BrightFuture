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
	
	template <typename Func>
	void ExecuteTask(Func&& task)
	{
		m_ios.post(std::forward<Func>(task));
	}

private:
	boost::asio::io_service& m_ios;
};

} // end of namespace
