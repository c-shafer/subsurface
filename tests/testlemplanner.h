// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
#ifndef TESTLEMPLANNER_H
#define TESTLEMPLANNER_H

#include "testbase.h"

class TestLemPlanner : public TestBase {
	Q_OBJECT
private slots:
	void testScheduleMeetsTargetHe8n25();
	void testScheduleMeetsTargetHeN2_2016();
	void testLowerTargetGivesMoreDeco();
	void testNoDecoWhenTargetAlreadyMet();
	void testNonMonotonicRiskTerminates();
};

#endif // TESTLEMPLANNER_H
