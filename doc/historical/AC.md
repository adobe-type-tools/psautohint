# AC - Automatic Coloring (Hinting)

## Background

AC was written by Bill Paxton. Originally, it
was integrated with the font editor, FE, but Bill extracted the
hinting code so it could run independently and would be easier to
maintain.

## Input

AC reads a glyph outline in bez format. The fontinfo
data is also read to get alignment zone information, the list of H,V counter
glyphs, the list of auxiliary H,V stem values, and whether or not flex
can be added to a glyph.

As the bez data is read a doubly linked-list is created that contains the
path element information, e.g. coordinates, path type (moveto, curveto...),
etc.

## Setup

The following initial setup and error checking is done after a glyph
is read:

1. Calculate the glyph bounding box to find the minimum and maximum
   x, y values. Supposedly, the very minimum hinting a glyph will get
   is its bounding box values.

2. Check for duplicate subpaths.

3. Check for consecutive movetos and only keep the last one.

4. Check that the path ends with a single closepath and that there are
   matching movetos and closepaths.

5. Initialize the value of the largest vertical and horizontal stem
   value allowed. The largest vertical stem value is the larger of 86.25
   and the largest value in the auxiliary V stem array. The largest
   horizontal stem value is the larger of 86.25 and the largest value in
   the auxiliary H stem array.

6. If flex is allowed add flex to the glyph. The current flex
   algorithm is very lax and flex can occur where you least expect it.
   Almost anything that conforms to page 72 of the black book is flexed.
   However, the last line on page 72 says the flex height must be 20 units
   or less and this should really say 10 units or less.

7. Check for smooth curves. If the direction arms tangent to the curve
   is between 0 and 30 degrees the points are forced to be colinear.

8. If there is a sharp angle, greater than 140 degrees, the angle will
   be blunted by adding another point. There’s a comment that says as of
   version 2.21 this blunting will not occur.

9. Count and save number of subpaths for each path element.

## Hinting

Generate possible hstem and vstem values. These values are saved as a
linked list (the coloring segment list) in the path element data structure.
There are four segment lists, one for top, bottom, left, and right segments.
The basic progression is: path → segments → values → hints, where a
segment is a horizontal or vertical feature, a value is a pair of
segments (top and bottom or left and right) that is assigned a priority
based on the length and width of a stem, and hints are non-overlapping
pairs with the highest priority.

### Generating {H,V}Stems

The path element is traversed and possible coordinates are added to the
segment list. The x or y coordinate is saved depending on if it’s a
top/bottom or left/right segment and the minimum and maximum extent for a
vertical or horizontal segment. These coordinates are included in the list:
a) the coordinates of horizontal/vertical lines, b) if this is a curve find the
bends (bend angle must be 135 degrees or less to be included);
don’t add a curve’s coordinate if the point is not at an extreme and is
going in the same direction as the previous path, c) add points at
extremes, d) add bands for s-curves or where an inflection point occurs,
e) checks are made to see if the curve is in a blue zone and a coordinate
added at the appropriate place.

Compact the coloring segment lists by removing any pairs that are
completely contained in another. Filter out bogus bend segments and
report any near misses to the horizontal alignment zones.

### Evaluating Stems

Form all top and bottom, left and right pairs from segment list and
generate ghost pairs for horizontal segments in alignment zones.
A priority value is assigned to each pair. The value is a function
of the length of the segments, the length of any overlap, and the
distance apart. A higher priority value means it is a likely candidate
to be included in the hints and this is given to pairs that are closer
together, longer in length, and have a clean overlap. All pairs with
0 priority are discarded.

Report any near misses (1 or 2 units) to the values in the H or V stems
array.

### Pruning Values

Prune non-relevant stem pairs and keep the best of any overlapping pairs.
This is done by looking at the priority values.

### Finding the Best Values

After pruning, the best pair is found for each top, bottom or left, right
segment using the priority values. Pairs that are at the same location
and are “similar” enough are merged by replacing the lower priority pair
with the higher priority pair.

The pairs are again checked for near misses (1 or 2 units) to the
values in the H or V stems array, but this time the information is
saved in an array. If fixing these pairs is allowed the pairs saved
in the array are changed to match the value it was “close” to in the
H or V stem array.

Check to see if H or V counter hints (hstem3, vstem3) should be used
instead of hstem or vstem.

Create the main hints that are included at the beginning of a glyph.
These are created by picking, in order of priority, from the segment
lists.

