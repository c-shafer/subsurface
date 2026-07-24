// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
#ifndef TESTLEMSCORE_H
#define TESTLEMSCORE_H

#include "testbase.h"

class TestLemScore : public TestBase {
	Q_OBJECT
private slots:
	void testOpenCircuitAgainstReference();
	void testDecoReducesRisk();
	void testCcrSetpoint();
	void testHeO2DecoReducesRisk();
};

#endif // TESTLEMSCORE_H
