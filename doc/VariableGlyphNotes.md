# Notes on hinting glyphs in a variable font

## Stem ordering

The initial CFF2 specification, and all revisions at the time I write this,
require that stems are defined in increasing order by their (lower) starting
locations and then, if two stems start at the same place, their ending
locations (which are higher except in the case of ghost hints).  Duplicate
stems are disallowed.  Stems that would be specified out of order in a
particular master (relative to the default master ordering) must therefore be
removed.

As we hope to eliminate these restrictions at a future point the variable font
autohinter supports two modes: a default mode in which stems are removed until
all are in order across all masters and an experimental mode in which stems
that change order are retained but treated as conflicting with all stems they
"cross" anywhere in design space. 

As long as a design space is defined by interpolation only (rather than extrapolation)
the extremes of stem ordering are represented by the (sorted) orders in the individual
masters. Consider a variable glyph with *n* masters. The bottom edge of stem *i* at 
some point in design space can be represented as

c<sub>1</sub>\*s<sub>*i*1</sub> + c<sub>2</sub>\*s<sub>*i*2</sub> + ... + c<sub>*n*</sub>\*s<sub>*in*</sub>

where c<sub>1</sub> + c<sub>2</sub> + ... + c<sub>*n*</sub> == 1 and 0 <= c<sub>*k*</sub> <= 1

The signed distance between the bottom edges of two stems *i* and *j* is accordingly

c<sub>1</sub>\*s<sub>*i*1</sub> + c<sub>2</sub>\*s<sub>*i*2</sub> + ... + c<sub>*n*</sub>\*s<sub>*in*</sub> - c<sub>1</sub>\*s<sub>*j*1</sub> + c<sub>2</sub>\*s<sub>*j*2</sub> + ... + c<sub>*n*</sub>\*s<sub>*jn*</sub>

or

c<sub>1</sub>\*(s<sub>*i*1</sub> - s<sub>*j*1</sub>) + c<sub>2</sub>\*(s<sub>*i*2</sub> - s<sub>*j*2</sub>) + ... + c<sub>*n*</sub>\*(s<sub>*in*</sub> - s<sub>*jn*</sub>)

The minimum/maximum distance in the space will therefore be *s*<sub>*ik*</sub>
- *s*<sub>*jk*</sub> for whatever master *k* minimizes/maximizes this value.
The question of whether *i* and *j* change order anywhere in design space is
therefore equivalent to the question of whether *i* and *j* change order in any
master, which is further equivalent to the question of whether they appear in a
different order in any master besides the default.

## Stem overlap

Suppose that stems *i* and *j* are in the same order across all masters with
stem *i* before stem *j* (so either s<sub>*ik*</sub> < s<sub>*jk*</sub> or
(s<sub>*ik*</sub> == s<sub>*jk*</sub> and e<sub>*ik*</sub> <
e<sub>*jk*</sub>)). Whether *i* overlaps with *j* in a given master *k* is
defined by whether s<sub>*jk*</sub> - e<sub>*ik*</sub> < O<sub>m</sub> (the
overlap margin). Therefore, by reasoning analogous to the above, the question
of whether two consistently ordered stems overlap in design space is equivalent
to whether they overlap in at least one master.

Any two stems that change order between two masters overlap at some point in design
space interpolated between those masters. The question of whether two stems overlap
in the general is therefore equivalent to:

1. Do the stems change order in any master relative to the default master? If yes,
   then they overlap.
2. If no, then is there any master *k* in which s<sub>*jk*</sub> -
	 e<sub>*ik*</sub> < O<sub>m</sub>? If yes, then they overlap.
