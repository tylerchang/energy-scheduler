#include <linux/init.h>       // Macros for module initialization
#include <linux/module.h>     // Core header for loading modules
#include <linux/kernel.h>     // Kernel logging macros
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>

/* Declare the kfunc prototype */
__bpf_kfunc int read_core_energy(int cpu);

/* Begin kfunc definitions */
__bpf_kfunc_start_defs();

/* Define the read_core_energy kfunc */
__visible noinline __bpf_kfunc int read_core_energy(int cpu)
{
    printk("CPU: %d", cpu);
    return 0;
}

/* End kfunc definitions */
__bpf_kfunc_end_defs();

/* Define the BTF kfuncs ID set */
BTF_KFUNCS_START(bpf_kfunc_example_ids_set)
BTF_ID_FLAGS(func, read_core_energy)
BTF_KFUNCS_END(bpf_kfunc_example_ids_set)

// BTF_SET8_START(read_core_energy_kfunc_ids)
// BTF_ID_FLAGS(func, read_core_energy)
// BTF_SET8_END(read_core_energy_kfunc_ids)

/* Register the kfunc ID set */
static const struct btf_kfunc_id_set bpf_kfunc_example_set = {
    .owner = THIS_MODULE,
    .set = &bpf_kfunc_example_ids_set,
};

/* Function executed when the module is loaded */
static int __init read_core_energy_init(void)
{
    int ret;

    printk(KERN_INFO "Hello, world!\n");
    /* Register the BTF kfunc ID set for BPF_PROG_TYPE_TRACEPOINT */
    ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACEPOINT, &bpf_kfunc_example_set);
    if (ret)
    {
        pr_err("bpf_kfunc_example: Failed to register BTF kfunc ID set\n");
        return ret;
    }
    printk(KERN_INFO "bpf_kfunc_example: Module loaded successfully\n");
    return 0; // Return 0 if successful
}

/* Function executed when the module is removed */
static void __exit read_core_energy_exit(void)
{
    /* Unregister the BTF kfunc ID set */
    // unregister_btf_kfunc_id_set(BPF_PROG_TYPE_KPROBE, &bpf_kfunc_example_set);
    // printk(KERN_INFO "Goodbye, world!\n");
}

/* Macros to define the module’s init and exit points */
module_init(read_core_energy_init);
module_exit(read_core_energy_exit);

MODULE_LICENSE("GPL");                 // License type (GPL)
MODULE_AUTHOR("Your Name");            // Module author
MODULE_DESCRIPTION("A simple module"); // Module description
MODULE_VERSION("1.0");                 // Module version
