#ifndef _SDM_H_
#define _SDM_H_

#include <stdint.h>

class SDM
{
	public:
		using Coroutine = int (*)(unsigned int cid, void* arg);

		static constexpr uint32_t MAX_CORE_COUNT = 1;
		static constexpr uint32_t MAX_COROUTINE_COUNT = 32;

		class Core
		{
			public:
				Core () :
					m_frame_ptr (nullptr)
				{
				}

				~Core ()
				{
				}

				void clear ()
				{
					m_frame_ptr = nullptr;
				}

				void setFrame (void *ptr)
				{
					m_frame_ptr = ptr;
				}

				bool addCoroutine (void *sp, Coroutine func, void *arg)
				{
					if (!m_frame_ptr)
					{
						return false;
					}


					return true;
				}

			private:
				void *m_frame_ptr;
		};

	private:
		class ASM
		{
			public:
				__attribute__((always_inline))
				static void setFunc (uint32_t cid, Coroutine func)
				{
					asm volatile (".insn r 0x0b, 1, 0, x0, %0, %1" :: "r"(cid), "r"(func) : "memory");
				}

				__attribute__((always_inline))
				static void setFrame (uintptr_t ptr)
				{
					asm volatile(".insn r 0x0b, 1, 1, x0, %0, x0" :: "r"(ptr) : "memory");
				}

				__attribute__((always_inline))
				static void setActiveCount (uint32_t count)
				{
					asm volatile (".insn r 0x0b, 1, 2, x0, %0, x0" :: "r"(count) : "memory");
				}

				__attribute__((always_inline))
				static void run ()
				{
					asm volatile (".insn r 0x0b, 1, 3, x0, x0, x0" ::: "memory");
				}

				[[noreturn]]
				__attribute__((always_inline))
				static void end ()
				{
					asm volatile (".insn r 0x0b, 1, 4, x0, x0, x0" ::: "memory");
					__builtin_unreachable();
				}
		};

	public:
		static SDM& get ()
		{
			static SDM instance;

			return instance;
		}

		Core *getCore (uint32_t hartid)
		{
			if (hartid >= MAX_CORE_COUNT)
			{
				return nullptr;
			}
			return &m_cores[hartid];
		}

		bool run (uint32_t active)
		{
			if (active <= 0)
			{
				return false;
			}

			ASM::setActiveCount (active);
			ASM::run ();

			return true;
		}
		
	private:
		SDM () :
			m_cores ()
		{
		}

		~SDM ()
		{
		}
		
	private:
		SDM::Core m_cores[MAX_CORE_COUNT];
};

void test ()
{
	SDM &sdm = SDM::get ();
	SDM::Core *core = sdm.getCore (0);

	core->clear ();
	core->addCoroutine (test, nullptr, 0);
	core->addCoroutine (test, nullptr, 0);
	core->addCoroutine (test, nullptr, 0);

	sdm.run ();
}

#endif
