## The docker method

Fully prepared docker images with a toolchain installed are provided from valios/vali-toolchain. This repository
comes with a Dockerfile that you can use to build the OS with. You can also include the userspace
by downloading the build .tar file from the userspace repository and keep it in
the same folder as you run the docker command. Then docker will include it when you build an image file.

Building the OS with no artifacts
```
docker build --build-arg ARCH={i386,amd64} --file build.Dockerfile .
```

Building an .img file locally with docker and export it to host:
```
DOCKER_BUILDKIT=1 docker build --build-arg ARCH={i386,amd64} --output type=local,dest=build --file nightly.Dockerfile .
```

## The DIY method

Before you set up anything you must set up environmental variables that are used by
the project. This is only true if you do not do development with a docker container.

| Variable              | Required |                        Description                         |
|-----------------------|:---------|:----------------------------------------------------------:|
| CROSS                 | Yes      |      Points to where the cross-compiler is installed.      |
| VALI_APPLICATION_PATH | No\*     | Points to where the Vali applications/libraries are built. |

\* Can be supplied to include built applications in the kernel image

### Building the toolchain

Toolchain scripts are located [here](https://github.com/Meulengracht/vali-toolchain). You should run the scripts in this order:
- depends.sh
- checkout.sh
- build-cross.sh

If you are not on Linux, you should download the llvm-project git and build it yourself using the cmake command
```
cmake -S llvm -B build -G "Unix Makefiles" 
   -DLLVM_TEMPORARILY_ALLOW_OLD_TOOLCHAIN=True
   -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra;libcxx;libcxxabi;libunwind;lldb;compiler-rt;lld'
   -DCMAKE_BUILD_TYPE=Release
   -DLLVM_INCLUDE_TESTS=Off
   -DLLVM_INCLUDE_EXAMPLES=Off
   -DCMAKE_INSTALL_PREFIX=/path/to/install
   -DLLVM_DEFAULT_TARGET_TRIPLE=amd64-uml-vali
```

### Building the operating system

Use cmake to configure the project, and supply the options you'd like to customize the build
with.

```
mkdir build
cd build
cmake -DVALI_ARCH={i386,amd64} ..
make
```

The above should be enough to build the kernel, services and drivers.

### Building the userspace (optional)

Just building this repository is not enough for the full OS experience. You need to checkout the userspace
repository that contains C++ runtime and applications and build those as well. For this you need to have the
SDK/DDK installed (```make install``` - remember to set CMAKE_INSTALL_PREFIX when configuring) and
the appropriate environmental variables' setup. Then the command ```make install_{img,vmdk}``` will automatically
pull in all built applications and libraries into the OS image.

| Option        |                                  Description                                  |
|---------------|:-----------------------------------------------------------------------------:|
| VALI_SDK_PATH | The path to the installed SDK, usually the prefix Vali is installed with      |

Then follow the instructions located [here](https://github.com/Meulengracht/vali-userspace) to get the sources for the applications.

### Generating the OS image

Build targets for generating OS images from the build are provided in the cmake system. You
can easily generate .img or .vmdk (more formats to come as requested). The following command
will also automatically include any files located in your VALI_APPLICATION_PATH.

```
cd <build> # where you have your build folder
make install_img # to generate a .img file
make install_vmdk # to generate a .vmdk file
```
