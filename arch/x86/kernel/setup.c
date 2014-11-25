/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *
 *  Memory region support
 *	David Parsons <orc@pell.chi.il.us>, July-August 1999
 *
 *  Added E820 sanitization routine (removes overlapping memory regions);
 *  Brian Moyle <bmoyle@mvista.com>, February 2001
 *
 * Moved CPU detection code to cpu/${cpu}.c
 *    Patrick Mochel <mochel@osdl.org>, March 2002
 *
 *  Provisions for empty E820 memory regions (reported by certain BIOSes).
 *  Alex Achenbach <xela@slit.de>, December 2002.
 *
 */


#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/sfi.h>
#include <linux/apm_bios.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/mca.h>
#include <linux/root_dev.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/edd.h>
#include <linux/iscsi_ibft.h>
#include <linux/nodemask.h>
#include <linux/kexec.h>
#include <linux/dmi.h>
#include <linux/pfn.h>
#include <linux/pci.h>
#include <asm/pci-direct.h>
#include <linux/init_ohci1394_dma.h>
#include <linux/kvm_para.h>
#include <linux/dma-contiguous.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/delay.h>

#include <linux/kallsyms.h>
#include <linux/cpufreq.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>

#include <linux/percpu.h>
#include <linux/crash_dump.h>
#include <linux/tboot.h>

#include <video/edid.h>

#include <asm/mtrr.h>
#include <asm/apic.h>
#include <asm/trampoline.h>
#include <asm/e820.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/efi.h>
#include <asm/timer.h>
#include <asm/i8259.h>
#include <asm/sections.h>
#include <asm/dmi.h>
#include <asm/io_apic.h>
#include <asm/ist.h>
#include <asm/setup_arch.h>
#include <asm/bios_ebda.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/bugs.h>

#include <asm/vsyscall.h>
#include <asm/cpu.h>
#include <asm/desc.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>

#include <asm/paravirt.h>
#include <asm/hypervisor.h>
#include <asm/olpc_ofw.h>

#include <asm/percpu.h>
#include <asm/topology.h>
#include <asm/apicdef.h>
#include <asm/amd_nb.h>
#ifdef CONFIG_X86_64
#include <asm/numa_64.h>
#endif
#include <asm/mce.h>
#include <asm/alternative.h>
#include <asm/prom.h>

unsigned long max_low_pfn_mapped;
unsigned long max_pfn_mapped;

#ifdef CONFIG_DMI
RESERVE_BRK(dmi_alloc, 65536);
#endif


static __initdata unsigned long _brk_start = (unsigned long)__brk_base;
unsigned long _brk_end = (unsigned long)__brk_base;

#ifdef CONFIG_X86_64
int default_cpu_present_to_apicid(int mps_cpu)
{
	return __default_cpu_present_to_apicid(mps_cpu);
}

int default_check_phys_apicid_present(int phys_apicid)
{
	return __default_check_phys_apicid_present(phys_apicid);
}
#endif

#ifndef CONFIG_DEBUG_BOOT_PARAMS
struct boot_params __initdata boot_params;
#else
struct boot_params boot_params;
#endif

