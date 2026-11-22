---
title: Importing Data
keywords: Import, GDAL, DXF, Shapefile, GeoPackage, CRT
parent: Reference
last_modified_date: 21 November 2026
nav_order: 0.55
---

FOOM imports geospatial vector data (every format which [GDAL](gdal.md) reads, such as GeoPackage,
Shapefile, DXF or OSM) through the *Import geodata* dialog.
It opens from **File > Import geodata...**, which takes several files at once,
and from **File > Import...** when the chosen file is such a format.
Map files have a dialog of their own, see [Map files](#map-files) below.

The objects are added to the current map part. The whole import is one step of **Edit > Undo**.


### Layers

The list shows every layer of the files with its kind of geometry and its number of features.
Only the checked layers are imported.
When several files are imported, a layer of a given name is read from every file which has it.


### Coordinate system

**As detected** reads each layer in its own coordinate system and places it by the map's georeferencing.
A layer without a coordinate system is read as the map's projected coordinates.

**Override** reads every layer in the given system, an EPSG code such as `EPSG:32613` or a PROJ
specification, whatever the file says.

**Unreferenced** reads the coordinates as plain numbers.
*1 m per unit, on the ground* makes a unit one metre in the terrain;
*1 mm per unit, on paper* makes it one millimetre on the map;
*Custom* gives the millimetres on the map per unit.
The data are then turned by the rotation (counter-clockwise, in degrees, about the data origin)
and moved by the offset (in millimetres on the map, y upwards).

A map without a coordinate reference system can only take unreferenced data
(see [Georeferencing](georeferencing.md)).


### Options

**Clip** keeps only what lies inside a boundary:
the extent of the map's objects, the area which the map window shows, or a box typed in
the map's projected coordinates (easting and northing of two corners).
Lines are cut at the boundary, areas are intersected with it, and points and texts are kept
when they lie inside.

**Layer name tag** writes the tag `layer_name` on every object: the name of its layer,
or the value of the chosen attribute. [CRT files](crt_files.md) can assign symbols by this tag.

**Text from attribute** adds a text object with the attribute's value at each point feature,
besides the point object.

**Import block references as point objects** is for DXF files: each block insert becomes one point object
tagged with the name of its block, instead of the block's geometry.

**Simplify** removes vertices as long as no removed vertex lies farther from the result than the tolerance,
in millimetres on the map. *Off* keeps every vertex.

**Symbols**: *Default symbols* gives the objects one simple point, line, area and text symbol each;
*CRT file* assigns the map's own symbols by the rules of a [CRT file](crt_files.md).


### The options of the last import

The dialog starts with the options of the last import which was confirmed with **OK**:
the coordinate system choice, the units, offset and rotation, the clip (and the typed box),
the layer name tag, the text attribute, the block option, the simplify tolerance and the symbols.
The layers are not remembered; for new files they are all checked.
An attribute which the new file does not have is not chosen.
**Cancel** keeps the options of the import before.


### Map files

**File > Import...** of a map file (FOOM, OpenOrienteering Mapper or OCD) asks how the symbols and colours
of the file are taken. A symbol of the file *clashes* when the map has a symbol of the same number.
A clashing symbol which is the same in every respect is simply shared.

- **No symbols or colours**: objects take the map's symbol of their number; objects whose number the map
  lacks are left out.
- **Only symbols whose number is absent**: new symbols come with their colours; objects of a clashing
  symbol take the map's symbol.
- **Absent symbols, renumbering on a clash**: a clashing symbol which differs is added under a new number
  below its own, such as 102.1 for 102.
- **All symbols and colours, replacing on a clash**: every symbol and colour of the file; a clashing symbol
  which differs replaces the map's, also for the map's own objects.

**New colours** go above the map's colours (drawn on top) or below them.
**Protect the imported symbols** marks them protected.
When the file has another scale, **Scale** brings its data to the map's scale.

The objects are placed **by georeferencing**, **by offset** (in millimetres on the map),
or **by mouse**: at the centre of the view, selected, so that they can be dragged into place.
The dialog remembers the choices of symbols, colours and protection.
