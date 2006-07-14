/*-
 * Copyright (c) 2005 Olivier Houchard.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/time.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/timetc.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/resource.h>
#include <machine/frame.h>
#include <machine/intr.h>
#include <arm/at91/at91rm92reg.h>
#include <arm/at91/at91var.h>
#include <arm/at91/at91_streg.h>

static struct at91st_softc {
	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	device_t		dev;
} *timer_softc;

#define RD4(off) \
	bus_space_read_4(timer_softc->sc_st, timer_softc->sc_sh, (off))
#define WR4(off, val) \
	bus_space_write_4(timer_softc->sc_st, timer_softc->sc_sh, (off), (val))

static inline int
st_crtr(void)
{
	int cur1, cur2;
	do {
		cur1 = RD4(ST_CRTR);
		cur2 = RD4(ST_CRTR);
	} while (cur1 != cur2);
	return (cur1);
}

static unsigned at91st_get_timecount(struct timecounter *tc);

static struct timecounter at91st_timecounter = {
	at91st_get_timecount, /* get_timecount */
	NULL, /* no poll_pps */
#ifdef SKYEYE_WORKAROUNDS
	0xffffffffu, /* counter_mask */
#else
	0xfffffu, /* counter_mask */
#endif
	32768, /* frequency */
	"AT91RM9200 timer", /* name */
	1000 /* quality */
};

static int
at91st_probe(device_t dev)
{

	device_set_desc(dev, "ST");
	return (0);
}

static int
at91st_attach(device_t dev)
{
	struct at91_softc *sc = device_get_softc(device_get_parent(dev));

	timer_softc = device_get_softc(dev);
	timer_softc->sc_st = sc->sc_st;
	timer_softc->dev = dev;
	if (bus_space_subregion(sc->sc_st, sc->sc_sh, AT91RM92_ST_BASE,
	    AT91RM92_ST_SIZE, &timer_softc->sc_sh) != 0)
		panic("couldn't subregion timer registers");
	/*
	 * Real time counter increments every clock cycle, need to set before
	 * initializing clocks so that DELAY works.
	 */
	WR4(ST_RTMR, 1);
	/* Disable all interrupts */
	WR4(ST_IDR, 0xffffffff);
	return (0);
}

static device_method_t at91st_methods[] = {
	DEVMETHOD(device_probe, at91st_probe),
	DEVMETHOD(device_attach, at91st_attach),
	{0, 0},
};

static driver_t at91st_driver = {
	"at91_st",
	at91st_methods,
	sizeof(struct at91st_softc),
};
static devclass_t at91st_devclass;

DRIVER_MODULE(at91_st, atmelarm, at91st_driver, at91st_devclass, 0, 0);

#ifdef SKYEYE_WORKAROUNDS
static unsigned long tot_count = 0;
#endif

static unsigned
at91st_get_timecount(struct timecounter *tc)
{
#ifdef SKYEYE_WORKAROUNDS
	return (tot_count);
#else
	return (st_crtr());
#endif
}

static void
clock_intr(void *arg)
{
	struct trapframe *fp = arg;

	/* The interrupt is shared, so we have to make sure it's for us. */
	if (RD4(ST_SR) & ST_SR_PITS) {
#ifdef SKYEYE_WORKAROUNDS
		tot_count += 32768 / hz;
#endif
		hardclock(TRAPF_USERMODE(fp), TRAPF_PC(fp));
	}
}

void
cpu_initclocks(void)
{
	int rel_value;
	struct resource *irq;
	int rid = 0;
	void *ih;
	device_t dev = timer_softc->dev;

	if (32768 % hz) {
		printf("Cannot get %d Hz clock; using 128Hz\n", hz);
		hz = 128;
	}
	rel_value = 32768 / hz;
	/* Disable all interrupts. */
	WR4(ST_IDR, 0xffffffff);
	/* The system timer shares the system irq (1) */
	irq = bus_alloc_resource(dev, SYS_RES_IRQ, &rid, 1, 1, 1,
	  RF_ACTIVE | RF_SHAREABLE);
	if (!irq)
		panic("Unable to allocate irq for the system timer");
	else
		bus_setup_intr(dev, irq, INTR_TYPE_CLK | INTR_FAST,
		    clock_intr, NULL, &ih);

	WR4(ST_PIMR, rel_value);

	/* Enable PITS interrupts. */
	WR4(ST_IER, ST_SR_PITS);
	tc_init(&at91st_timecounter);
}

void
DELAY(int n)
{
	uint32_t start, end, cur;

	start = st_crtr();
	n = (n * 1000) / 32768;
	if (n <= 0)
		n = 1;
	end = (start + n) & ST_CRTR_MASK;
	cur = start;
	if (start > end) {
		while (cur >= start || cur < end)
			cur = st_crtr();
	} else {
		while (cur < end)
			cur = st_crtr();
	}
}

void
cpu_reset(void)
{
	/*
	 * Reset the CPU by programmig the watchdog timer to reset the
	 * CPU after 128 'slow' clocks, or about ~4ms.  Loop until
	 * the reset happens for safety.
	 */
	WR4(ST_WDMR, ST_WDMR_RSTEN | 2);
	WR4(ST_CR, ST_CR_WDRST);
	while (1)
		continue;
}

void
cpu_startprofclock(void)
{
}

void
cpu_stopprofclock(void)
{
}

