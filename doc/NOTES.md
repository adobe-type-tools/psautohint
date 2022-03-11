# Python CFF Autohinter

The `python` directory of the `psautohint` package contains a port of the
"Automatic Coloring" code originally written in C by Bill Paxton and before
spring of 2022 distributed in earlier versions of the package with a Python
wrapper.

Most of the algorithms are unchanged but not all. Some notable differences are:

1. Because all code is now in Python there is no role for the `bez` charstring
	 interchange format. CharStrings are now directly unpacked into `glyphData`
	 objects. These are capable of representing any (decompiled) CharString with
	 the caveats that the particular spline operator (e.g. `rCurveTo` vs
	 `curveTo`) is not recorded.
2. Intermediate hint state is now maintained in `hintState` objects, one per
	 glyph, rather than in globals.
3. The C code algorithms for vertical and horizontal hinting were mostly
	 implemented as separate functions. In the port there is typically one
	 function shared by both dimensions. The `glyphData/pt` 2-tuple object has
	 special `a` and `o` value accessors and a class-level switch as part of this
	 unification. Some bug fixes and improvements that were only added to one
	 dimension in the past now work in both.
4. In the C code spline characteristics such as bounding boxes and measures of
	 flatness were calculated by approximating spline curves with line segments.
	 In the Python code these calculations are closed-form and implemented with
	 fontTools' quadratic and cubic root finders.
5. The C code had special functions for handling a spline with an inflection
   point. The new code copies and splits such splines at their inflection points,
	 analyzes the copies, and copies the resulting analysis back to the inflected
	 splines.
6. The mask distribution algorithm (which is equivalent to what used to be
	 called "hint substitution" is implemeneted somewhat differently.

There are also some features that are not (yet) ported:

1. The C code for hint substitution "promoted" hints to earlier splines under
	 some circumstances. This was not included because testing did not indicate
	 it was of noticable benefit in current renderers.
2. Previous versions of psautohint would clean up duplicate `moveTo` operators
	 and warn about duplicate subpaths or unusually large glyphs. It is now more
	 appropriate to check for these characteristics using sanitizers at earlier
	 stages in the development process.
3. Under some circumstances the C autohinter would divide curved splines at *t*
	 == .5 when the ``allow-changes`` option was active. The primary reason for
	 doing so was when a single spline "wanted" conflicting hints at its start
	 and end points. This is a relatively rare circumstance and the Adobe
	 maintainers are evaluating what, if anything, should be done in this case.
4. The C autohinter would add a new hint mask before each subpath in the
	 "percent" and "perthousand" glyphs but did not offer this option for other
	 glyphs. This code was not ported.

The [earlier documentation](historical/AC.mc) for the C code is still helpful
but most functions are now documented in-line. Adapter code in `autohint.py`
calls `hint()` on `glyphHinter` in `hinter.py`, which in turn calls into the
dimension-specific `hhinter` and `vhinter` objects in the same file.

The Python code is slower when hinting individual glyphs, often 5 times as slow
or more compared with the C code on the same machine. It also uses more memory.
However, by default glyphs are now hinted in parallel using the Python
`multiprocessing` module when multiple CPU cores are available. As part of this
change the glyphs are also unpacked just before hinting and updated right after
hinting in order to lower the total memory used by the process at a given time.
As a result the overall hinting process is often slightly faster on most
contemporary machines.
