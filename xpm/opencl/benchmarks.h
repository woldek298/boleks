#include "opencl.h"
#include "xpmclient.h"

void runBenchmarks(cl_context context,
                   openclPrograms &programs,
                   cl_device_id deviceId,
                   unsigned depth,
                   unsigned defaultGroupSize);
