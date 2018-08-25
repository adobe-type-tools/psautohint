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

PathElt* FindSubpathBBox(PathElt* e);
void FindCurveBBox(Fixed x0, Fixed y0, Fixed px1, Fixed py1, Fixed px2,
                   Fixed py2, Fixed x1, Fixed y1, Fixed* pllx, Fixed* plly,
                   Fixed* purx, Fixed* pury);
void HintVBnds(void);
void HintHBnds(void);
void AddBBoxHV(bool Hflg, bool subs);
void CheckPathBBox(void);
bool CheckBBoxes(PathElt* e1, PathElt* e2);

#endif /* AC_BBOX_H_ */
