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
	void testBackgasBreaksDoNotHang();
	void testSalinityAndAtmAffectSchedule();
	void testSurfacingGfWarning();
	void testExceptionalExposureAir60();
	void testExceptionalExposureAir70();
	void testExceptionalExposureAirO2_70();
	void testExceptionalExposureAirO2_100();
	void testExceptionalExposureDepth();
	void testVval79BounceFirstStop();
	void testExceptionalExposureIgnoresGasModWaypoint();
};

#endif // TESTTHALMANN_H
