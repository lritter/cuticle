# Cuticle

Cuticle is an experiment in thumbnailing using libvips and nodejs. I'm not a C programmer by trade so this is also a place for me to learn a bit about C and C++.

You're probably better off using something like [sharp](https://github.com/lovell/sharp) if you're doing this for real...

## Stuff n' Junk

The package is essentially a refactoring of [vipsthumbnail](https://github.com/jcupitt/libvips/blob/master/tools/vipsthumbnail.c) to allow it to be used as a library as well as a cli.

The package includes a slapped-together native extension as well as an executuable "hangnail" with many of the same options a vipsthumbnail. Use `hangnail --help` for more.