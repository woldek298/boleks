#include "cudautil.h"
#include <string.h>
#include <fstream>
#include <iostream>
#include <memory>

bool cudaCompileKernel(const char *kernelName,
                       const std::vector<const char*> &sources,
                       const char **arguments,
                       int argumentsNum,
                       CUmodule *module,
                       int majorComputeCapability,
                       int,
                       bool needRebuild)
{
  std::ifstream testfile(kernelName);
  bool builtAsPtxText = true;

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

    nvrtcProgram prog;
    NVRTC_SAFE_CALL(
      nvrtcCreateProgram(&prog,
                         sourceFile.c_str(),
                         "xpm.cu",
                         0,
                         NULL,
                         NULL));

    int nvrtcMajor = 0;
    int nvrtcMinor = 0;
    NVRTC_SAFE_CALL(nvrtcVersion(&nvrtcMajor, &nvrtcMinor));

    std::vector<std::string> optionStorage;
    optionStorage.reserve(argumentsNum + 4);
    bool cuda128OrNewer = (nvrtcMajor > 12) || (nvrtcMajor == 12 && nvrtcMinor >= 8);
    bool blackwellOrNewer = majorComputeCapability >= 12;

    for (int i = 0; i < argumentsNum; ++i) {
      if (arguments[i] && arguments[i][0] != '\0') {
        std::string opt(arguments[i]);
        if (cuda128OrNewer && blackwellOrNewer &&
            opt.find("--gpu-architecture=compute_") == 0) {
          opt.replace(strlen("--gpu-architecture="), strlen("compute_"), "sm_");
        }
        optionStorage.emplace_back(opt);
      }
    }

    if (cuda128OrNewer && blackwellOrNewer) {
      optionStorage.emplace_back("--extra-device-vectorization");
      optionStorage.emplace_back("--restrict");
      LOG_F(INFO, "Enabling CUDA 12.8 Blackwell compile opts: --extra-device-vectorization --restrict");
    }

    std::vector<const char*> optionPtrs;
    optionPtrs.reserve(optionStorage.size());
    for (const std::string &opt : optionStorage) {
      optionPtrs.push_back(opt.c_str());
    }

    nvrtcResult compileResult = nvrtcCompileProgram(prog,
                                                    static_cast<int>(optionPtrs.size()),
                                                    optionPtrs.empty() ? nullptr : optionPtrs.data());

    // Obtain compilation log from the program.
    size_t logSize;
    NVRTC_SAFE_CALL(nvrtcGetProgramLogSize(prog, &logSize));
    std::unique_ptr<char[]> log(new char[logSize]);
    NVRTC_SAFE_CALL(nvrtcGetProgramLog(prog, log.get()));
    if (compileResult != NVRTC_SUCCESS) {
      LOG_F(ERROR, "nvrtcCompileProgram error: %s", nvrtcGetErrorString(compileResult));
      LOG_F(ERROR, "%s\n", log.get());
      return false;
    }

    std::vector<char> image;
    if (cuda128OrNewer && blackwellOrNewer) {
      size_t cubinSize = 0;
      nvrtcResult cubinSizeResult = nvrtcGetCUBINSize(prog, &cubinSize);
      if (cubinSizeResult == NVRTC_SUCCESS && cubinSize > 0) {
        image.resize(cubinSize);
        NVRTC_SAFE_CALL(nvrtcGetCUBIN(prog, image.data()));
        builtAsPtxText = false;
        LOG_F(INFO, "nvrtc output format: CUBIN (%u bytes)", (unsigned)image.size());
      }
    }

    if (image.empty()) {
      // Obtain PTX from the program.
      size_t ptxSize;
      NVRTC_SAFE_CALL(nvrtcGetPTXSize(prog, &ptxSize));
      image.resize(ptxSize);
      NVRTC_SAFE_CALL(nvrtcGetPTX(prog, image.data()));
      builtAsPtxText = true;
      LOG_F(INFO, "nvrtc output format: PTX (%u bytes)", (unsigned)image.size());
    }

    // Destroy the program.
    NVRTC_SAFE_CALL(nvrtcDestroyProgram(&prog));

    {
      std::ofstream bin(kernelName, std::ofstream::binary | std::ofstream::trunc);
      bin.write(image.data(), image.size());
      bin.close();
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
  ptx[binsize] = '\0';

  if (builtAsPtxText && strstr(ptx.get(), ".version ") == nullptr) {
    builtAsPtxText = false;
  }

  CUresult result = cuModuleLoadDataEx(module, ptx.get(), 0, 0, 0);
  if (result != CUDA_SUCCESS) {
    if (builtAsPtxText && (result == CUDA_ERROR_INVALID_PTX || result == CUDA_ERROR_UNSUPPORTED_PTX_VERSION)) {
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
