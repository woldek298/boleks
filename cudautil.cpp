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

  if (needRebuild || !testfile) {
    LOG_F(INFO, "compiling ...");

    std::string sourceFile;
    for (auto &i : sources) {
      std::ifstream stream(i);
      std::string str((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
      sourceFile.append(str);
    }

    LOG_F(INFO, "source: %u bytes", (unsigned)sourceFile.size());
    if (sourceFile.empty()) {
      LOG_F(ERROR, "source files not found or empty");
      return false;
    }

    int nvrtcMajor = 0;
    int nvrtcMinor = 0;
    NVRTC_SAFE_CALL(nvrtcVersion(&nvrtcMajor, &nvrtcMinor));
    bool cuda128OrNewer = (nvrtcMajor > 12) || (nvrtcMajor == 12 && nvrtcMinor >= 8);
    bool blackwellOrNewer = majorComputeCapability >= 12;

    auto buildOptionList = [&](bool blackwellTuned) {
      std::vector<std::string> optionStorage;
      optionStorage.reserve(argumentsNum + 4);
      for (int i = 0; i < argumentsNum; ++i) {
        if (arguments[i] && arguments[i][0] != '\0') {
          std::string opt(arguments[i]);
          if (blackwellTuned && opt.find("--gpu-architecture=compute_") == 0) {
            opt.replace(strlen("--gpu-architecture="), strlen("compute_"), "sm_");
          }
          optionStorage.emplace_back(opt);
        }
      }

      if (blackwellTuned) {
        optionStorage.emplace_back("--extra-device-vectorization");
        optionStorage.emplace_back("--restrict");
      }

      return optionStorage;
    };

    auto compileWithOptions = [&](const std::vector<std::string> &opts,
                                  std::vector<char> &image,
                                  bool &isPtxText,
                                  std::string &compileLog) -> bool {
      nvrtcProgram prog;
      NVRTC_SAFE_CALL(
        nvrtcCreateProgram(&prog,
                           sourceFile.c_str(),
                           "xpm.cu",
                           0,
                           NULL,
                           NULL));

      std::vector<const char*> optionPtrs;
      optionPtrs.reserve(opts.size());
      for (const std::string &opt : opts)
        optionPtrs.push_back(opt.c_str());

      nvrtcResult compileResult = nvrtcCompileProgram(prog,
                                                      static_cast<int>(optionPtrs.size()),
                                                      optionPtrs.empty() ? nullptr : optionPtrs.data());

      size_t logSize = 0;
      NVRTC_SAFE_CALL(nvrtcGetProgramLogSize(prog, &logSize));
      std::unique_ptr<char[]> log(new char[logSize]);
      NVRTC_SAFE_CALL(nvrtcGetProgramLog(prog, log.get()));
      compileLog.assign(log.get());

      if (compileResult != NVRTC_SUCCESS) {
        NVRTC_SAFE_CALL(nvrtcDestroyProgram(&prog));
        return false;
      }

      image.clear();
      isPtxText = true;
      if (blackwellOrNewer && cuda128OrNewer) {
        size_t cubinSize = 0;
        nvrtcResult cubinSizeResult = nvrtcGetCUBINSize(prog, &cubinSize);
        if (cubinSizeResult == NVRTC_SUCCESS && cubinSize > 0) {
          image.resize(cubinSize);
          NVRTC_SAFE_CALL(nvrtcGetCUBIN(prog, image.data()));
          isPtxText = false;
        }
      }

      if (image.empty()) {
        size_t ptxSize = 0;
        NVRTC_SAFE_CALL(nvrtcGetPTXSize(prog, &ptxSize));
        image.resize(ptxSize);
        NVRTC_SAFE_CALL(nvrtcGetPTX(prog, image.data()));
        isPtxText = true;
      }

      NVRTC_SAFE_CALL(nvrtcDestroyProgram(&prog));
      return true;
    };

    bool tryBlackwellTuned = cuda128OrNewer && blackwellOrNewer;
    std::vector<std::string> primaryOptions = buildOptionList(tryBlackwellTuned);
    std::vector<char> image;
    std::string compileLog;

    if (tryBlackwellTuned) {
      LOG_F(INFO, "Trying CUDA 12.8 Blackwell compile opts: --extra-device-vectorization --restrict");
    }

    bool compileOk = compileWithOptions(primaryOptions, image, builtAsPtxText, compileLog);
    if (!compileOk && tryBlackwellTuned) {
      LOG_F(WARNING, "Blackwell tuned compile failed, retrying fallback PTX compile.");
      LOG_F(WARNING, "%s", compileLog.c_str());
      std::vector<std::string> fallbackOptions = buildOptionList(false);
      compileOk = compileWithOptions(fallbackOptions, image, builtAsPtxText, compileLog);
    }

    if (!compileOk) {
      LOG_F(ERROR, "nvrtcCompileProgram error: %s", nvrtcGetErrorString(NVRTC_ERROR_COMPILATION));
      LOG_F(ERROR, "%s", compileLog.c_str());
      return false;
    }

    LOG_F(INFO,
          "nvrtc output format: %s (%u bytes)",
          builtAsPtxText ? "PTX" : "CUBIN",
          (unsigned)image.size());

    std::ofstream bin(kernelName, std::ofstream::binary | std::ofstream::trunc);
    bin.write(image.data(), image.size());
    bin.close();
  }

  std::ifstream bfile(kernelName, std::ifstream::binary);
  if (!bfile) {
    return false;
  }

  bfile.seekg(0, bfile.end);
  size_t binsize = bfile.tellg();
  bfile.seekg(0, bfile.beg);
  if (!binsize) {
    LOG_F(ERROR, "%s empty", kernelName);
    return false;
  }

  std::unique_ptr<char[]> ptx(new char[binsize + 1]);
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