static struct resource data_resource = {
	.name	= "Kernel data",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource code_resource = {
	.name	= "Kernel code",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource bss_resource = {
	.name	= "Kernel bss",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};


#ifdef CONFIG_X86_32
struct cpuinfo_x86 new_cpu_data __cpuinitdata = {0, 0, 0, 0, -1, 1, 0, 0, -1};
struct cpuinfo_x86 boot_cpu_data __read_mostly = {0, 0, 0, 0, -1, 1, 0, 0, -1};
EXPORT_SYMBOL(boot_cpu_data);
static void set_mca_bus(int x)
{
#ifdef CONFIG_MCA
	MCA_bus = x;
#endif
}

unsigned int def_to_bigsmp;

unsigned int machine_id;
unsigned int machine_submodel_id;
unsigned int BIOS_revision;

struct apm_info apm_info;
EXPORT_SYMBOL(apm_info);

#if defined(CONFIG_X86_SPEEDSTEP_SMI) || \
	defined(CONFIG_X86_SPEEDSTEP_SMI_MODULE)
struct ist_info ist_info;
EXPORT_SYMBOL(ist_info);
#else
struct ist_info ist_info;
#endif

#else
struct cpuinfo_x86 boot_cpu_data __read_mostly = {
	.x86_phys_bits = MAX_PHYSMEM_BITS,
};
EXPORT_SYMBOL(boot_cpu_data);
#endif


#if !defined(CONFIG_X86_PAE) || defined(CONFIG_X86_64)
unsigned long mmu_cr4_features;
#else
unsigned long mmu_cr4_features = X86_CR4_PAE;
#endif

int bootloader_type, bootloader_version;

struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);

extern int root_mountflags;

unsigned long saved_video_mode;

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char __initdata command_line[COMMAND_LINE_SIZE];
#ifdef CONFIG_CMDLINE_BOOL
static char __initdata builtin_cmdline[COMMAND_LINE_SIZE] = CONFIG_CMDLINE;
#endif

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
struct edd edd;
#ifdef CONFIG_EDD_MODULE
EXPORT_SYMBOL(edd);
#endif
static inline void __init copy_edd(void)
{
     memcpy(edd.mbr_signature, boot_params.edd_mbr_sig_buffer,
	    sizeof(edd.mbr_signature));
     memcpy(edd.edd_info, boot_params.eddbuf, sizeof(edd.edd_info));
     edd.mbr_signature_nr = boot_params.edd_mbr_sig_buf_entries;
     edd.edd_info_nr = boot_params.eddbuf_entries;
}
#else
static inline void __init copy_edd(void)
{
}
#endif

void * __init extend_brk(size_t size, size_t align)
{
	size_t mask = align - 1;
	void *ret;

	BUG_ON(_brk_start == 0);
	BUG_ON(align & mask);

	_brk_end = (_brk_end + mask) & ~mask;
	BUG_ON((char *)(_brk_end + size) > __brk_limit);

	ret = (void *)_brk_end;
	_brk_end += size;

	memset(ret, 0, size);

	return ret;
}

#ifdef CONFIG_X86_64
static void __init init_gbpages(void)
{
	if (direct_gbpages && cpu_has_gbpages)
		printk(KERN_INFO "Using GB pages for direct mapping\n");
	else
		direct_gbpages = 0;
}
#else
static inline void init_gbpages(void)
{
}
static void __init cleanup_highmap(void)
{
}
#endif

static void __init reserve_brk(void)
{
	if (_brk_end > _brk_start)
		memblock_reserve(__pa(_brk_start),
				 __pa(_brk_end) - __pa(_brk_start));

	_brk_start = 0;
}

#ifdef CONFIG_BLK_DEV_INITRD

#define MAX_MAP_CHUNK	(NR_FIX_BTMAPS << PAGE_SHIFT)
static void __init relocate_initrd(void)
{
	
	u64 ramdisk_image = boot_params.hdr.ramdisk_image;
	u64 ramdisk_size  = boot_params.hdr.ramdisk_size;
	u64 area_size     = PAGE_ALIGN(ramdisk_size);
	u64 end_of_lowmem = max_low_pfn_mapped << PAGE_SHIFT;
	u64 ramdisk_here;
	unsigned long slop, clen, mapaddr;
	char *p, *q;

	
	ramdisk_here = memblock_find_in_range(0, end_of_lowmem, area_size,
					 PAGE_SIZE);

	if (!ramdisk_here)
		panic("Cannot find place for new RAMDISK of size %lld\n",
			 ramdisk_size);

	memblock_reserve(ramdisk_here, area_size);
	initrd_start = ramdisk_here + PAGE_OFFSET;
	initrd_end   = initrd_start + ramdisk_size;
	printk(KERN_INFO "Allocated new RAMDISK: %08llx - %08llx\n",
			 ramdisk_here, ramdisk_here + ramdisk_size);

	q = (char *)initrd_start;

	
	if (ramdisk_image < end_of_lowmem) {
		clen = end_of_lowmem - ramdisk_image;
		p = (char *)__va(ramdisk_image);
		memcpy(q, p, clen);
		q += clen;
		ramdisk_image += clen;
		ramdisk_size  -= clen;
	}

	
	while (ramdisk_size) {
		slop = ramdisk_image & ~PAGE_MASK;
		clen = ramdisk_size;
		if (clen > MAX_MAP_CHUNK-slop)
			clen = MAX_MAP_CHUNK-slop;
		mapaddr = ramdisk_image & PAGE_MASK;
		p = early_memremap(mapaddr, clen+slop);
		memcpy(q, p+slop, clen);
		early_iounmap(p, clen+slop);
		q += clen;
		ramdisk_image += clen;
		ramdisk_size  -= clen;
	}
	
	ramdisk_image = boot_params.hdr.ramdisk_image;
	ramdisk_size  = boot_params.hdr.ramdisk_size;
	printk(KERN_INFO "Move RAMDISK from %016llx - %016llx to"
		" %08llx - %08llx\n",
		ramdisk_image, ramdisk_image + ramdisk_size - 1,
		ramdisk_here, ramdisk_here + ramdisk_size - 1);
}

static void __init reserve_initrd(void)
{
	
	u64 ramdisk_image = boot_params.hdr.ramdisk_image;
	u64 ramdisk_size  = boot_params.hdr.ramdisk_size;
	u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);
	u64 end_of_lowmem = max_low_pfn_mapped << PAGE_SHIFT;

