{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  name = "cpuj-dev";

  nativeBuildInputs = with pkgs; [
    gcc
    binutils
    gdb
    pkg-config
  ];

  shellHook = ''
    echo "cpuj1 dev shell — run 'make' to build, 'make test' to test"
    echo "usage: ./cpujvm examples/count.asm"
  '';
}