<p align="center">
  <img src="images/foom-logo/foom-logo.png" alt="FOOM" width="520">
</p>

FOOM is a program for drawing orienteering maps. It runs on Windows, macOS, Linux and Android,
reads and writes OCD files, and is free software.

Its native file format is `.foom`. Files with the extensions `.omap` and `.xmap` are read and
can still be written.


## Building

See [`INSTALL.md`](INSTALL.md). The build uses CMake and needs Qt, GDAL and PROJ.
The tests run with `ctest` from the build folder.

`foom-cli --run job.json` runs jobs without the window.


## Documentation

- The user manual is in [`doc/manual/`](doc/manual/).


## Logo

The logo and the wordmark are in [`images/foom-logo/`](images/foom-logo/), as SVG and PNG.
The script `make_logo.py` in that folder writes them and the program icons.


## License

FOOM is licensed under the [GNU General Public License, version 3](COPYING).

FOOM is a modified version of [OpenOrienteering Mapper](https://github.com/OpenOrienteering/mapper),
changed from 2026 on. The copyright notices of its authors stay in the source files, and the
About dialog lists them.
