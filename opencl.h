/*
 * opencl.h
 *
 *  Created on: 01.05.2014
 *      Author: mad
 */

#ifndef OPENCL_H_
#define OPENCL_H_

#ifndef __APPLE__
#include <CL/cl.h>
#else
#include <OpenCL/opencl.h>
#endif
#include "loguru.hpp"
#include <stdio.h>
#include <string.h>
#include <vector>

#define OCL(error) \
  if(cl_int err = error){ \
    LOG_F(ERROR, "OpenCL error: %d at %s:%d\n", err, __FILE__, __LINE__); \
    exit(err); \
  }

#define OCLR(error, ret) \
  if(cl_int err = error){ \
    LOG_F(ERROR, "OpenCL error: %d at %s:%d\n", err, __FILE__, __LINE__); \
    return ret; \
  }

template<typename T>
class clBuffer {
public:
  
  clBuffer() {
    
    Size = 0;
    HostData = 0;
    DeviceData = 0;
    
  }
  
  ~clBuffer() {
    
    if(HostData)
      delete [] HostData;
    
    if(DeviceData)
      clReleaseMemObject(DeviceData);
    
  }
  
  cl_int init(cl_context gContext, int size, cl_mem_flags flags = 0) {
    Size = size;
    
    if(!(flags & CL_MEM_HOST_NO_ACCESS)){
      HostData = new T[Size];
      memset(HostData, 0, Size*sizeof(T));
    }else
      HostData = 0;

    cl_int error;
    if (flags & CL_MEM_HOST_NO_ACCESS)
      flags = CL_MEM_READ_WRITE;
    if (flags & CL_MEM_COPY_HOST_PTR)
      DeviceData = clCreateBuffer(gContext, flags, Size*sizeof(T), HostData, &error);
    else
      DeviceData = clCreateBuffer(gContext, flags, Size*sizeof(T), 0, &error);
    return error;
  }
  
  cl_int copyToDevice(cl_command_queue cq, bool blocking = true) {
    
    return clEnqueueWriteBuffer(cq, DeviceData, blocking, 0, Size*sizeof(T), HostData, 0, 0, 0);
    
  }
  
  cl_int copyToHost(cl_command_queue cq, bool blocking = true, unsigned size = 0) {
    
    if(size == 0)
      size = Size;
    
    return clEnqueueReadBuffer(cq, DeviceData, blocking, 0, size*sizeof(T), HostData, 0, 0, 0);
    
  }
  
  T& get(int index) {
    return HostData[index];
  }
  
  T& operator[](int index) {
    return HostData[index];
  }
  
public:
  
  int Size;
  T* HostData;
  cl_mem DeviceData;
  
  
};


bool clInitialize(const char *requiredPlatform, std::vector<cl_device_id> &gpus);
bool clCompileKernel(cl_context gContext,
                     cl_device_id gpu,
                     const char *binaryName,
                     const char **sources,
                     unsigned sourcesNum,
                     const char *arguments,
                     cl_int *binstatus,
                     cl_program *gProgram,
                     bool needRebuild);

bool clCompileGCNKernel(cl_context gContext,
                        cl_device_id gpu,
                        const char *binaryName,
                        const char **sources,
                        unsigned sourcesNum,
                        const char *arguments,
                        cl_int *binstatus,
                        cl_program *gProgram,
                        bool needRebuild);



#endif /* OPENCL_H_ */
