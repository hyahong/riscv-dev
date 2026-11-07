#include <sdm.h>
#include <printk.h>

constexpr uint32_t coroutine_count = 4;
alignas(64) static uint64_t frames[SDM::MAX_COROUTINE_COUNT + 1][32];
alignas(16) static uint8_t stacks[coroutine_count][4096];

extern "C" void
sdmhnsw_demo (void)
{
	SDM &sdm = SDM::get ();
	SDM::Core *core = sdm.getCore (0);

	const auto worker = [] (unsigned int cid, void *) -> int {
		printk ("[SDM] cid=%d\n", cid);
		return 0;
	};

	core->clear ();
	core->setFrame (frames);

	for (uint32_t cid = 0; cid < coroutine_count; ++cid)
	{
		if (!core->addCoroutine (stacks[cid] + sizeof (stacks[cid]), worker, nullptr))
		{
			printk ("failed to add the coroutine to sdm core.\n");
			return;
		}
	}

	sdm.run (coroutine_count);
}
