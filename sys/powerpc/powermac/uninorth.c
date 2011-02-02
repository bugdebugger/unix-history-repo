/*-
 * Copyright (C) 2002 Benno Rice.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/pio.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <powerpc/powermac/uninorthvar.h>

#include <vm/vm.h>
#include <vm/pmap.h>

/*
 * Driver for the Uninorth chip itself.
 */

static MALLOC_DEFINE(M_UNIN, "unin", "unin device information");

/*
 * Device interface.
 */

static int  unin_chip_probe(device_t);
static int  unin_chip_attach(device_t);

/*
 * Bus interface.
 */
static int  unin_chip_print_child(device_t dev, device_t child);
static void unin_chip_probe_nomatch(device_t, device_t);
static struct resource *unin_chip_alloc_resource(device_t, device_t, int, int *,
						 u_long, u_long, u_long, u_int);
static int  unin_chip_activate_resource(device_t, device_t, int, int,
					struct resource *);
static int  unin_chip_deactivate_resource(device_t, device_t, int, int,
					  struct resource *);
static int  unin_chip_release_resource(device_t, device_t, int, int,
				       struct resource *);
static struct resource_list *unin_chip_get_resource_list (device_t, device_t);

/*
 * OFW Bus interface
 */

static ofw_bus_get_devinfo_t unin_chip_get_devinfo;

/*
 * Local routines
 */

static void		unin_enable_gmac(device_t dev);
static void		unin_enable_mpic(device_t dev);

/*
 * Driver methods.
 */
