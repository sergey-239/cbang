/******************************************************************************\

          This file is part of the C! library.  A.K.A the cbang library.

                Copyright (c) 2003-2019, Cauldron Development LLC
                   Copyright (c) 2003-2017, Stanford University
                               All rights reserved.

         The C! library is free software: you can redistribute it and/or
        modify it under the terms of the GNU Lesser General Public License
       as published by the Free Software Foundation, either version 2.1 of
               the License, or (at your option) any later version.

        The C! library is distributed in the hope that it will be useful,
          but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
                 Lesser General Public License for more details.

         You should have received a copy of the GNU Lesser General Public
                 License along with the C! library.  If not, see
                         <http://www.gnu.org/licenses/>.

        In addition, BSD licensing may be granted on a case by case basis
        by written permission from at least one of the copyright holders.
           You may request written permission by emailing the authors.

                  For information regarding this software email:
                                 Joseph Coffland
                          joseph@cauldrondevelopment.com

\******************************************************************************/

#include "OpenCLLibrary.h"

#include "GPUVendor.h"

#include <cbang/Exception.h>
#include <cbang/StdTypes.h>
#include <cbang/Catch.h>

#include <string.h>

using namespace std;
using namespace cb;


#undef CBANG_EXCEPTION
#define CBANG_EXCEPTION DynamicLibraryException

#ifdef _WIN32
static const char *openclLib = "OpenCL.dll";

#elif __APPLE__
static const char *openclLib = "/System/Library/Frameworks/OpenCL.framework/OpenCL";

size_t strnlen(const char *s, size_t n) {
  size_t l = 0;
  while (l < n && *s) l++;
  return l;
}

#else
static const char *openclLib = "libOpenCL.so";
#endif


// OpenCL API call
#ifdef _WIN32
#define CL_API_CALL __stdcall
#else
#define CL_API_CALL
#endif


namespace {
  // OpenCL typedefs
  typedef int8_t cl_char;
  typedef int32_t cl_int;
  typedef uint32_t cl_uint;
  typedef uint64_t cl_ulong;

  typedef void *cl_device_id;
  typedef cl_uint cl_device_info;
  typedef void *cl_platform_id;
  typedef cl_ulong cl_device_type;

  typedef union {
    struct {
      cl_uint type;
      cl_uint data[5];
    } raw;

    struct {
      cl_uint type;
      cl_char unused[17];
      cl_char bus;
      cl_char device;
      cl_char function;
    } pcie;
  } cl_device_topology_amd;


  // OpenCL functions
  typedef cl_int (CL_API_CALL *clGetDeviceInfo_t)
    (cl_device_id, cl_device_info, size_t, void *, size_t *);

  typedef cl_int (CL_API_CALL *clGetPlatformIDs_t)
    (cl_uint, cl_platform_id *, cl_uint *);

  typedef cl_int (CL_API_CALL *clGetDeviceIDs_t)
    (cl_platform_id, cl_device_type, cl_uint, cl_device_id *, cl_uint *);


  // OpenCL defines
  enum _cl_device_type_t {
    CL_DEVICE_TYPE_DEFAULT =           1 << 0,
    CL_DEVICE_TYPE_CPU =               1 << 1,
    CL_DEVICE_TYPE_GPU =               1 << 2,
    CL_DEVICE_TYPE_ACCELERATOR =       1 << 3,
    CL_DEVICE_TYPE_CUSTOM =            1 << 4,
    CL_DEVICE_TYPE_ALL =               0xffffffff,
  };

  enum _cl_param_t {
    CL_DEVICE_TYPE =                   0x1000,
    CL_DEVICE_VENDOR_ID =              0x1001,
    CL_DRIVER_VERSION =                0x102d,
    CL_DEVICE_VERSION =                0x102f,
    CL_DEVICE_TOPOLOGY_AMD =           0x4037,
    CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD = 0x0001,
    CL_DEVICE_PCI_BUS_ID_NV =          0x4008,
    CL_DEVICE_PCI_SLOT_ID_NV =         0x4009,
  };
}


#define DYNAMIC_CALL(lib, name, args) {                         \
    cl_int err;                                                 \
    name##_t name = (name##_t)lib->getSymbol(#name);            \
    if ((err = name args)) THROW(#name "() returned " << err);  \
  }


namespace {
  size_t getDeviceInfoSize(DynamicLibrary *lib, cl_device_id dev,
                           cl_device_info param) {
    size_t size = 0;

    DYNAMIC_CALL(lib, clGetDeviceInfo, (dev, param, 0, 0, &size));

    return size;
  }


  SmartPointer<char> getDeviceInfoData(DynamicLibrary *lib, cl_device_id dev,
                                       cl_device_info param,
                                       size_t *retSize = 0) {
    size_t size = getDeviceInfoSize(lib, dev, param);
    if (retSize) *retSize = size;

    // Get param
    SmartPointer<char>::Array data = new char[size];
    if (size)
      DYNAMIC_CALL(lib, clGetDeviceInfo, (dev, param, size, data.get(), 0));

    return data;
  }


  string getDeviceInfoString(DynamicLibrary *lib, cl_device_id dev,
                             cl_device_info param) {
    size_t size = 0;
    SmartPointer<char> data = getDeviceInfoData(lib, dev, param, &size);

    return string(data.get(), strnlen(data.get(), size));
  }
}


