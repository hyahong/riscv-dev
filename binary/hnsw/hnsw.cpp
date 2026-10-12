#include <hnsw.h>
#include <malloc.h>
#include <printk.h>

using namespace hnsw;

constexpr size_t K = 10;
constexpr size_t EF = 100;
constexpr uintptr_t HNSW_DUMP_ADDRESS = 0xECCCC000;
constexpr uintptr_t DDR_END = 0x180000000;

struct FileMemory
{
	const uint8_t *data;
	size_t bytes;
};

static const char *
status_name (Status status)
{
	switch (status)
	{
		case Status::Ok:
			return "Ok";
		case Status::InvalidArgument:
			return "InvalidArgument";
		case Status::InvalidGraph:
			return "InvalidGraph";
		case Status::WorkspaceTooSmall:
			return "WorkspaceTooSmall";
		case Status::CandidateHeapFull:
			return "CandidateHeapFull";
	}
	return "Unknown";
}

static int
run_search (const FileMemory &dump, const int16_t *query, size_t query_dim)
{
	GraphView graph;
	Status status = open_graph (dump.data, dump.bytes, graph);
	if (status != Status::Ok)
	{
		printk ("open_graph: %s\n", status_name (status));
		return 1;
	}
	if (graph.h.dim_ != query_dim)
	{
		printk ("Query dimension mismatch: graph=%llu, query=%lu\n",
					(unsigned long long) graph.h.dim_, (unsigned long) query_dim);
		return 1;
	}

	const size_t n = size_t (graph.h.cur_element_count);
	const size_t ef = EF > K ? EF : K;
	const size_t nearest_capacity = (ef < n ? ef : n) + 1;
	Workspace work = {
		static_cast<HeapItem *> (malloc (n * sizeof(HeapItem))),
		n,
		static_cast<HeapItem *> (malloc (nearest_capacity * sizeof(HeapItem))),
		nearest_capacity,
		static_cast<uint32_t *> (malloc (n * sizeof(uint32_t))),
		n,
		0
	};
	int exit_code = 1;

	if (!work.candidates || !work.nearest || !work.visited)
	{
		printk ("Cannot allocate search workspace\n");
	}
	else
	{
		initialize_workspace(work);

		Result result[K];
		size_t count = 0;

		status = search_knn (graph, query, K, EF, work, result, K, count);

		printk ("Nodes: %lu, dim: %llu, entry ID: %u, max level: %d\n",
					 (unsigned long) n, (unsigned long long)graph.h.dim_, graph.h.enterpoint_node_, graph.h.maxlevel_);
		printk ("Search: k=%lu, ef=%lu\n", (unsigned long) K, (unsigned long) EF);

		if (status != Status::Ok)
		{
			printk ("search_knn: %s\n", status_name(status));
		}
		else
		{
			exit_code = 0;
			for (size_t i = 0; i < count; ++i)
				printk ("%lu: id=%u label=%llu distance=%lld\n",
								 (unsigned long) (i + 1), result[i].id, (unsigned long long) result[i].label, (long long) result[i].distance);
		}
	}

	free (work.candidates);
	free (work.nearest);
	free (work.visited);

	return exit_code;
}

