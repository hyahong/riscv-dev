#ifndef _COROHNSW_H_
#define _COROHNSW_H_

#include <coroutine>
#include <stdint.h>
#include <stddef.h>

class Coro
{
	public:
		class Task;
		using Coroutine = Task (*)(unsigned int cid, void *arg);

		static constexpr uint32_t MAX_COROUTINE_COUNT = 32;
		static constexpr uintptr_t CACHE_LINE_SIZE = 64;

	public:
		class Task
		{
			public:
				struct promise_type;
				using Handle = std::coroutine_handle<promise_type>;

				struct promise_type
				{
					Task
					get_return_object () noexcept
					{
						return Task (Handle::from_promise (*this));
					}

					std::suspend_always
					initial_suspend () noexcept
					{
						return { };
					}

					std::suspend_always
					final_suspend () noexcept
					{
						return { };
					}

					void
					return_void () noexcept
					{
					}

					void
					unhandled_exception () noexcept
					{
						__builtin_trap ();
					}
				};

			public:
				Task () noexcept :
					m_handle (nullptr)
				{
				}

				explicit Task (Handle handle) noexcept :
					m_handle (handle)
				{
				}

				Task (const Task &) = delete;
				Task &operator= (const Task &) = delete;

				Task (Task &&other) noexcept :
					m_handle (other.m_handle)
				{
					other.m_handle = nullptr;
				}

				Task &
				operator= (Task &&other) noexcept
				{
					if (this != &other)
					{
						destroy ();

						m_handle = other.m_handle;
						other.m_handle = nullptr;
					}
					return *this;
				}

				~Task ()
				{
					destroy ();
				}

				bool
				done () const noexcept
				{
					return !m_handle || m_handle.done ();
				}

				void
				resume () const noexcept
				{
					if (!done ())
					{
						m_handle.resume ();
					}
				}

			private:
				void
				destroy () noexcept
				{
					if (m_handle)
					{
						m_handle.destroy ();
						m_handle = nullptr;
					}
				}

				Handle m_handle;
		};

	public:
		Coro () :
			m_tasks (),
			m_coroutine_count (0)
		{
		}

		~Coro ()
		{
		}

		void
		clear ()
		{
			uint32_t cid;

			for (cid = 0; cid < m_coroutine_count; ++cid)
			{
				m_tasks[cid] = Task ();
			}
			m_coroutine_count = 0;
		}

		bool
		addCoroutine (Coroutine func, void *arg)
		{
			if (!func || m_coroutine_count >= MAX_COROUTINE_COUNT)
			{
				return false;
			}

			m_tasks[m_coroutine_count] = func (m_coroutine_count, arg);

			++m_coroutine_count;
			return true;
		}

		bool
		run ()
		{
			uint32_t active, cid;

			active = 0;
			for (cid = 0; cid < m_coroutine_count; ++cid)
			{
				if (!m_tasks[cid].done ())
				{
					++active;
				}
			}
			if (active == 0)
			{
				return false;
			}

			while (active != 0)
			{
				for (cid = 0; cid < m_coroutine_count; ++cid)
				{
					const Task &task = m_tasks[cid];

					if (task.done ())
					{
						continue;
					}

					task.resume ();
					if (task.done ())
					{
						--active;
					}
				}
			}

			return true;
		}

		[[nodiscard]]
		static std::suspend_always
		yield () noexcept
		{
			return { };
		}

	private:
		Task m_tasks[MAX_COROUTINE_COUNT];
		uint32_t m_coroutine_count;
};

#endif
