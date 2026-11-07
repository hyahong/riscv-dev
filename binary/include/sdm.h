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
					m_frame_ptr (nullptr),
					m_coroutine_count (0)
				{
				}

				~Core ()
				{
				}

				void clear ()
				{
					m_frame_ptr = nullptr;
					m_coroutine_count = 0;
				}

				void setFrame (void *ptr)
				{
					m_frame_ptr = ptr;
					m_coroutine_count = 0;

					ASM::setFrame (reinterpret_cast<uintptr_t> (ptr));
				}

				bool addCoroutine (void *sp, Coroutine func, void *arg)
				{
					uintptr_t frame_base, stack_top;
					uint64_t *frame;
					uint32_t i;

					if (!m_frame_ptr || !sp || !func ||
							m_coroutine_count >= MAX_COROUTINE_COUNT)
					{
						return false;
					}

					// aligned address
					frame_base = reinterpret_cast<uintptr_t> (m_frame_ptr);
					stack_top = reinterpret_cast<uintptr_t> (sp);
					if ((frame_base & 7) ||
							(stack_top & 15) ||
							(reinterpret_cast<uintptr_t> (func) & 1))
					{
						return false;
					}

					// clear
					frame = static_cast<uint64_t *> (m_frame_ptr) + m_coroutine_count * 32;
					for (i = 0; i < 32; ++i)
					{
						frame[i] = 0;
					}
					// ra
					frame[1] = reinterpret_cast<uintptr_t> (&coroutineEnd);
					// sp
					frame[2] = stack_top;
					// gp
					frame[3] = ASM::getGp ();
					// tp
					frame[4] = ASM::getTp ();
					// a0 (arg0)
					frame[10] = m_coroutine_count;
					// a1 (arg1)
					frame[11] = reinterpret_cast<uintptr_t> (arg);

					ASM::setFunc (m_coroutine_count, func);

					++m_coroutine_count;
					return true;
				}

			private:
				[[noreturn]]
				static void coroutineEnd ()
				{
					ASM::end ();
				}

				void *m_frame_ptr;
				uint32_t m_coroutine_count;
		};

	private:
		class ASM
		{
			public:
				__attribute__((always_inline))
				static uintptr_t getGp ()
				{
					uintptr_t gp;

					asm volatile ("mv %0, gp" : "=r"(gp));
					return gp;
				}

				__attribute__((always_inline))
				static uintptr_t getTp ()
				{
					uintptr_t tp;

					asm volatile ("mv %0, tp" : "=r"(tp));
					return tp;
				}

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

#endif