extern "C"
void
hnsw_demo (void)
{
	int16_t query[] = {
		378, 576, -1973, -422, 1074, 660, -311, -1053, -72, 668, 1020, -1612, 938, -348, 894, 1801,
		272, -388, 128, -1259, -1053, 990, 897, -40, 1091, 1519, 553, 1174, -722, 735, 1100, 524,
		397, -1576, -1883, -1660, -99, 958, -279, 1136, -824, -31, 1174, -324, 400, 545, 111, -1336,
		-1331, -829, 99, -940, 705, 427, 643, -879, 1540, 489, -146, -759, 1362, 264, -1626, 240,
		819, -811, -724, 2151, -1269, -621, 0, -755, 1932, 1051, -1447, -167, 384, 1092, -865, -301,
		105, -1022, -854, 699, 112, 793, -1050, -1022, -1254, -155, -642, -2008, -2156, 1592, -1353, -626,
		-88, 732, 31, -3475, 1715, 1324, 1354, 826, -861, -1358, -677, 591, -717, -68, 1225, -676,
		-149, -615, -688, -442, -2419, -417, 107, 1080, 55, -3712, -441, -3819, 544, -1184, 72, -1011,
		851, 634, 1344, -2214, -1972, 13694, -2715, -726, -636, -1927, 487, -1216, -2204, -484, -2039, 1181,
		-1729, -1247, -638, -1691, -1383, 213, -436, 167, -453, 2243, -1249, -1128, 1851, -1229, -536, -285,
		-1482, 1469, 1003, 61, -697, -628, 1441, -108, -1416, -31, -251, -1192, 671, 2088, 607, 589,
		61, 306, -584, 521, 172, 27, -1274, -1136, -706, -82, 864, 65, -1226, 404, -898, -1571,
		-388, 519, -1703, 740, -108, 2443, 787, -894, 355, -146, 1755, -638, 1294, -450, 5, -1774,
		1239, 473, 1107, 3103, 1516, 313, 435, 714, 312, 1725, 1666, 946, 816, -995, -216, -456,
		-858, -1855, 766, -759, 417, 351, 500, 1071, 1153, 1049, 469, -1333, 1357, -1891, -1464, -1816,
		-1337, 1160, 459, 1845, 0, -2117, 78, 660, -971, 364, -471, -370, -259, 1207, 333, 777,
		-552, 852, -177, 754, 877, -383, 733, -70, 734, 138, 2039, -2615, 358, 1151, -701, -248,
		1217, -640, -441, -640, 944, 533, 424, 666, -115, 1165, 557, 820, -887, 227, -158, 21,
		-55, -899, -1550, -500, -1012, 386, -1225, -58, 86, 1654, -587, -910, -626, 158, 1072, -1270,
		712, -2685, 1234, -495, 492, 849, 263, 622, 13691, 558, -732, -1601, 1420, -1466, -572, -2229,
		835, 2061, -561, -542, 187, 512, -754, -562, 2176, -606, -1164, -331, 665, -368, 47, -449,
		206, 1365, 152, -120, 518, 209, 1004, -2176, 2006, -386, 174, 2043, -1047, -1131, 396, 715,
		-918, 1333, -130, -167, 684, -100, 689, 857, 3314, 160, -94, 1338, -572, -659, -1408, 46,
		-328, -597, -405, -726, -174, 403, 301, 64, -685, -787, -979, 1255, 916, 1109, 1070, -2155,
		1144, -1226, -1560, 1107, 411, -280, -2693, -620, 308, 842, 1951, -980, -454, 198, -1438, 1444,
		290, -746, 79, -171, -544, 5, 494, -1026, 933, 825, 2569, -616, 549, 1345, 964, 2026,
		-556, -360, 2051, 1041, 2076, -1741, -97, -4427, 620, 250, 515, 610, 307, 281, 505, -1962,
		166, -1265, 1214, -1241, -3644, 258, -304, -516, -25, 1231, -1435, 209, 98, 261, -2321, 765,
		-1020, 448, -1635, 1319, 3497, 304, -114, -877, -894, -1686, 605, 969, 695, -440, -678, 1470,
		2077, 1400, -566, 1045, -665, -115, 1440, -377, -495, 585, -948, 1062, -346, -1317, -842, 1457,
		341, -939, 320, 794, -548, 1138, -1366, -2008, 1311, 513, -1621, 956, 1110, 5995, 232, -1490,
		394, 1562, -2489, 401, 389, -312, 736, 2388, -702, 1813, -2411, 2723, 156, 911, -52, -767
	};

	const uint8_t *data = reinterpret_cast<const uint8_t *> (HNSW_DUMP_ADDRESS);
	const uint64_t bytes = read_u64 (data + offsetof (DumpHeader, total_bytes));
	if (bytes < sizeof (DumpHeader) || bytes > DDR_END - HNSW_DUMP_ADDRESS)
	{
		printk ("Invalid HNSW dump size: %llu\n", (unsigned long long) bytes);
		return;
	}

	const FileMemory dump = { data, size_t (bytes) };
	printk ("HNSW dump: %p, %llu bytes\n", (const void *) data, (unsigned long long) bytes);

	run_search (dump, query, sizeof (query) / sizeof (query[0]));

	// the dump belongs to the PCIe loader. only malloc'd workspace is freed.
}

