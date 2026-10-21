# Optional GPU FFT dependencies

- VkFFT 1.3.4, commit `066a17c17068c0f11c9298d848c2976c71fad1c1`,
  https://github.com/DTolm/VkFFT (MIT, see VkFFT/LICENSE).
  Only the unmodified `vkFFT/` headers are vendored.
- Khronos OpenCL-Headers v2024.10.24, commit
  `886dc9b7ff48366c2adca31d8587cf47169ba893`,
  https://github.com/KhronosGroup/OpenCL-Headers (Apache-2.0, see
  OpenCL-Headers/LICENSE). Only the unmodified `CL/` headers are vendored.

The adapter loads the system OpenCL runtime at run time. No GPU SDK is needed
to build AMSignal and no OpenCL runtime is needed to run the CPU path.
Include these notices/licenses when redistributing the GPU-enabled build.