	if (!boot_params.hdr.type_of_loader ||
	    !ramdisk_image || !ramdisk_size)
		return;		

	initrd_start = 0;

	if (ramdisk_size >= (end_of_lowmem>>1)) {
		memblock_free(ramdisk_image, ramdisk_end - ramdisk_image);
		printk(KERN_ERR "initrd too large to handle, "
		       "disabling initrd\n");
		return;
	}

	printk(KERN_INFO "RAMDISK: %08llx - %08llx\n", ramdisk_image,
			ramdisk_end);


	if (ramdisk_end <= end_of_lowmem) {
		
		initrd_start = ramdisk_image + PAGE_OFFSET;
		initrd_end = initrd_start + ramdisk_size;
		return;
	}

	relocate_initrd();

	memblock_free(ramdisk_image, ramdisk_end - ramdisk_image);
}
#else
static void __init reserve_initrd(void)
{
}
#endif 

static void __init parse_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data;

	if (boot_params.hdr.version < 0x0209)
		return;
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		u32 data_len, map_len;

		map_len = max(PAGE_SIZE - (pa_data & ~PAGE_MASK),
			      (u64)sizeof(struct setup_data));
		data = early_memremap(pa_data, map_len);
		data_len = data->len + sizeof(struct setup_data);
		if (data_len > map_len) {
			early_iounmap(data, map_len);
			data = early_memremap(pa_data, data_len);
			map_len = data_len;
		}

		switch (data->type) {
		case SETUP_E820_EXT:
			parse_e820_ext(data);
			break;
		case SETUP_DTB:
			add_dtb(pa_data);
			break;
		default:
			break;
		}
		pa_data = data->next;
		early_iounmap(data, map_len);
	}
}

static void __init e820_reserve_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data;
	int found = 0;

	if (boot_params.hdr.version < 0x0209)
		return;
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		e820_update_range(pa_data, sizeof(*data)+data->len,
			 E820_RAM, E820_RESERVED_KERN);
		found = 1;
		pa_data = data->next;
		early_iounmap(data, sizeof(*data));
	}
	if (!found)
		return;

	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
	memcpy(&e820_saved, &e820, sizeof(struct e820map));
	printk(KERN_INFO "extended physical RAM map:\n");
	e820_print_map("reserve setup_data");
}

static void __init memblock_x86_reserve_range_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data;

	if (boot_params.hdr.version < 0x0209)
		return;
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		memblock_reserve(pa_data, sizeof(*data) + data->len);
		pa_data = data->next;
		early_iounmap(data, sizeof(*data));
	}
}


#ifdef CONFIG_KEXEC

#ifdef CONFIG_X86_32
# define CRASH_KERNEL_ADDR_MAX	(512 << 20)
#else
# define CRASH_KERNEL_ADDR_MAX	(896 << 20)
#endif

static void __init reserve_crashkernel(void)
{
	unsigned long long total_mem;
	unsigned long long crash_size, crash_base;
	int ret;

	total_mem = memblock_phys_mem_size();

	ret = parse_crashkernel(boot_command_line, total_mem,
			&crash_size, &crash_base);
	if (ret != 0 || crash_size <= 0)
		return;

	
	if (crash_base <= 0) {
		const unsigned long long alignment = 16<<20;	

		crash_base = memblock_find_in_range(alignment,
			       CRASH_KERNEL_ADDR_MAX, crash_size, alignment);

		if (!crash_base) {
			pr_info("crashkernel reservation failed - No suitable area found.\n");
			return;
		}
	} else {
		unsigned long long start;

		start = memblock_find_in_range(crash_base,
				 crash_base + crash_size, crash_size, 1<<20);
		if (start != crash_base) {
			pr_info("crashkernel reservation failed - memory is in use.\n");
			return;
		}
	}
	memblock_reserve(crash_base, crash_size);

	printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
			"for crashkernel (System RAM: %ldMB)\n",
			(unsigned long)(crash_size >> 20),
			(unsigned long)(crash_base >> 20),
			(unsigned long)(total_mem >> 20));

	crashk_res.start = crash_base;
	crashk_res.end   = crash_base + crash_size - 1;
	insert_resource(&iomem_resource, &crashk_res);
}
#else
static void __init reserve_crashkernel(void)
{
}
#endif

