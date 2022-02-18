/*
*  Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted (subject to the limitations in the
*  disclaimer below) provided that the following conditions are met:
*
*      * Redistributions of source code must retain the above copyright
*        notice, this list of conditions and the following disclaimer.
*
*      * Redistributions in binary form must reproduce the above
*        copyright notice, this list of conditions and the following
*        disclaimer in the documentation and/or other materials provided
*        with the distribution.
*
*      * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
*        contributors may be used to endorse or promote products derived
*        from this software without specific prior written permission.
*
*  NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
*  GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
*  HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
*   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
*  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
*  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
*  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
*  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
*  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
*  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
*  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
*  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <C2Component.h>
#include <stdio.h>
#include <dlfcn.h>

namespace QTI {
struct QC2ComponentStoreFactory {
  virtual ~QC2ComponentStoreFactory() = default;
  virtual std::shared_ptr<C2ComponentStore> getInstance() = 0;
};


// symbol name for getting the factory (library = libqcodec2_core.so)
static constexpr const char * kFn_QC2ComponentStoreFactoryGetter = "QC2ComponentStoreFactoryGetter";

using QC2ComponentStoreFactoryGetter_t
  = QC2ComponentStoreFactory * (*)(int majorVersion, int minorVersion);

}

using namespace QTI;

int main()
{
  void *lib = dlopen("libqcodec2_core.so", RTLD_NOW);
  if (lib == nullptr) {
    printf("failed to open %s: %s", "libqcodec2_core.so", dlerror());
    return -1;
  }

  auto factoryGetter =
    (QC2ComponentStoreFactoryGetter_t)dlsym(lib, kFn_QC2ComponentStoreFactoryGetter);

  if (factoryGetter == nullptr) {
    printf("failed to load symbol %s: %s", kFn_QC2ComponentStoreFactoryGetter, dlerror());
    dlclose(lib);
    return -1;
  }

  auto c2StoreFactory = (*factoryGetter)(1, 0);    // get version 1.0
  if (c2StoreFactory == nullptr) {
    printf("failed to get Store factory !");
    dlclose(lib);
    return -1;
  }

  std::shared_ptr<C2ComponentStore> store = c2StoreFactory->getInstance();
  auto components = store->listComponents();
  for (auto component : components) {
    printf("enumerate component : %s\n", component->name.c_str());
  }

  delete c2StoreFactory;
  dlclose(lib);

  return 0;
}