static device_method_t unin_chip_methods[] = {

	/* Device interface */
	DEVMETHOD(device_probe,         unin_chip_probe),
	DEVMETHOD(device_attach,        unin_chip_attach),

	/* Bus interface */
	DEVMETHOD(bus_print_child,      unin_chip_print_child),
	DEVMETHOD(bus_probe_nomatch,    unin_chip_probe_nomatch),
	DEVMETHOD(bus_setup_intr,       bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,    bus_generic_teardown_intr),

	DEVMETHOD(bus_alloc_resource,   unin_chip_alloc_resource),
	DEVMETHOD(bus_release_resource, unin_chip_release_resource),
	DEVMETHOD(bus_activate_resource, unin_chip_activate_resource),
	DEVMETHOD(bus_deactivate_resource, unin_chip_deactivate_resource),
	DEVMETHOD(bus_get_resource_list, unin_chip_get_resource_list),

	DEVMETHOD(bus_child_pnpinfo_str, ofw_bus_gen_child_pnpinfo_str),

        /* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_devinfo,	unin_chip_get_devinfo),
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	{ 0, 0 }
};

static driver_t	unin_chip_driver = {
	"unin",
	unin_chip_methods,
	sizeof(struct unin_chip_softc)
};

static devclass_t	unin_chip_devclass;

DRIVER_MODULE(unin, nexus, unin_chip_driver, unin_chip_devclass, 0, 0);

/*
 * Add an interrupt to the dev's resource list if present
 */
static void
unin_chip_add_intr(phandle_t devnode, struct unin_chip_devinfo *dinfo)
{
	phandle_t iparent;
	int	*intr;
	int	i, nintr;
	int 	icells;

	if (dinfo->udi_ninterrupts >= 6) {
		printf("unin: device has more than 6 interrupts\n");
		return;
	}

	nintr = OF_getprop_alloc(devnode, "interrupts", sizeof(*intr), 
		(void **)&intr);
	if (nintr == -1) {
		nintr = OF_getprop_alloc(devnode, "AAPL,interrupts", 
			sizeof(*intr), (void **)&intr);
		if (nintr == -1)
			return;
	}

	if (intr[0] == -1)
		return;

	if (OF_getprop(devnode, "interrupt-parent", &iparent, sizeof(iparent))
	    <= 0)
		panic("Interrupt but no interrupt parent!\n");

	if (OF_searchprop(iparent, "#interrupt-cells", &icells, sizeof(icells))
	    <= 0)
		icells = 1;

	for (i = 0; i < nintr; i+=icells) {
		u_int irq = MAP_IRQ(iparent, intr[i]);

		resource_list_add(&dinfo->udi_resources, SYS_RES_IRQ,
		    dinfo->udi_ninterrupts, irq, irq, 1);

		if (icells > 1) {
			powerpc_config_intr(irq,
			    (intr[i+1] & 1) ? INTR_TRIGGER_LEVEL :
			    INTR_TRIGGER_EDGE, INTR_POLARITY_LOW);
		}

		dinfo->udi_interrupts[dinfo->udi_ninterrupts] = irq;
		dinfo->udi_ninterrupts++;
	}
}

static void
unin_chip_add_reg(phandle_t devnode, struct unin_chip_devinfo *dinfo)
{
	struct	unin_chip_reg *reg;
	int	i, nreg;

	nreg = OF_getprop_alloc(devnode, "reg", sizeof(*reg), (void **)&reg);
	if (nreg == -1)
		return;

	for (i = 0; i < nreg; i++) {
		resource_list_add(&dinfo->udi_resources, SYS_RES_MEMORY, i,
				  reg[i].mr_base,
				  reg[i].mr_base + reg[i].mr_size,
				  reg[i].mr_size);
	}
}

static void
unin_enable_gmac(device_t dev)
{
	volatile u_int *clkreg;
	struct unin_chip_softc *sc;
	u_int32_t tmpl;

	sc = device_get_softc(dev);
	clkreg = (void *)(sc->sc_addr + UNIN_CLOCKCNTL);
	tmpl = inl(clkreg);
	tmpl |= UNIN_CLOCKCNTL_GMAC;
	outl(clkreg, tmpl);
}

static void
unin_enable_mpic(device_t dev)
{
	volatile u_int *toggle;
	struct unin_chip_softc *sc;
	u_int32_t tmpl;

	sc = device_get_softc(dev);
	toggle = (void *)(sc->sc_addr + UNIN_TOGGLE_REG);
	tmpl = inl(toggle);
	tmpl |= UNIN_MPIC_RESET | UNIN_MPIC_OUTPUT_ENABLE;
	outl(toggle, tmpl);
}

static int
unin_chip_probe(device_t dev)
{
	const char	*name;

	name = ofw_bus_get_name(dev);

	if (name == NULL)
		return (ENXIO);

	if (strcmp(name, "uni-n") != 0 && strcmp(name, "u3") != 0
	    && strcmp(name, "u4") != 0)
		return (ENXIO);

	device_set_desc(dev, "Apple UniNorth System Controller");
	return (0);
}

static int
unin_chip_attach(device_t dev)
{
	struct unin_chip_softc *sc;
	struct unin_chip_devinfo *dinfo;
	phandle_t  root;
	phandle_t  child;
	device_t   cdev;
	char compat[32];
	u_int reg[3];
	int error, i = 0;

	sc = device_get_softc(dev);
	root = ofw_bus_get_node(dev);

	if (OF_getprop(root, "reg", reg, sizeof(reg)) < 8)
		return (ENXIO);

	if (strcmp(ofw_bus_get_name(dev), "u3") == 0
	    || strcmp(ofw_bus_get_name(dev), "u4") == 0)
		i = 1; /* #address-cells lies */

	sc->sc_physaddr = reg[i];
	sc->sc_size = reg[i+1];

	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_mem_rman.rm_descr = "UniNorth Device Memory";

	error = rman_init(&sc->sc_mem_rman);

	if (error) {
		device_printf(dev, "rman_init() failed. error = %d\n", error);
		return (error);
	}

	error = rman_manage_region(&sc->sc_mem_rman, sc->sc_physaddr,
				   sc->sc_physaddr + sc->sc_size - 1);	
	if (error) {
		device_printf(dev,
			      "rman_manage_region() failed. error = %d\n",
			      error);
		return (error);
	}

        /*
	 * Iterate through the sub-devices
	 */
	for (child = OF_child(root); child != 0; child = OF_peer(child)) {
		dinfo = malloc(sizeof(*dinfo), M_UNIN, M_WAITOK | M_ZERO);
		if (ofw_bus_gen_setup_devinfo(&dinfo->udi_obdinfo, child)
		    != 0)
		{
			free(dinfo, M_UNIN);
			continue;
		}

		resource_list_init(&dinfo->udi_resources);
		dinfo->udi_ninterrupts = 0;
		unin_chip_add_intr(child, dinfo);

		unin_chip_add_reg(child, dinfo);

		cdev = device_add_child(dev, NULL, -1);
		if (cdev == NULL) {
			device_printf(dev, "<%s>: device_add_child failed\n",
				      dinfo->udi_obdinfo.obd_name);
			resource_list_free(&dinfo->udi_resources);
			ofw_bus_gen_destroy_devinfo(&dinfo->udi_obdinfo);
			free(dinfo, M_UNIN);
			continue;
		}

		device_set_ivars(cdev, dinfo);
	}

	/*
	 * Only map the first page, since that is where the registers
	 * of interest lie.
	 */
	sc->sc_addr = (vm_offset_t)pmap_mapdev(sc->sc_physaddr, PAGE_SIZE);

	sc->sc_version = *(u_int *)sc->sc_addr;
	device_printf(dev, "Version %d\n", sc->sc_version);

	/*
	 * Enable the GMAC Ethernet cell and the integrated OpenPIC
	 * if Open Firmware says they are used.
	 */
	for (child = OF_child(root); child; child = OF_peer(child)) {
		memset(compat, 0, sizeof(compat));
		OF_getprop(child, "compatible", compat, sizeof(compat));
		if (strcmp(compat, "gmac") == 0)
			unin_enable_gmac(dev);
		if (strcmp(compat, "chrp,open-pic") == 0)
			unin_enable_mpic(dev);
	}
	
	/*
	 * GMAC lives under the PCI bus, so just check if enet is gmac.
	 */
	child = OF_finddevice("enet");
	memset(compat, 0, sizeof(compat));
	OF_getprop(child, "compatible", compat, sizeof(compat));
	if (strcmp(compat, "gmac") == 0)
		unin_enable_gmac(dev);

	return (bus_generic_attach(dev));
}

static int
unin_chip_print_child(device_t dev, device_t child)
{
        struct unin_chip_devinfo *dinfo;
        struct resource_list *rl;
        int retval = 0;

        dinfo = device_get_ivars(child);
        rl = &dinfo->udi_resources;

        retval += bus_print_child_header(dev, child);

        retval += resource_list_print_type(rl, "mem", SYS_RES_MEMORY, "%#lx");
        retval += resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%ld");

        retval += bus_print_child_footer(dev, child);

        return (retval);
}

static void
unin_chip_probe_nomatch(device_t dev, device_t child)
{
        struct unin_chip_devinfo *dinfo;
        struct resource_list *rl;
	const char *type;

	if (bootverbose) {
		dinfo = device_get_ivars(child);
		rl = &dinfo->udi_resources;

		if ((type = ofw_bus_get_type(child)) == NULL)
			type = "(unknown)";
		device_printf(dev, "<%s, %s>", type, ofw_bus_get_name(child));
		resource_list_print_type(rl, "mem", SYS_RES_MEMORY, "%#lx");
		resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%ld");
		printf(" (no driver attached)\n");
	}
}


static struct resource *
unin_chip_alloc_resource(device_t bus, device_t child, int type, int *rid,
			 u_long start, u_long end, u_long count, u_int flags)
{
	struct		unin_chip_softc *sc;
	int		needactivate;
	struct		resource *rv;
	struct		rman *rm;
	u_long		adjstart, adjend, adjcount;
	struct		unin_chip_devinfo *dinfo;
	struct		resource_list_entry *rle;

	sc = device_get_softc(bus);
	dinfo = device_get_ivars(child);

	needactivate = flags & RF_ACTIVE;
	flags &= ~RF_ACTIVE;

	switch (type) {
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		rle = resource_list_find(&dinfo->udi_resources, SYS_RES_MEMORY,
					 *rid);
		if (rle == NULL) {
			device_printf(bus, "no rle for %s memory %d\n",
				      device_get_nameunit(child), *rid);
			return (NULL);
		}

		rle->end = rle->end - 1; /* Hack? */

		if (start < rle->start)
			adjstart = rle->start;
		else if (start > rle->end)
			adjstart = rle->end;
		else
			adjstart = start;

		if (end < rle->start)
			adjend = rle->start;
		else if (end > rle->end)
			adjend = rle->end;
		else
			adjend = end;

		adjcount = adjend - adjstart;

		rm = &sc->sc_mem_rman;
		break;

	case SYS_RES_IRQ:
		/* Check for passthrough from subattachments. */
		if (device_get_parent(child) != bus)
			return BUS_ALLOC_RESOURCE(device_get_parent(bus), child,
						  type, rid, start, end, count,
						  flags);

		rle = resource_list_find(&dinfo->udi_resources, SYS_RES_IRQ,
		    *rid);
		if (rle == NULL) {
			if (dinfo->udi_ninterrupts >= 6) {
				device_printf(bus,
					      "%s has more than 6 interrupts\n",
					      device_get_nameunit(child));
				return (NULL);
			}
			resource_list_add(&dinfo->udi_resources, SYS_RES_IRQ,
					  dinfo->udi_ninterrupts, start, start,
					  1);

			dinfo->udi_interrupts[dinfo->udi_ninterrupts] = start;
			dinfo->udi_ninterrupts++;
		}

		return (resource_list_alloc(&dinfo->udi_resources, bus, child,
					    type, rid, start, end, count,
					    flags));
	default:
		device_printf(bus, "unknown resource request from %s\n",
			      device_get_nameunit(child));
		return (NULL);
	}

	rv = rman_reserve_resource(rm, adjstart, adjend, adjcount, flags,
				   child);
	if (rv == NULL) {
		device_printf(bus,
			      "failed to reserve resource %#lx - %#lx (%#lx)"
			      " for %s\n", adjstart, adjend, adjcount,
			      device_get_nameunit(child));
		return (NULL);
	}

	rman_set_rid(rv, *rid);

	if (needactivate) {
		if (bus_activate_resource(child, type, *rid, rv) != 0) {
                        device_printf(bus,
				      "failed to activate resource for %s\n",
				      device_get_nameunit(child));
			rman_release_resource(rv);
			return (NULL);
                }
        }

	return (rv);
}

static int
unin_chip_release_resource(device_t bus, device_t child, int type, int rid,
			   struct resource *res)
{
	if (rman_get_flags(res) & RF_ACTIVE) {
		int error = bus_deactivate_resource(child, type, rid, res);
		if (error)
			return error;
	}

	return (rman_release_resource(res));
}

static int
unin_chip_activate_resource(device_t bus, device_t child, int type, int rid,
			    struct resource *res)
{
	void    *p;

	if (type == SYS_RES_IRQ)
                return (bus_activate_resource(bus, type, rid, res));

	if ((type == SYS_RES_MEMORY) || (type == SYS_RES_IOPORT)) {
		vm_offset_t start;

		start = (vm_offset_t) rman_get_start(res);

		if (bootverbose)
			printf("unin mapdev: start %zx, len %ld\n", start,
			       rman_get_size(res));

		p = pmap_mapdev(start, (vm_size_t) rman_get_size(res));
		if (p == NULL)
			return (ENOMEM);
		rman_set_virtual(res, p);
		rman_set_bustag(res, &bs_be_tag);
		rman_set_bushandle(res, (u_long)p);
	}

	return (rman_activate_resource(res));
}


static int
unin_chip_deactivate_resource(device_t bus, device_t child, int type, int rid,
			      struct resource *res)
{
        /*
         * If this is a memory resource, unmap it.
         */
        if ((type == SYS_RES_MEMORY) || (type == SYS_RES_IOPORT)) {
		u_int32_t psize;
		
		psize = rman_get_size(res);
		pmap_unmapdev((vm_offset_t)rman_get_virtual(res), psize);
	}

	return (rman_deactivate_resource(res));
}


static struct resource_list *
unin_chip_get_resource_list (device_t dev, device_t child)
{
	struct unin_chip_devinfo *dinfo;

	dinfo = device_get_ivars(child);
	return (&dinfo->udi_resources);
}

static const struct ofw_bus_devinfo *
unin_chip_get_devinfo(device_t dev, device_t child)
{
	struct unin_chip_devinfo *dinfo;

	dinfo = device_get_ivars(child);
	return (&dinfo->udi_obdinfo);
}

