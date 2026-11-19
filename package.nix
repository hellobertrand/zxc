{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  doxygen,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "zxc";
  version = "1.0.1";
  src = lib.cleanSource ./.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    doxygen
  ];

  cmakeFlags = [
    "-DBUILD_SHARED_LIBS=ON"
    "-DZXC_NATIVE_ARCH=OFF"
    "-DZXC_INSTALL=ON"
  ];

  doCheck = true;

  outputs = [
    "out"
    "dev"
    "man"
  ];

  meta = with lib; {
    description = "Lossless compression C library built for ultra-fast decode (faster than LZ4 at better ratios)";
    longDescription = ''
      ZXC is an asymmetric lossless compressor optimised for write-once /
      read-many workloads. Levels -1…-6 decode 1.1–2.4× faster than LZ4 at
      equal or better ratio; level -7 beats zstd -1 on both axes on modern CPUs.
      Features seekable archives, in-place decompression, dictionaries and
      multi-frame containers (format v9 / SOVERSION 6).
    '';
    homepage = "https://github.com/hellobertrand/zxc";
    changelog = "https://github.com/hellobertrand/zxc/releases/tag/v${finalAttrs.version}";
    license = licenses.bsd3;
    maintainers = with maintainers; [ kleindotc ];
    platforms = platforms.all;
    mainProgram = "zxc";
    pkgConfigModules = [ "libzxc" ];
  };
})