static struct resource standard_io_resources[] = {
	{ .name = "dma1", .start = 0x00, .end = 0x1f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic1", .start = 0x20, .end = 0x21,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer0", .start = 0x40, .end = 0x43,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer1", .start = 0x50, .end = 0x53,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x60, .end = 0x60,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x64, .end = 0x64,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma page reg", .start = 0x80, .end = 0x8f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic2", .start = 0xa0, .end = 0xa1,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma2", .start = 0xc0, .end = 0xdf,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "fpu", .start = 0xf0, .end = 0xff,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO }
};

void __init reserve_standard_io_resources(void)
{
	int i;

	
	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);

}

static __init void reserve_ibft_region(void)
{
	unsigned long addr, size = 0;

	addr = find_ibft_region(&size);

	if (size)
		memblock_reserve(addr, size);
}

static unsigned reserve_low = CONFIG_X86_RESERVE_LOW << 10;

static bool __init snb_gfx_workaround_needed(void)
{
#ifdef CONFIG_PCI
	int i;
	u16 vendor, devid;
	static const u16 snb_ids[] = {
		0x0102,
		0x0112,
		0x0122,
		0x0106,
		0x0116,
		0x0126,
		0x010a,
	};

	/* Assume no if something weird is going on with PCI */
	if (!early_pci_allowed())
		return false;

	vendor = read_pci_config_16(0, 2, 0, PCI_VENDOR_ID);
	if (vendor != 0x8086)
		return false;

	devid = read_pci_config_16(0, 2, 0, PCI_DEVICE_ID);
	for (i = 0; i < ARRAY_SIZE(snb_ids); i++)
		if (devid == snb_ids[i])
			return true;
#endif

	return false;
}

/*
 * Sandy Bridge graphics has trouble with certain ranges, exclude
 * them from allocation.
 */
static void __init trim_snb_memory(void)
{
	static const unsigned long bad_pages[] = {
		0x20050000,
		0x20110000,
		0x20130000,
		0x20138000,
		0x40004000,
	};
	int i;

	if (!snb_gfx_workaround_needed())
		return;

	printk(KERN_DEBUG "reserving inaccessible SNB gfx pages\n");

	/*
	 * Reserve all memory below the 1 MB mark that has not
	 * already been reserved.
	 */
	memblock_reserve(0, 1<<20);

	for (i = 0; i < ARRAY_SIZE(bad_pages); i++) {
		if (memblock_reserve(bad_pages[i], PAGE_SIZE))
			printk(KERN_WARNING "failed to reserve 0x%08lx\n",
			       bad_pages[i]);
	}
}

/*
 * Here we put platform-specific memory range workarounds, i.e.
 * memory known to be corrupt or otherwise in need to be reserved on
 * specific platforms.
 *
 * If this gets used more widely it could use a real dispatch mechanism.
 */
static void __init trim_platform_memory_ranges(void)
{
	trim_snb_memory();
}

static void __init trim_bios_range(void)
{
	e820_update_range(0, ALIGN(reserve_low, PAGE_SIZE),
			  E820_RAM, E820_RESERVED);

	e820_remove_range(BIOS_BEGIN, BIOS_END - BIOS_BEGIN, E820_RAM, 1);

	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
}

static int __init parse_reservelow(char *p)
{
	unsigned long long size;

	if (!p)
		return -EINVAL;

	size = memparse(p, &p);

	if (size < 4096)
		size = 4096;

	if (size > 640*1024)
		size = 640*1024;

	reserve_low = size;

	return 0;
}

early_param("reservelow", parse_reservelow);


