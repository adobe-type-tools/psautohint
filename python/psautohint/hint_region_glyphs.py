# Copyright 2014 Adobe. All rights reserved.

"""
Utilities for converting between T2 charstrings and the bez data format.
"""

import copy
import logging
import os
import re
import subprocess
import tempfile
import itertools

from . import fdTools, FontParseError

log = logging.getLogger(__name__)

notes = """For each glyph in the default font
    
        # Collect hint and path data.
        Get the generalized (no optimizations) T2 charstring from the default font
        Produce a hint object for each hint. This object will contain a list of pairs of path segment indices, where each pair references the two path segments which define the top and bottom edges of the hint. There may be more than one such pair. Build up the lists as we step through the path segments. For each path segment, record which hint catches the segment. 
        For each region font, get the generalized (no optimizations) T2 charstring.
            Step through the Hint objects, and derive a hint from each pair of path segments referenced hint list.
            Step through the path segments, and record which hint catches the segment.
    
        # fix hints.	
        Remove duplicate hints. For each hint object, remove it if the hints are the same as the previous hint object in all the fonts, and update the path segments to reference the previous hint.
        Remove  hints with an order problem. The CFF spec requires all hints in  the initial hint list of the charstring to be sorted in increasing order of the bottom edge. If any hint breaks this rule in a region font, we have to remove it from all fonts.
    
        # Build a new hint group
        Create a new hint group.
        Step through the path indices. For each index, step through the path segments from each font. If any path segment is caught by a hint, try and add it to the current hint group. Do not add if it is a duplicate of a hint already there. If is overlaps with a hint already there in any font, then start a new hint group.
        
        # Build new charstring.programs.
        Reduce the path segment list to charstring.program.
"""


class PathVH:
    def __init__(self, path_ops):
        self.p0 = None
        self.area = 0
        self.path_ops = []
        self.clockwise = False
        if path_ops:
            self.assignVH(path_ops)

    def __bool__(self):
        return True if self.path_ops else False



    @staticmethod
    def  get_line_area(p0, p1):
        value = -((p1[0] - p0[0]) * (p1[1] + p0[1]) * .5)
        return value

    @staticmethod
    def get_curve_area(p0, p1, p2, p3):
        x0, y0 = p0[0], p0[1]
        x1, y1 = p1[0] - x0, p1[1] - y0
        x2, y2 = p2[0] - x0, p2[1] - y0
        x3, y3 = p3[0] - x0, p3[1] - y0
        value = -((x1 * (-y2 - y3) +
                   x2 * (y1 - 2 * y3) +
                   x3 * (y1 + 2 * y2)
                  ) * 0.15)
        return value

    def assignVH(self, path_ops):
        # Require that initial path op is moveto. Store is start point p0
        # Step through ops.
        # When we try and match path ops to hints, I will declare a match
        # if the op has endpoint whose x or y matches a hint, and the
        # incoming or outgoing bezier control point (bcp)  is mostly
        # horizontal for an h hint, or vertical for a v hint. An incoming
        # bcp is the last two points in a curve, or the previous op end
        # point and the current op end point for a line-to. Likewise for
        # the outgoing bcp for the current endpoint.
        # Here, we are just recording whether an op endpoint has a mostly
        # horizontal incoming or outgoing bcp, and saving the bcp coordinates.
        # For a series of line-to's, the incoming bcp for the current op
        # endpoint is the same point pair as the outgoing bcp for their prior
        # op endpoint, so we check for this.
        self.area = 0
        last_mt = prev_op = None
        i = 0
        if path_ops[0].op != "mt":
            print("First path op is not mt")
        assert path_ops[0].op == "mt", "First path op is not mt"
        for path_op in path_ops:
            if path_op.op == "mt":
                if last_mt is None:
                    # This is the first move-to - start of path.
                    last_mt = path_op
                    p0 = path_op.coords
                else:
                    # we are ending the path
                    break

            elif path_op.op == "dt":
                path_op.set_bcps(prev_op) # sets v, h, up.
                p1 = path_op.coords
                self.area += self.get_line_area(p0, p1)
                p0 = p1

            elif path_op.op == "ct":
                path_op.set_bcps(prev_op)
                p1 = path_op.coords[0:2]
                p2 = path_op.coords[2:4]
                p3 = path_op.coords[-2:]
                self.area += self.get_curve_area(p0, p1, p2, p3)
                p0 = p3
            else:
                assert 0, "Un-handled path op"+ path_op.op
            prev_op = path_op
            i += 1

        self.path_ops = path_ops[:i]
        return path_ops[path_op.idx:]

    @property
    def last_idx(self):
        return self.path_ops[-1].idx if self.path_ops else 0