If no good hints are found use bounding box hints.

## Shuffling Subpaths

The glyph’s subpaths are reordered so that the hints will not need
to change constantly because it is jumping from one subpath to another.
Kanji glyphs had the most problems with this which caused huge
files to be created.

## Hint Substitution

Remove “flares” from the segment list. Flares usually occur at the top
of a serif where a hint is added at an endpoint, but it’s not at the
extreme and there is another endpoint very nearby. This causes a blip
at the top of the serif at low resolutions. Flares are not removed if
they are in an overshoot band.

Copy hints to earlier elements in the path, if possible, so hint
substitution can start sooner.

Don’t allow hint substitution at short elements, so remove any if
they exist. Short is considered less than 6 units.

Go through path element looking for pairs. If this pair is compatible
with the currently active hints then add it otherwise start hint
substitution.

## Special Cases

When generating stems a procedure is called to allow lines that are not
completely horizontal or vertical to be included in the coloring
segment list. This was specifically included because the serifs of
ITCGaramond Ultra have points that are not quite horizontal according
to the comment int the program. When generating hstem values the threshold
is ≤ 2 for delta y and ≥ 25 for delta x. The reverse is true when
generating vstem values.

There are many thresholds used when evaluating stems, pruning, and
finding the best values. The thresholds used throughout the program are
just “best guesses” according to Bill Paxton. There are various comments
in the code explaining why some of these thresholds were changed and
specifically for which fonts.

The following glyphs attempt to have H counter hints added.

> "element", "equivalence", "notelement", "divide"

in addition to any glyphs listed in the HCounterChars keyword of
the fontinfo file.

The following glyphs attempt to have V counter hints added.

> "m", "M", "T", "ellipsis"

in addition to any glyphs listed in the VCounterChars keyword of
the fontinfo file.

There used to be a special set of glyphs that received dotsection
hints. This was to handle hinting on older PS interpreters that could
not perform hint replacement. This feature has been commented out of
the current version of AC.

AC uses a 24.8 Fixed type number rather than the more widely used 16.16.

## Output

AC writes out glyphs bez format that includes
the hinting information. Along with each hint it writes a comment
that specifies which path element was used to create this hint.
This comment is used by BuildFont for hinting multiple master
fonts.

## Platforms

AC should run on Unix and Unix-like operating systems, Mac OS X and Microsoft
Windows, both 32-bit and 64-bit. The code is written in portable C.

AC consists of one header file and about 28 C files.