void __init setup_arch(char **cmdline_p)
{
#ifdef CONFIG_X86_32
	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));
	visws_early_detect();

	clone_pgd_range(swapper_pg_dir     + KERNEL_PGD_BOUNDARY,
			initial_page_table + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	load_cr3(swapper_pg_dir);
	__flush_tlb_all();
#else
	printk(KERN_INFO "Command line: %s\n", boot_command_line);
#endif

	olpc_ofw_detect();

	early_trap_init();
	early_cpu_init();
	early_ioremap_init();

	setup_olpc_ofw_pgd();

	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);
	screen_info = boot_params.screen_info;
	edid_info = boot_params.edid_info;
#ifdef CONFIG_X86_32
	apm_info.bios = boot_params.apm_bios_info;
	ist_info = boot_params.ist_info;
	if (boot_params.sys_desc_table.length != 0) {
		set_mca_bus(boot_params.sys_desc_table.table[3] & 0x2);
		machine_id = boot_params.sys_desc_table.table[0];
		machine_submodel_id = boot_params.sys_desc_table.table[1];
		BIOS_revision = boot_params.sys_desc_table.table[2];
	}
#endif
	saved_video_mode = boot_params.hdr.vid_mode;
	bootloader_type = boot_params.hdr.type_of_loader;
	if ((bootloader_type >> 4) == 0xe) {
		bootloader_type &= 0xf;
		bootloader_type |= (boot_params.hdr.ext_loader_type+0x10) << 4;
	}
	bootloader_version  = bootloader_type & 0xf;
	bootloader_version |= boot_params.hdr.ext_loader_ver << 4;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = boot_params.hdr.ram_size & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((boot_params.hdr.ram_size & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((boot_params.hdr.ram_size & RAMDISK_LOAD_FLAG) != 0);
#endif
#ifdef CONFIG_EFI
	if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
		     "EL32", 4)) {
		efi_enabled = 1;
		efi_64bit = false;
	} else if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
		     "EL64", 4)) {
		efi_enabled = 1;
		efi_64bit = true;
	}
	if (efi_enabled && efi_memblock_x86_reserve_range())
		efi_enabled = 0;
#endif

	x86_init.oem.arch_setup();

	iomem_resource.end = (1ULL << boot_cpu_data.x86_phys_bits) - 1;
	setup_memory_map();
	parse_setup_data();
	
	e820_reserve_setup_data();

	copy_edd();

	if (!boot_params.hdr.root_flags)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = _brk_end;

	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;
	bss_resource.start = virt_to_phys(&__bss_start);
	bss_resource.end = virt_to_phys(&__bss_stop)-1;

#ifdef CONFIG_CMDLINE_BOOL
#ifdef CONFIG_CMDLINE_OVERRIDE
	strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
#else
	if (builtin_cmdline[0]) {
		
		strlcat(builtin_cmdline, " ", COMMAND_LINE_SIZE);
		strlcat(builtin_cmdline, boot_command_line, COMMAND_LINE_SIZE);
		strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
	}
#endif
#endif

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	x86_configure_nx();

	parse_early_param();

	x86_report_nx();

	
	memblock_x86_reserve_range_setup_data();

	if (acpi_mps_check()) {
#ifdef CONFIG_X86_LOCAL_APIC
		disable_apic = 1;
#endif
		setup_clear_cpu_cap(X86_FEATURE_APIC);
	}

#ifdef CONFIG_PCI
	if (pci_early_dump_regs)
		early_dump_pci_devices();
#endif

	finish_e820_parsing();

	if (efi_enabled)
		efi_init();

	dmi_scan_machine();

	init_hypervisor_platform();

	x86_init.resources.probe_roms();

	
	insert_resource(&iomem_resource, &code_resource);
	insert_resource(&iomem_resource, &data_resource);
	insert_resource(&iomem_resource, &bss_resource);

	trim_bios_range();
#ifdef CONFIG_X86_32
	if (ppro_with_ram_bug()) {
		e820_update_range(0x70000000ULL, 0x40000ULL, E820_RAM,
				  E820_RESERVED);
		sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
		printk(KERN_INFO "fixed physical RAM map:\n");
		e820_print_map("bad_ppro");
	}
#else
	early_gart_iommu_check();
#endif

	max_pfn = e820_end_of_ram_pfn();

	
	mtrr_bp_init();
	if (mtrr_trim_uncached_memory(max_pfn))
		max_pfn = e820_end_of_ram_pfn();

#ifdef CONFIG_X86_32
	
	find_low_pfn_range();
