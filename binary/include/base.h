#ifndef _BASE_H_
#define _BASE_H_

#include <hnsw.h>

namespace hnsw
{

bool graph_prepare (GraphView &graph);

bool workspace_prepare (const GraphView &graph, Workspace *works, size_t count, size_t k, size_t ef);
void workspace_release (Workspace *works, size_t count);

void print_elapsed (uint64_t start_cycle, uint64_t end_cycle);

}

#endif
