#include "cudautil.h"
#include <string.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace {

bool replaceArchitectureOption(std::vector<std::string> &ownedArguments,
                               std::vector<const char*> &compileArguments,
                               const char *from,
                               const char *to)
{
  for (unsigned i = 0; i < ownedArguments.size(); ++i) {
    size_t pos = ownedArguments[i].find(from);
    if (pos != std::string::npos) {
      ownedArguments[i].replace(pos, strlen(from), to);
      compileArguments[i] = ownedArguments[i].c_str();
      return true;
    }
  }
  return false;
}

bool compileProgramToFile(const std::string &sourceFile,
                          const char *kernelName,
                          const char **arguments,
                          int argumentsNum,
                          bool preferNativeCubin)
{
  nvrtcProgram prog;
  NVRTC_SAFE_CALL(
    nvrtcCreateProgram(&prog,
                       sourceFile.c_str(),
                       "xpm.cu",
                       0,
                       NULL,
                       NULL));

  nvrtcResult compileResult = nvrtcCompileProgram(prog, argumentsNum, arguments);

  // Obtain compilation log from the program.
  size_t logSize;
  NVRTC_SAFE_CALL(nvrtcGetProgramLogSize(prog, &logSize));
  std::unique_ptr<char[]> log(new char[logSize]);
  NVRTC_SAFE_CALL(nvrtcGetProgramLog(prog, log.get()));

  if (compileResult != NVRTC_SUCCESS) {
    LOG_F(ERROR, "nvrtcCompileProgram error: %s", nvrtcGetErrorString(compileResult));
    LOG_F(ERROR, "%s\n", log.get());
    nvrtcDestroyProgram(&prog);
    return false;
  }

  std::ofstream bin(kernelName, std::ofstream::binary | std::ofstream::trunc);
  if (!bin) {
    LOG_F(ERROR, "Cannot open %s for writing", kernelName);
    nvrtcDestroyProgram(&prog);
    return false;
  }

#if defined(NVRTC_VERSION) && NVRTC_VERSION >= 11010
  if (preferNativeCubin) {
    size_t cubinSize = 0;
    nvrtcResult cubinSizeResult = nvrtcGetCUBINSize(prog, &cubinSize);
    if (cubinSizeResult == NVRTC_SUCCESS && cubinSize > 0) {
      std::unique_ptr<char[]> cubin(new char[cubinSize]);
      NVRTC_SAFE_CALL(nvrtcGetCUBIN(prog, cubin.get()));
      bin.write(cubin.get(), cubinSize);
      nvrtcDestroyProgram(&prog);
      LOG_F(INFO, "compiled native CUDA cubin: %s", kernelName);
      return true;
    }

    LOG_F(WARNING, "NVRTC did not produce cubin, falling back to PTX");
    nvrtcDestroyProgram(&prog);
    return false;
  }
#else
  if (preferNativeCubin) {
    LOG_F(WARNING, "NVRTC headers do not expose cubin retrieval, falling back to PTX");
    nvrtcDestroyProgram(&prog);
    return false;
  }
#endif

  // Obtain PTX from the program.
  size_t ptxSize;
  NVRTC_SAFE_CALL(nvrtcGetPTXSize(prog, &ptxSize));
  std::unique_ptr<char[]> ptx(new char[ptxSize]);
  NVRTC_SAFE_CALL(nvrtcGetPTX(prog, ptx.get()));
  bin.write(ptx.get(), ptxSize);
  nvrtcDestroyProgram(&prog);
  LOG_F(INFO, "compiled CUDA PTX: %s", kernelName);
  return true;
}

}

bool cudaCompileKernel(const char *kernelName,
                       const std::vector<const char*> &sources,
                       const char **arguments,
                       int argumentsNum,
                       CUmodule *module,
                       int majorComputeCapability,
                       int,
                       bool needRebuild,
                       bool preferNativeCubin)
{
  std::ifstream testfile(kernelName);
  if(needRebuild || !testfile) {
    LOG_F(INFO, "compiling ...");

    std::string sourceFile;
    for (auto &i: sources) {
      std::ifstream stream(i);
      std::string str((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
      sourceFile.append(str);
    }

    LOG_F(INFO, "source: %u bytes", (unsigned)sourceFile.size());
    if(sourceFile.size() < 1){
      LOG_F(ERROR, "source files not found or empty");
      return false;
    }

    std::vector<std::string> ownedArguments;
    std::vector<const char*> compileArguments;
    ownedArguments.reserve(argumentsNum);
    compileArguments.reserve(argumentsNum);
    for (int i = 0; i < argumentsNum; ++i) {
      ownedArguments.push_back(arguments[i]);
      compileArguments.push_back(ownedArguments.back().c_str());
    }

    bool nativeCubinRequested = preferNativeCubin;
#if !defined(NVRTC_VERSION) || NVRTC_VERSION < 11010
    nativeCubinRequested = false;
#endif
    nativeCubinRequested = nativeCubinRequested &&
      replaceArchitectureOption(ownedArguments, compileArguments, "compute_", "sm_");

    if (!compileProgramToFile(sourceFile,
                              kernelName,
                              compileArguments.data(),
                              argumentsNum,
                              nativeCubinRequested)) {
      if (!nativeCubinRequested)
        return false;

      LOG_F(WARNING, "native cubin compilation failed, falling back to PTX");
      for (int i = 0; i < argumentsNum; ++i) {
        ownedArguments[i] = arguments[i];
        compileArguments[i] = ownedArguments[i].c_str();
      }

      if (!compileProgramToFile(sourceFile,
                                kernelName,
                                compileArguments.data(),
                                argumentsNum,
                                false)) {
        return false;
      }
    }
  }

  std::ifstream bfile(kernelName, std::ifstream::binary);
  if(!bfile) {
    return false;
  }

  bfile.seekg(0, bfile.end);
  size_t binsize = bfile.tellg();
  bfile.seekg(0, bfile.beg);
  if(!binsize){
    LOG_F(ERROR, "%s empty", kernelName);
    return false;
  }

  std::unique_ptr<char[]> ptx(new char[binsize+1]);
  bfile.read(ptx.get(), binsize);
  bfile.close();

  CUresult result = cuModuleLoadDataEx(module, ptx.get(), 0, 0, 0);
  if (result != CUDA_SUCCESS) {
    if (result == CUDA_ERROR_INVALID_PTX || result == CUDA_ERROR_UNSUPPORTED_PTX_VERSION) {
      LOG_F(WARNING, "GPU Driver version too old, update recommended");
      LOG_F(WARNING, "Workaround: downgrade version in PTX to 6.0 ...");
      char *pv = strstr(ptx.get(), ".version ");
      if (pv) {
        pv[9] = '6';
        pv[11] = '0';
      }

      CUDA_SAFE_CALL(cuModuleLoadDataEx(module, ptx.get(), 0, 0, 0));
    } else {
      const char *msg;
      cuGetErrorName(result, &msg);
      LOG_F(ERROR, "Loading CUDA module failed with error %s", msg);
      return false;
    }
  }

  return true;
}
