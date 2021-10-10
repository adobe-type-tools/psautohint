# Python CFF Autohinter

The `python` directory of the `psautohint` package contains a port of the
"Automatic Coloring" code originally written in C by Bill Paxton and, until
2021, distributed in earlier versions of the package with a Python wrapper.

Most of the algorithms are unchanged but not all. Some notable differences are:

1. Because all code is now in Python there is no role for the `bez` charstring
	 interchange format. CharStrings are now directly unpacked into `glyphData`
	 objects. These are capable of any CharString with the caveats that the 
	 particular spline operator (e.g. `rCurveTo` vs `curveTo`) is not recorded
	 and strings must be "decompiled". 
2. In place of globals, intermediate hint state is stored in `hintState` 
   objects, with one per glyph.
3. The C code algorithms for vertical and horizontal hinting were mostly 
   implemented as separate functions. In the port there is typically one 
	 function used for both dimensions. The `glyphData/pt` 2-tuple object has 
	 special `a` and `o` value accessors and a class-level switch as part 
	 of this unification. Some improvements only added to one dimension in the C
	 code now work in both.
4. In the C code spline characteristics such as bounding boxes and measures of
	 flatness were calculated by approximating spline curves with line segments.
	 In the Python code these calculations are closed-form, using fontTools'
	 quadratic and cubic root finders.
5. The mask distribution algorithm (which is equivalent to what used to be
	 called "hint substitution" is implemeneted somewhat differently. 

There are also some features that are not (yet) ported:

1. The C code for hint substitution "promoted" hints to earlier splines under
	 some circumstances. This was not included because testing did not indicate
	 it was of noticable benefit in current renderers.
2. Previous versions of psautohint would clean up duplicate `moveTo` operators 
   and warn about duplicate subpaths or unusually large glyphs. Checking 
	 these characteristics at hint-time made more sense when the code was 
	 written but is now better handled at earlier stages in the development
	 process. 
3. The port does not currently add a segment for a flat area on a spline with
	 inflection points. Some variant of this feature will likely be added to
	 a future version of the autohinter after some research about better options.
4. Under some circumstances the C autohinter would divide curved splines at *t*
	 == .5 when the ``allow-changes`` option was active. The primary reason for
	 doing so was when a single spline "wanted" conflicting hints at its start
	 and end points. This is a relatively rare circumstance and the Adobe 
	 maintainers are evaluating what, if anything, should be done in this case.
5. The C autohinter would add a new hint mask before each subpath in the 
   "percent" and "perthousand" glyphs but did not offer this option for 
	 other glyphs. This code was not ported.

The [earlier documentation](historical/AC.mc) for the C code is still helpful
but most functions are now documented. The ported code starts with the
`autohint.py` `hintAdapter` object and its `hint()` method. This calls down
into the dimension-specific `hinter.py` `hinter` objects and their `addFlex()`
and `calcHintValues()` methods, which is where most of the work is done. Then
if there are conflicting hints the `hintAdapter` distributes masks among
the splines.

The python code is slower, often around 5 times as slow as the C code on the
same machine. It also uses more memory. Developers of particularly large fonts
may benefit from hinting their glyphs in batches. 
