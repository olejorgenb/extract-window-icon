# { stdenv, glib, cairo, pkgconfig, xorg }: 
with import <nixpkgs> {};

stdenv.mkDerivation {
  name = "extract-window-icon";
  src = ./.;
  buildInputs =
    [ glib cairo pkgconfig ] 
    ++ (with xorg; [ xcbutilwm xcbutil libxcb ]);
}
