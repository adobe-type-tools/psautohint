/*
 * Copyright 2014 Adobe Systems Incorporated (http://www.adobe.com/).
 * All Rights Reserved.
 *
 * This software is licensed as OpenSource, under the Apache License, Version
 * 2.0.
 * This license is available at: http://opensource.org/licenses/Apache-2.0.
 */

/* procedures in AC called from buildfont */

#ifndef BFTOAC_H
#define BFTOAC_H

#include "ac.h"

void FindCurveBBox(Fixed x0, Fixed y0, Fixed px1, Fixed py1, Fixed px2,
                   Fixed py2, Fixed x1, Fixed y1, Fixed* pllx, Fixed* plly,
                   Fixed* purx, Fixed* pury);

#endif /*BFTOAC_H*/
