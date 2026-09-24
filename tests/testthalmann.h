// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
#ifndef TESTTHALMANN_H
#define TESTTHALMANN_H

#include "testbase.h"

class TestThalmann : public TestBase {
	Q_OBJECT
private slots:
	void testCcrHeO2();
	void testCcrHeO2Deep();
	void testCcr200fsw40min();
	void testLiveRecomputeAfterExtraDwell();
	void testLastStopCeilingIsSetM();
	void testOpenCircuitAir();
	void testOpenCircuitAirO2();
	void testSurfacingGfWarning();
	void testExceptionalExposureAir60();
	void testExceptionalExposureAir70();
	void testExceptionalExposureAirO2_70();
	void testExceptionalExposureAirO2_100();
	void testExceptionalExposureDepth();
	void testExceptionalExposureIgnoresGasModWaypoint();
	void testBvm3NoStop60();
	void testBvm3NotesOnVval79();
	void testBvm3VsAppendixE();
	void testBvm3Repetitive();
};

#endif // TESTTHALMANN_H
