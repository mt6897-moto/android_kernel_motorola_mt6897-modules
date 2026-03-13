obj-$(CONFIG_LENOVO_HYPER_SCHED)    += lenovohyperSched.o

lenovohyperSched-y                  += lenovo_hypersched.o lenovo_hyperrwsem.o lenovo_hypermutex.o \
                                       lenovo_hypernode.o lenovo_hyperprio.o dynamic_prio.o \
                                       lenovo_hyperbandwidth.o

subdir-ccflags-y += -I$(srctree)/kernel/
subdir-ccflags-y += -I$(srctree)/fs/
subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/kernel/sched/lenovohypersched/
