// SPDX-License-Identifier: MIT
//
// haydn / SM8350: satisfy AMFI's PMGR latch without an Apple PMGR.
//
// The unchanged AppleMobileFileIntegrity kext looks for a service matching
// "ApplePMGRNub", then calls the nub's readAon/writeAon slots, which forward to
// the provider stored at nub+0x88. With no provider those helpers return 0 and
// drop the write, which is exactly what the ConfigurationSettings latch wants
// (read 0, OR 4, write no-op). Without this the kernel panics with
// "AMFI: No PMGR?" @ ConfigurationSettings.cpp:235.
//
// The class itself comes from the prelinked kernel: allocate it through its
// OSMetaClass so no exact subclass layout is needed here, attach it to the
// platform expert and publish it. sheng_pmgr_run() is called from bsd_init by
// patches/xnu/sheng-platform-power.patch when XNU_SHENG_PMGR=1.

#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/c++/OSMetaClass.h>
#include <libkern/c++/OSObject.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/libkern.h>

static const OSMetaClass *
haydn_lookup_class(const char *name)
{
	const OSSymbol *symbol = OSSymbol::withCString(name);

	if (symbol == NULL) {
		return NULL;
	}
	return OSMetaClass::getMetaClassWithName(symbol);
}

extern "C" void
sheng_pmgr_run(void)
{
	const OSMetaClass *meta;
	OSObject *object;
	IOService *nub;
	IOPlatformExpert *platform;

	meta = haydn_lookup_class("ApplePMGRNub");
	if (meta == NULL) {
		kprintf("haydn-pmgr: ApplePMGRNub is not in the kernel\n");
		return;
	}

	object = meta->alloc();
	if (object == NULL) {
		kprintf("haydn-pmgr: alloc failed\n");
		return;
	}

	nub = OSDynamicCast(IOService, object);
	if (nub == NULL) {
		kprintf("haydn-pmgr: ApplePMGRNub is not an IOService\n");
		object->release();
		return;
	}

	if (!nub->init()) {
		kprintf("haydn-pmgr: init failed\n");
		nub->release();
		return;
	}

	platform = IOService::getPlatform();
	if (platform != NULL) {
		nub->attach(platform);
	}
	nub->registerService();

	kprintf("haydn-pmgr: provider-less ApplePMGRNub published\n");

	// Bring up the touch controller from the same bsd_init hook. The driver is
	// in package/HaydnTouch.cpp and linked as build/haydn-touch/libHaydnTouch.a;
	// its own register reads decide the chip identity, so a mismatch here only
	// costs a log line, not a panic.
	extern void haydn_touch_run(void);
	haydn_touch_run();
}