class HintMap:
    #             is_outer, is_v, is_up   -> match_high
    match_high_dict = {(False, False, False): False,
                 (True, False, False): True,
                 (False, True, False): True,
                 (True, True, False): False,
                 (False, False, True): True,
                 (True, False, True): False,
                 (False, True, True): False,
                 (True, True, True): True,
                 }
    def __init__(self, glyph_name):
        self.glyph_name = glyph_name
        self.hint_list = []
        self.path_ops = []
        self.hhints = []
        self.vhints = []

    def add_default_path_data(self, bez_data):
        path_ops, hhints, vhints = parse_bez(bez_data)
        self.path_ops = path_ops
        self.hhints.append(hhints)
        self.vhints.append(vhints)

        # Assign V,H and clockwise attributes to path ops
        self.paths = []
        path = PathVH(path_ops)
        first_idx = 0
        while path:
            self.paths.append(path)
            path = PathVH(path_ops[path.last_idx+1:])

    def add_region_path_data(self, bez_data):
        path_ops, hhints, vhints = parse_bez(bez_data)
        assert 0, "Not Yet Implemented"

    def find_hint_segments(self):
        """ Find mapping of segment pairs to stem hints in the default paths.
        for each path:
            get path orientation
            for each path op:
                get roughly h or v and direction
                The end point defines a segment if either:
                1) the following out bcp or line is roughly v or h, or
                2) the incoming bcp is roughly v or h.
                var theta = Math.atan2(dy, dx); // range (-PI, PI]
                 theta *= 180 / Math.PI; // rads to degs, range (-180, 180]

                Based on direction, choose vhint list, and low or high edge,
                If not h or v, continue.

                for each hint in hint list:
                    based on v or h segment status and direction, check if
                    end point matches hint. If so, add it to low or high
                    segment lists for this hint.
         """

        def match_hints(hints, paths, is_v=False):
            for hint in hints:
                found_seg = False
                for path in paths:
                    is_outer = not path.clockwise
                    for path_op in path.path_ops:
                        # print(f" path endp: {path_op.endpoint}")
                        for bcp in [path_op.in_bcp, path_op.out_bcp]:
                            if bcp and ((is_v and bcp.v) or ((not is_v) and
                                                              bcp.h)):
                                match_high = self.match_high_dict[(is_outer,
                                                                   is_v, bcp.up)]
                                ep = path_op.endpoint

                                coord = ep[0] if is_v else ep[1]
                                if match_high:
                                    if coord == hint.high:
                                        if not bcp in hint.low_ops:
                                            hint.high_ops.append(bcp)
                                            found_seg = True
                                elif coord == hint.low:
                                    if not bcp in hint.low_ops:
                                        hint.low_ops.append(bcp)
                                        found_seg = True
            if  not found_seg:
                print(f"{self.glyph_name}: failed to find path segment "
                      f"defining {'v' if is_v else 'h'} hint {hint}")

        match_hints(self.hhints[0], self.paths)
        match_hints(self.vhints[0], self.paths, is_v=True)
        return

    def find_matching_segs(self, is_vert, hint, dflt_ops, low=True):
        """ A path_op matches a hint coord if either the end point equals the
        hint value or the prior path_op end point equals to the hint value,
        and the path_op is reasonably parallel to to the hint direction.
        """
        def match_vstem(coord, hint_val):
            return hint_val == coord[1]

        def match_hstem(coord, hint_val):
            return hint_val == coord[0]

        match_func = match_vstem if is_vert else match_hstem

        seg_list = []
        hint_val = hint.low if low else hint.high
        for i, path_op in enumerate(dflt_ops[1:]):
            # Is there a point in the segment which matches?
            seg = [dflt_ops[i].coords[-1]] + path_op.coords
            for coord in seg:
                found_match =  match_func(coord, hint_val)
                if found_match:
                    break
            if not found_match:
                continue
            # path op could be a match. Now check if it is mostly parallel to
            # the hint direction.
        return seg_list

    def is_stem(is_vert, low_seg, high_seg):
        return True