## Related comment from Blues() in control.c:

    Top alignment zones are in the global 'gTopBands', bottom in
    'gBotBands'.

    This function looks through the path, as defined by the linked list of
    PathElt's, starting at the global 'gPathStart', and adds to several
    lists.  Coordinates are stored in the PathElt.(x,y) as (original
    value)/2.0, aka right shifted by 1 bit from the original 24.8 Fixed. I
    suspect that is to allow a larger integer portion - when this program
    was written, an int was 16 bits.

    'gHStems' and 'gVStems' are global arrays of Fixed 24.8 numbers..

    'gSegLists' is an array of 4 HintSeg linked lists. List 0 and 1 are
    respectively up and down vertical segments. Lists 2 and 3 are
    respectively left pointing and right pointing horizontal segments. On a
    counter-clockwise path, this is the same as selecting top and bottom
    stem locations.

    NoBlueGlyph() consults a hard-coded list of glyph names, if the glyph is
    in this list, set the alignment zones ('gTopBands' and 'gBotBands') to
    empty.

    1) gen.c:GenHPts()
       Builds the raw list of stem segments in global
       'topList' and 'botList'. It steps through the liked list of path
       segments, starting at 'gPathStart'. It decides if a path is mostly H,
       and if so, adds it to a linked list of hstem candidates in gSegLists,
       by calling gen.c:AddHSegment(). This calls ReportAddHSeg() (useful in
       debugging), and then gen.c:AddSegment().

       If the path segment is in fact entirely vertical and is followed by a
       sharp bend, gen.c:GenHPts() adds two new path segments just 1 unit
       long, after the segment end point, called H/VBends (segment type
       sBend=1). I have no idea what these are for.

       AddSegment() is pretty simple. It creates a new hint segment
       (HintSeg) for the parent PathElt, fills it in, adds it to appropriate
       list of the 4 gSegLists, and then sorts by hstem location. seg->sElt
       is the parent PathElt, seg->sType is the type, seg->sLoc is the
       location in Fixed 18.14: right shift 7 to get integer value.

       If the current PathElt is a Closepath, It also calls LinkSegment() to
       add the current stem segment to the list of stem segments referenced
       by this elt's e->Hs/Vs.

       Note that a hint segment is created for each nearly vertical or
       horizontal PathElt. This means that in an H, there will be two hint
       segments created for the bottom and top of the H, as there are two
       horizontal paths with the same Y at the top and bottom of the H.

       Assign the top and bottom Hstem location lists.
       topList = segLists[2]
       botList = segLists[3];

    2) eval.c::EvalH()
       Evaluates every combination of botList and topList, and assign a
       priority value and a 'Q' value.

       For each bottom stem
       for each top stem
       1) assign priority (spc) and weight (val) values with EvalHPair()
       2) report stem near misses  in the 'HStems' list with HStemMiss()
       3) decide whether to add pair to 'HStems' list with AddHValue()

        Add ghost hints.
        For each bottom stem segment and then for each top stem segment:
        if it is in an alignment zone, make a ghost hint segment and add it
        with AddHValue().

        EvalHPair() sets priority (spc) and weight (val) values.
          Omit pair by setting value to 0 if:
            bottom is in bottom alignment zone, and top is in top alignment
            zone. (otherwise, these will override the ghost hints).

          Boost priority by +2 if either the bot or top segment is in an
          alignment zone.

          dy = stem width ( top - bot)

          Calculate dist. Dist is set to a fudge factor * dy.
            if bottom segment xo->x1 overlaps top x0->x1, the fudge factor is
            1.0. The less the overlap, the larger the fduge factor.
            if bottom segment xo->x1 overlaps top x0->x1:.
              if  top and bottom overlap exactly, dist = dy
              if they barely overlap, dist = 1.4*dy
              in between, interpolate.
            else, look at closest ends betwen bottom and top segments.
              dx = min X separation between top and bottom segments.
              dist = 1.4 *dy
              dist += dx*dx
              if dx > dy:
                dist *= dx / dy;

          Look through the gHStems global list. For each match to dy, boost
          priority by +1.

          Calculate weight with gen.c:AdjustVal()
            if dy is more than twice the 1.1.5* the largest hint in gHStems,
            set weight to 0.
            Calculate weight as related to length of the segments squared
            divided by the distance squared.
            Basically, the greater the ratio segment overlap to stem width,
            the higher the value.
            if dy is greater than the largest stem hint in gHStems, decrease
            the value scale weight by  of * (largest stem hint in
            gHStems)/dy)**3.

        AddHValue() decides whether add a (bottom, top)  pair of hint segments.
        Do not add the pair if:
        if weight (val) is 0,
        if both are sBEND segments
        if neither are a ghost hint, and weight <= pruneD and priority (spc)
        is <= 0:
        if either is an sBEND: skip
        if the BBox for one segment is the same or inside the BBox for the
        other: skip

        else add it with eval.c:InsertHValue()
        add new HintVal to global valList.
        item->vVal = val; # weight
        item->initVal = val; # originl weight from EvalHPair()
        item->vSpc = spc; # priority
        item->vLoc1 = bot; # bottom Y value in Fixed 18.14
        item->vLoc2 = top; # top Y value in Fixed 18.14
        item->vSeg1 = bSeg; # bottom hint segment
        item->vSeg2 = tSeg; # top hint segment
        item->vGhst = ghst; # if it is a ghost segment.
        The new item is inserted after the first element where vlist->vLoc2 >= top
       and vlist->vLoc1 >= bottom

    3) merge.c:PruneHVals();

       item2 in the list knocks out item1 if:
       1) (item2 val is more than 3* greater than item1 val) and
          (val 1 is less than FixedInt(100)) and
          (item2 top and bottom is within item 1 top and bottom) and
          (if val1 is more than 50* less than val2 and either top
           seg1 is close to top seg 2, or bottom seg1 is close to
           bottom seg 2) and
          (val 1 < FixInt(16)) or
          ((item1 top not in blue zone, or top1 = top2) and
           (item1 bottom not in blue zone, or top1 = bottom2))
       "Close to" for the bottom segment means you can get to the bottom elt for
       item 2 from bottom elt for 1 within the same path, by
        stepping either forward or back from item 1's elt, and without going
       outside the bounds between
        location 1 and location 2. Same for top segments.

    4) pick.c:FindBestHVals();
        When a hint segment
