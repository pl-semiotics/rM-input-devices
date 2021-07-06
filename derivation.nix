{ stdenv, lib, linuxHeaders, linuxPackages, targetPlatform }:

stdenv.mkDerivation {
  pname = "rM-input-devices";
  version = "0.0.1";
  src = lib.cleanSource ./.;
  buildInputs = [ linuxHeaders ];
  REMARKABLE_VERSION = targetPlatform.rmVersion;
  UINPUT_KO = "${linuxPackages.kernel}/lib/modules/${linuxPackages.kernel.modDirVersion}/kernel/drivers/input/misc/uinput.ko";
  # work around EABI version issues with bfd
  preConfigure = "LD=$LD.gold";
  outputs = [ "out" "dev" ];
  installPhase = ''
    mkdir -p $out/bin
    cp build/rM-mk-uinput{,-standalone} $out/bin
    mkdir -p $dev/include $dev/lib
    cp rM-input-devices.h $dev/include
    cp build/librM-input-devices{.so,-standalone.a} $dev/lib
  '';
}
