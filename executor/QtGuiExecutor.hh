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

class QtGuiExecutor :
	public ExecutorBase<QtGuiExecutor>,
	public std::enable_shared_from_this<QtGuiExecutor>
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

	QtGuiExecutor() = default;
	friend Executor* TheQtGuiExecutor();
	
	struct Private {};

public:
	explicit QtGuiExecutor(Private) : QtGuiExecutor{} {}
	
	template <typename Func>
	static void Post(Func&& func, QObject *dest = qApp)
	{
		QCoreApplication::postEvent(dest, new FunctorEvent<Func>(std::forward<Func>(func)));
	}

	// Called by ExecutorBase using CRTP
	template <typename Func>
	static void ExecuteTask(Func&& task)
	{
		Post(std::forward<Func>(task));
	}
	
	static void Post(TaskPointer&& task)
	{
		Post([task=std::move(task)]{task->Execute();});
	}
	
	std::shared_ptr<BrightFuture::Executor> ShareFromThis()
	{
		return shared_from_this();
	}
	
	std::shared_ptr<const BrightFuture::Executor> ShareFromThis() const
	{
		return shared_from_this();
	}
};

inline Executor* TheQtGuiExecutor()
{
	static auto inst = std::make_shared<QtGuiExecutor>(QtGuiExecutor::Private{});
	return inst.get();
}

} // end of namespace
