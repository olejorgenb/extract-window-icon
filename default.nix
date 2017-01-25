# { stdenv, glib, cairo, pkgconfig, xorg }: 
with import <nixpkgs> {};

stdenv.mkDerivation {
  version = "0.1";
  name = "extract-window-icon-${version}";

  src = ./.;

  buildInputs =
    [ glib cairo pkgconfig ] 
    ++ (with xorg; [ xcbutilwm xcbutil libxcb ]);

  installFlags = "PREFIX=\${out}";

}
