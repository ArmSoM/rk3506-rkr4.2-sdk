#include <rtthread.h>

#ifdef RT_USING_TINYMEMBENCH
extern int tinymembench_main(void);
#endif

#ifdef RT_USING_LINPACK
extern void linpack_main(void);
#endif

#ifdef RT_USING_DHRYSTONE
extern void dhrystone_main(void);
#endif

#ifdef RT_USING_FHOURSTONE
extern int fhourstones_main();
#endif

#ifdef RT_USING_PI_CSS5
extern int pi_css5_main(int argc, char *argv[]);
#endif

#ifdef RT_USING_WHETSTONE
extern int whetstone_main(int argc, char *argv[]);
#endif

#ifdef RT_USING_COREMARK
extern void coremark_main(void);
#endif

void benchmark(void)
{
#if defined(RT_USING_PI_CSS5) || defined(RT_USING_WHETSTONE)
    char *argv[4];
#endif

#ifdef RT_USING_TINYMEMBENCH
    tinymembench_main();
#endif

#ifdef RT_USING_LINPACK
    linpack_main();
#endif

#ifdef RT_USING_DHRYSTONE
    dhrystone_main();
#endif

#ifdef RT_USING_FHOURSTONE
    fhourstones_main();
#endif

#ifdef RT_USING_PI_CSS5
    argv[0] = "pi";
    argv[1] = "1000000";
    pi_css5_main(2, argv);
#endif

#ifdef RT_USING_WHETSTONE
    argv[0] = "whetdc";
    argv[1] = "1000000";
    whetstone_main(2, argv);
#endif

#ifdef RT_USING_COREMARK
    coremark_main();
#endif
}

MSH_CMD_EXPORT(benchmark, benchmark);
