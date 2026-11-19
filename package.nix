{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  doxygen,
  makeWrapper,
  less,
  gnugrep,
  bash,
}:
stdenv.mkDerivation (finalAttrs: {
  pname = "zxc";
  version =
    let
      h = builtins.readFile ./include/zxc_constants.h;
      get = n: builtins.head (builtins.match ".*#define ZXC_VERSION_${n} ([0-9]+).*" h);
    in
    "${get "MAJOR"}.${get "MINOR"}.${get "PATCH"}";
  src = lib.cleanSource ./.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    doxygen
    makeWrapper
  ];

  cmakeFlags = [
    "-DBUILD_SHARED_LIBS=ON"
    "-DZXC_NATIVE_ARCH=OFF"
    "-DZXC_INSTALL=ON"
  ];

  # zxcless / zxcgrep resolve less, grep, sh and zxc vía PATH.
  postInstall = lib.optionalString (!stdenv.hostPlatform.isWindows) ''
    for prog in zxcless zxcgrep; do
      wrapProgram "$out/bin/$prog" \
       --prefix PATH : ${
         lib.makeBinPath [
           less
           gnugrep
           bash
         ]
       }:$out/bin
    done
  '';

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
    platforms = platforms.all;
    mainProgram = "zxc";
    pkgConfigModules = [ "libzxc" ];
  };
})
