#!/bin/sh

mkdir -p quake
mkdir -p quake/id1
rm -f quake/*
cp COPYING quake/copying
cp README.DIST quake/readme
cp quake.ttp quake/quake.ttp

rm -f quake.zip
zip -o quake.zip quake/*

gh release delete latest --cleanup-tag -y
gh release create latest --notes "latest"
gh release upload latest quake.zip --clobber