OpenCLLibrary::OpenCLLibrary(Inaccessible) : DynamicLibrary(openclLib) {
  // Get num platforms
  cl_uint numPlatforms;
  DYNAMIC_CALL(this, clGetPlatformIDs, (0, 0, &numPlatforms));

  // Get platform ids
  SmartPointer<cl_platform_id>::Array platforms =
    new cl_platform_id[numPlatforms];
  DYNAMIC_CALL(this, clGetPlatformIDs, (numPlatforms, platforms.get(), 0));

  for (cl_uint i = 0; i < numPlatforms; i++) {
    cl_platform_id platform = platforms[i];

    // Get devices
    cl_uint numDevices = 0;
    SmartPointer<cl_device_id>::Array devices;

    try {
      DYNAMIC_CALL(this, clGetDeviceIDs,
                   (platform, CL_DEVICE_TYPE_ALL, 0, 0, &numDevices));

      devices = new cl_device_id[numDevices];
      DYNAMIC_CALL(this, clGetDeviceIDs,
                   (platform, CL_DEVICE_TYPE_ALL, numDevices, devices.get(),
                    0));
    } catch (const Exception &e) {numDevices = 0;}

    for (cl_uint j = 0; j < numDevices; j++)
      try {
        ComputeDevice cd = getDeviceInfo(devices[j]);

        // Set indices
        cd.platformIndex = i;
        cd.deviceIndex = j;

        if (cd.isValid()) this->devices.push_back(cd);
      } CATCH_ERROR;
  }
}


const ComputeDevice &OpenCLLibrary::getDevice(unsigned i) const {
  if (getDeviceCount() <= i) THROW("Invalid OpenCL device index " << i);
  return devices.at(i);
}


VersionU16 OpenCLLibrary::getDriverVersion(void *device) {
  string version = getDeviceInfoString(this, device, CL_DRIVER_VERSION);

  // Parse driver version
  if (!version.empty()) {
    // Format varies, many drivers don't following the prescribed format.
    // Look for last group matching regex \d+\.\d+
    vector<string> parts;
    String::tokenize(version, parts);

    unsigned i = parts.size();
    while (i--) {
      string v = parts[i].substr(0, parts[i].find_first_not_of("1234567890."));

      size_t end = v.find('.');
      if (end == string::npos) continue;
      end = v.find('.', end + 1);

      return VersionU16(v.substr(0, end));
    }
  }

  return VersionU16();
}


VersionU16 OpenCLLibrary::getComputeVersion(void *device) {
  string version = getDeviceInfoString(this, device, CL_DEVICE_VERSION);

  // Parse device version
  if (!version.empty()) {
    // Format: OpenCL <major>.<minor> <vendor info>
    vector<string> parts;
    String::tokenize(version, parts);
    if (1 < parts.size()) return VersionU16(parts[1]);
  }

  return VersionU16();
}


int32_t OpenCLLibrary::getVendorID(void *device) {
  cl_uint vendorID = 0;
  DYNAMIC_CALL(this, clGetDeviceInfo,
               (device, CL_DEVICE_VENDOR_ID, sizeof(vendorID), &vendorID, 0));
  return vendorID;
}


bool OpenCLLibrary::isGPU(void *device) {
  cl_device_type devType = 0;
  DYNAMIC_CALL(this, clGetDeviceInfo,
               (device, CL_DEVICE_TYPE, sizeof(cl_device_type), &devType, 0));
  return devType & CL_DEVICE_TYPE_GPU;
}


void OpenCLLibrary::getAMDPCIInfo(void *device, ComputeDevice &cd) {
  size_t size = 0;
  SmartPointer<char> data =
    getDeviceInfoData(this, device, CL_DEVICE_TOPOLOGY_AMD, &size);

  auto topology = (cl_device_topology_amd *)data.get();

  if (sizeof(cl_device_topology_amd) <= size &&
      (int)topology->raw.type == CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD) {

    cd.pciBus      = topology->pcie.bus;
    cd.pciSlot     = topology->pcie.device;
    cd.pciFunction = topology->pcie.function;
  }
}


void OpenCLLibrary::getNVIDIAPCIInfo(void *device, ComputeDevice &cd) {
  cl_int busID = -1;
  cl_int slotID = -1;

  DYNAMIC_CALL(this, clGetDeviceInfo,
               (device, CL_DEVICE_PCI_BUS_ID_NV, sizeof(busID), &busID, 0));
  DYNAMIC_CALL(this, clGetDeviceInfo,
               (device, CL_DEVICE_PCI_SLOT_ID_NV, sizeof(slotID), &slotID, 0));

  cd.pciBus      = busID;
  cd.pciSlot     = slotID >> 3;
  cd.pciFunction = slotID & 7;
}


void OpenCLLibrary::getPCIInfo(void *device, ComputeDevice &cd) {
  try {
    switch (cd.vendorID) {
    case GPUVendor::VENDOR_AMD: getAMDPCIInfo(device, cd); break;
    case GPUVendor::VENDOR_NVIDIA: getNVIDIAPCIInfo(device, cd); break;
    default: break;
    }
  } CATCH_DEBUG(3);
}


ComputeDevice OpenCLLibrary::getDeviceInfo(void *device) {
  ComputeDevice cd;

  cd.driverVersion = getDriverVersion(device);
  cd.computeVersion = getComputeVersion(device);
  cd.vendorID = getVendorID(device);
  cd.gpu = isGPU(device);
  getPCIInfo(device, cd);

  return cd;
}
