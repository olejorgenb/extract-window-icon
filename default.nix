# { stdenv, glib, cairo, pkgconfig, xorg }: 
with import <nixpkgs> {};

stdenv.mkDerivation {
  name = "extract-pixmap";
  src = ./.;
  buildInputs =
    [ glib cairo pkgconfig ] 
    ++ (with xorg; [ xcbutilwm xcbutil libxcb ]);
}
