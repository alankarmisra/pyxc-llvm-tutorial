#ifndef PYXC_LINKER_H
#define PYXC_LINKER_H

#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdlib>
#include <string>
#include <vector>

#include "lld/Common/Driver.h"

// Declare only the LLD drivers we use. Definitions come from linked lld libs.
LLD_HAS_DRIVER(coff)
LLD_HAS_DRIVER(elf)
LLD_HAS_DRIVER(macho)

class PyxcLinker {
public:
  static bool Link(const std::string &objFile, const std::string &runtimeObj,
                   const std::string &outputExe) {
    std::vector<std::string> ObjFiles = {objFile};
    return Link(ObjFiles, runtimeObj, outputExe);
  }

  static bool Link(const std::vector<std::string> &objFiles,
                   const std::string &runtimeObj,
                   const std::string &outputExe) {

    // Determine platform
    llvm::Triple triple((llvm::sys::getDefaultTargetTriple()));

    if (triple.isOSLinux()) {
      return LinkELF(objFiles, runtimeObj, outputExe);
    } else if (triple.isOSDarwin()) {
      return LinkMachO(objFiles, runtimeObj, outputExe);
    } else if (triple.isOSWindows()) {
      return LinkPE(objFiles, runtimeObj, outputExe);
    }

    llvm::errs() << "Unsupported platform\n";
    return false;
  }

private:
  static bool LinkELF(const std::vector<std::string> &objFiles,
                      const std::string &runtimeObj,
                      const std::string &outputExe) {
    std::vector<const char *> args = {
        "ld.lld",
        "-o",
        outputExe.c_str(),
    };
    for (const auto &Obj : objFiles)
      args.push_back(Obj.c_str());

    if (!runtimeObj.empty()) {
      args.push_back(runtimeObj.c_str());
    }

    // Add system libraries
    args.push_back("-lc");
    args.push_back("-dynamic-linker");
    args.push_back("/lib64/ld-linux-x86-64.so.2");

    return lld::elf::link(args, llvm::outs(), llvm::errs(), false, false);
  }

  static bool LinkMachO(const std::vector<std::string> &objFiles,
                        const std::string &runtimeObj,
                        const std::string &outputExe) {
    llvm::Triple triple((llvm::sys::getDefaultTargetTriple()));
    std::string arch = triple.getArchName().str();
    if (arch.empty())
      arch = "arm64";

    const char *sdkRootEnv = std::getenv("SDKROOT");
    std::string sdkRoot =
        sdkRootEnv ? sdkRootEnv : "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk";
    if (!llvm::sys::fs::exists(sdkRoot))
      sdkRoot = "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk";

    // ld64.lld requires an explicit platform tuple on Darwin.
    std::string platformVersion;
    if (const char *depTarget = std::getenv("MACOSX_DEPLOYMENT_TARGET");
        depTarget && *depTarget) {
      platformVersion = depTarget;
    } else {
#if defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__)
      int ver = __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__;
      int major = ver / 10000;
      int minor = (ver / 100) % 100;
      int patch = ver % 100;
      platformVersion = std::to_string(major) + "." + std::to_string(minor) +
                        "." + std::to_string(patch);
#else
      llvm::VersionTuple osVersion = triple.getOSVersion();
      if (!osVersion.empty())
        platformVersion = osVersion.getAsString();
      else
        platformVersion = "11.0";
#endif
    }
    const char *minVersion = platformVersion.c_str();
    const char *sdkVersion = platformVersion.c_str();

    std::vector<const char *> args = {
        "ld64.lld",
        "-o",
        outputExe.c_str(),
        "-arch",
        arch.c_str(),
        "-platform_version",
        "macos",
        minVersion,
        sdkVersion,
    };

    if (!sdkRoot.empty()) {
      args.push_back("-syslibroot");
      args.push_back(sdkRoot.c_str());
    }

    for (const auto &Obj : objFiles)
      args.push_back(Obj.c_str());

    if (!runtimeObj.empty()) {
      args.push_back(runtimeObj.c_str());
    }

    args.push_back("-lSystem");

    return lld::macho::link(args, llvm::outs(), llvm::errs(), false, false);
  }

  static bool LinkPE(const std::vector<std::string> &objFiles,
                     const std::string &runtimeObj,
                     const std::string &outputExe) {
    // Create persistent string before building args
    std::string outArg = "/out:" + outputExe;

    std::vector<const char *> args = {
        "lld-link",
        outArg.c_str(),
    };
    for (const auto &Obj : objFiles)
      args.push_back(Obj.c_str());

    if (!runtimeObj.empty()) {
      args.push_back(runtimeObj.c_str());
    }

    // Add Windows system libraries
    args.push_back("/defaultlib:libcmt");

    return lld::coff::link(args, llvm::outs(), llvm::errs(), false, false);
  }
};

#endif // PYXC_LINKER_H
