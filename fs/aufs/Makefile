
include ${srctree}/${src}/magic.mk

obj-$(CONFIG_AUFS_FS) += aufs.o
aufs-y := module.o sbinfo.o super.o branch.o xino.o sysaufs.o opts.o \
	wkq.o vfsub.o dcsub.o \
	cpup.o whout.o plink.o wbr_policy.o \
	dinfo.o dentry.o \
	finfo.o file.o f_op.o \
	dir.o vdir.o \
	iinfo.o inode.o i_op.o i_op_add.o i_op_del.o i_op_ren.o \
	ioctl.o

# all are boolean
aufs-$(CONFIG_SYSFS) += sysfs.o
aufs-$(CONFIG_DEBUG_FS) += dbgaufs.o
aufs-$(CONFIG_AUFS_BDEV_LOOP) += loop.o
aufs-$(CONFIG_AUFS_HINOTIFY) += hinotify.o
aufs-$(CONFIG_AUFS_EXPORT) += export.o
aufs-$(CONFIG_AUFS_POLL) += poll.o
aufs-$(CONFIG_AUFS_DEBUG) += debug.o
aufs-$(CONFIG_AUFS_MAGIC_SYSRQ) += sysrq.o
