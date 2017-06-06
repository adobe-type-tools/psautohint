/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

#ifndef AC_BBOX_H_
#define AC_BBOX_H_

#include "ac.h"

PPathElt FindSubpathBBox(PPathElt e);
void FindCurveBBox(Fixed x0, Fixed y0, Fixed px1, Fixed py1, Fixed px2,
                   Fixed py2, Fixed x1, Fixed y1, Fixed* pllx, Fixed* plly,
                   Fixed* purx, Fixed* pury);
void ClrVBnds(void);
void ReClrVBnds(void);
void ClrHBnds(void);
void ReClrHBnds(void);
void AddBBoxHV(bool Hflg, bool subs);
void ClrBBox(void);
void CheckPathBBox(void);
bool CheckBBoxes(PPathElt e1, PPathElt e2);

#endif /* AC_BBOX_H_ */