#else
	num_physpages = max_pfn;

	check_x2apic();

	
	
	if (max_pfn > (1UL<<(32 - PAGE_SHIFT)))
		max_low_pfn = e820_end_of_low_ram_pfn();
	else
		max_low_pfn = max_pfn;

	high_memory = (void *)__va(max_pfn * PAGE_SIZE - 1) + 1;
#endif

	find_smp_config();

	reserve_ibft_region();

	reserve_brk();

	cleanup_highmap();

	memblock.current_limit = get_max_mapped();
	memblock_x86_fill();

	if (efi_enabled)
		efi_reserve_boot_services();

	
	early_reserve_e820_mpc_new();

#ifdef CONFIG_X86_CHECK_BIOS_CORRUPTION
	setup_bios_corruption_check();
#endif

	printk(KERN_DEBUG "initial memory mapped : 0 - %08lx\n",
			max_pfn_mapped<<PAGE_SHIFT);

	setup_trampolines();

	trim_platform_memory_ranges();

	init_gbpages();

	
	max_low_pfn_mapped = init_memory_mapping(0, max_low_pfn<<PAGE_SHIFT);
	max_pfn_mapped = max_low_pfn_mapped;

#ifdef CONFIG_X86_64
	if (max_pfn > max_low_pfn) {
		int i;
		unsigned long start, end;
		unsigned long start_pfn, end_pfn;

		for_each_mem_pfn_range(i, MAX_NUMNODES, &start_pfn, &end_pfn,
							 NULL) {

			end = PFN_PHYS(end_pfn);
			if (end <= (1UL<<32))
				continue;

			start = PFN_PHYS(start_pfn);
			max_pfn_mapped = init_memory_mapping(
						max((1UL<<32), start), end);
		}

 		/* can we preseve max_low_pfn ?*/
		max_low_pfn = max_pfn;
	}
#endif
	memblock.current_limit = get_max_mapped();
	dma_contiguous_reserve(0);


#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif
	
	setup_log_buf(1);

	reserve_initrd();

	reserve_crashkernel();

	vsmp_init();

	io_delay_init();

	acpi_boot_table_init();

	early_acpi_boot_init();

	initmem_init();
	memblock_find_dma_reserve();

#ifdef CONFIG_KVM_CLOCK
	kvmclock_init();
#endif

	x86_init.paging.pagetable_setup_start(swapper_pg_dir);
	paging_init();
	x86_init.paging.pagetable_setup_done(swapper_pg_dir);

	if (boot_cpu_data.cpuid_level >= 0) {
		
		mmu_cr4_features = read_cr4();
	}

#ifdef CONFIG_X86_32
	
	clone_pgd_range(initial_page_table + KERNEL_PGD_BOUNDARY,
			swapper_pg_dir     + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);
#endif

	tboot_probe();

#ifdef CONFIG_X86_64
	map_vsyscall();
#endif

	generic_apic_probe();

	early_quirks();

	acpi_boot_init();
	sfi_init();
	x86_dtb_init();

	if (smp_found_config)
		get_smp_config();

	prefill_possible_map();

	init_cpu_to_node();

	init_apic_mappings();
	ioapic_and_gsi_init();

	kvm_guest_init();

	e820_reserve_resources();
	e820_mark_nosave_regions(max_low_pfn);

	x86_init.resources.reserve_resources();

	e820_setup_gap();

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	if (!efi_enabled || (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
		conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
	x86_init.oem.banner();

	x86_init.timers.wallclock_init();

	x86_platform.wallclock_init();

	mcheck_init();

	arch_init_ideal_nops();

#ifdef CONFIG_EFI
	/* Once setup is done above, disable efi_enabled on mismatched
	 * firmware/kernel archtectures since there is no support for
	 * runtime services.
	 */
	if (efi_enabled && IS_ENABLED(CONFIG_X86_64) != efi_64bit) {
		pr_info("efi: Setup done, disabling due to 32/64-bit mismatch\n");
		efi_unmap_memmap();
		efi_enabled = 0;
	}
#endif
}

#ifdef CONFIG_X86_32

static struct resource video_ram_resource = {
	.name	= "Video RAM area",
	.start	= 0xa0000,
	.end	= 0xbffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

void __init i386_reserve_resources(void)
{
	request_resource(&iomem_resource, &video_ram_resource);
	reserve_standard_io_resources();
}

#endif 
