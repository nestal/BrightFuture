/*
	Copyright © 2017 Wan Wai Ho <me@nestal.net>
	Distributed under the Boost Software License, Version 1.0.
	(See accompanying file LICENSE_1_0.txt or copy at
	http://www.boost.org/LICENSE_1_0.txt)
*/

#pragma once

#include "../BrightFuture.hh"

#include <QEvent>
#include <QtCore/QCoreApplication>

namespace BrightFuture {

class QtGuiExecutor : public ExecutorBase<QtGuiExecutor>
{
private:
	template <typename Func>
	class FunctorEvent : public QEvent
	{
	public:
		explicit FunctorEvent(Func&& func) : QEvent{QEvent::User}, m_func{std::move(func)}
		{
		}

		~FunctorEvent() override
		{
			try
			{
				m_func();
			}
			catch (...)
			{
			}
		}

	private:
		Func m_func;
	};

	friend Executor& TheQtGuiExecutor();
	
public:
	QtGuiExecutor() = default;
	
	template <typename Func>
	static void PostMain(Func&& func, QObject *dest = qApp)
	{
		QCoreApplication::postEvent(dest, new FunctorEvent<Func>(std::forward<Func>(func)));
	}

	// Called by ExecutorBase using CRTP
	static void Post(TaskPointer&& task)
	{
		PostMain([task=std::move(task)]{task->Execute();});
	}
};

inline Executor& TheQtGuiExecutor()
{
	static QtGuiExecutor inst;
	return inst;
}

} // end of namespace