class BezierControlPointVH:
    def __init__(self, p0, p1):
        """ Set whether bcp main direction is vertical or horizontal,
        and whether p1 > p0 along main direction. """
        self.p0 = p0
        self.p1 = p1
        self.h = self.v = False
        if self.is_h(p0, p1):
            self.h = True
            self.up = p1[0] > p0[0]
        elif self.is_v(p0, p1):
            self.v = True
            self.up = p1[1] > p0[1]
        if self:
            print(f"Setting bcp {p0, p1} h:{self.h} v:{self.v} up: {self.h}")

    def __bool__(self):
        """ bcp is not of interest unless it is mostly v or h;
        otherwise, it cannot participate in defining a hint stem edge."""
        return True if self.h or self.v else False

    def is_h(self, p0, p1):
        return self.is_vh(p0, p1)

    def is_v(self, p0, p1):
        return self.is_vh(p0, p1, check_vert=True)

    @staticmethod
    def is_vh(p0, p1, check_vert=False):
        # line is mostly H or V if slope < 0.1.
        result = False
        if check_vert:
            y0, x0 = p0
            y1, x1 = p1
        else:
            x0, y0 = p0
            x1, y1 = p1
        if (x0 == x1):
            result = True
        elif abs((y1 - y0) / (x1 - x0)) < 0.1:
            result = True
        return result

class PathOp:
    """ Holds end point of each path op, and the ingoing and outgoing bcp's
    that might define a stem edge. """
    def __init__(self, op, coords, idx):
        self.op = op
        self.coords = coords
        self.idx = idx  # index into entire HintMap.path_ops list.
        self.endpoint = coords[-2:]
        self.in_bcp = None
        self.out_bcp = None


    def set_bcps(self, prev_op):
        if self.op == "dt":
            p0 = prev_op.coords[-2:]
            bcp = BezierControlPointVH(p0, self.coords)
            if bcp:
                self.bcp_in = bcp
                prev_op.out_bcp = bcp
           if not (bcp or prev_op.in_bcp):
                self.check_extremum(prev_op):

        elif self.op == "ct":
            in_bcp = BezierControlPointVH(self.coords[2:4], self.coords[4:])
            if in_bcp:
                self.bcp_in = in_bcp
            if not (bcp or prev_op.in_bcp):
                self.check_extremum(prev_op):

            out_bcp = BezierControlPointVH(prev_op.coords[-2:], self.coords[
            if out_bcp:
                p0prev_op.out_bcp = out_bcp

    def set_extremum(self, prev_op):
        """ If the previous endpoint was an extremum - both control points
        point to one side of the V or H - then mark it as v or h.
        """
        p0 = prev_op.in_bcp.p0
        p1 = prev_op.in_bcp.p1
        # prev_op.in_bcp.p1 == self.out_bcp.p0, by current logic.
        p2 = self.out_bcp.p1
        dx = p2[0] - p0[0]]
        dy = p2[1] - p0[2]]
        if abs(dx) >abs(dy):
            # mostly horizontal feature feature
            if 


    def __str__(self):
        return  f"op {self.idx} coords {self.coords} {self.op} "

class HintOp:
    top_edge = -20
    bottom_edge = -21
    def __init__(self, coords):
        self.coords = coords
        self.low = coords[0]
        self.edge_hint = None
        self.high_ops = []
        self.low_ops = []
        high = coords[1]
        if high == self.top_edge:
            self.high = self.low
            self.low = None
            self.edge_hint = self.top_edge
        elif high == self.bottom_edge:
            self.low = self.low +self. bottom_edge
            self.high = None
            self.edge_hint = self.bottom_edge
        else:
            self.high = self.low + high

    def __eq__(self, other):
        return self.coords == other.coords

    def __str__(self):
        return  f"stem [{self.low}, {self.high}]"


def parse_bez(bez_data):
    path_ops = []
    hhints = []
    vhints = []
    bez_data = re.sub(r"%.+?\n", "", bez_data)  # suppress comments
    tokens = bez_data.split()
    new_coords = []
    idx = 0
    for token in tokens:
        try:
            val1 = round(float(token), 2)
            val2 = int(val1)
            if int(val1) == val2:
                new_coords.append(val2)
            else:
                new_coords.append(val1)
            continue
        except ValueError:
            # token is an operator.
            coords = new_coords
            new_coords = []
            assert len(coords) in [0,2,6], "Bad operator values"
            pass
        last_mt = None
        if token in ["rb", "rv"]:
            hint_op = HintOp(coords)
            if not hint_op in hhints:
                hhints.append(hint_op)
        elif token in ["ry", "rm"]:
            hint_op = HintOp(coords)
            if not hint_op in vhints:
                vhints.append(hint_op)
        elif token == "mt":
            if last_mt and (prev_coords[-2:] != last_mt):
                # if last path is not closed, close it with a 'dt'.
                path_ops.append(PathOp("dt", last_mt, idx))
                idx += 1
            path_ops.append(PathOp(token, coords, idx))
            prev_coords = coords
            idx += 1
            last_mt = coords
        elif coords: # skip all non-marking ops, like sc, ed.
            path_ops.append(PathOp(token, coords, idx))
            prev_coords = coords
            idx += 1
    return path_ops, hhints, vhints


def get_hint_map(glyph_name, default_bez_data):
    """For each stem hint value pair, we want to find the path segment pair
    that defines it. When two path segment pairs  define the same
    stem hint, we add one stem hint:segment pair for each segment pair. This is
    because two path segment pairs that define the same stem hint in one
    font may define a different stem hint in another font.

    We do not keep track of hint groups, as these will differ between the
    source fonts, and need to be recalculated in the end after any bad hints
    are removed.

    Parse bez into a list of path segments [op,[values]] and a list of hint
    values - hhints and vhints.

    Find mapping of segment pairs to stem hints.
    """
    hint_map = HintMap(glyph_name)
    hint_map.add_default_path_data(default_bez_data)
    hint_map.find_hint_segments()
    return hint_map


def build_hint_groups(hint_map):
    """Build hint groups as needed to avoid overlapping hints.
    Establish an initial empty hint group
    Build a list of conflicting stem hints, and a list of all the other hints.
    Stem hints conflict if they conflict in any source font.
    Each conflicting stem hint entry contains a list of stem hint indices
    for stem hints with which it conflicts.
    If there are no conflicting hints, return.
    sort stem hints by the path segment referenced.
    for each stem hint in turn in the conflicting hint list:
        create a new hint group and attach it to te4h path segment
        fill the hint group with the current stem hint, and all
        non-conflicting hints.
        For the first hint group, attach it to the start of the charstring.
    """
    hint_map.hint_groups = []

def remove_bad_hints(hint_map):
    """ Remove  duplicate hints, and hints that cannot be sorted by the
     Type 2 spec.
    """
    hint_map.hint_groups = []

def fixup_hints(hint_map):
    """We now need to assign hint groups and remove duplicate hints, and hints
    that cannot be sorted by the Type 2 spec.
    """
    build_hint_groups(hint_map)
    remove_bad_hints(hint_map)


def get_hinted_bez_data(hint_map):
    hinted = []
    return hinted
