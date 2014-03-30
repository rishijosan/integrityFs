/*
 * Copyright (c) 1998-2011 Erez Zadok
 * Copyright (c) 2009	   Shrikar Archak
 * Copyright (c) 2003-2011 Stony Brook University
 * Copyright (c) 2003-2011 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
 
//*****************************************************************************
// CSE-506 Operating Systems - Spring 2013
// 
// Home Work Assignment 2
//
// Author   - Rishi Josan 
// SBU ID # - 108996773 
// Version  - 1.0
// Date     - 7 April 2013
//
//*****************************************************************************

#ifndef _WRAPFS_H_
#define _WRAPFS_H_

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/seq_file.h>
#include <linux/statfs.h>
#include <linux/fs_stack.h>
#include <linux/magic.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>

/* New Includes*/
#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/errno.h>
#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/xattr.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/cred.h>



/* the file system name */
#define WRAPFS_NAME "wrapfs"

/* wrapfs root inode number */
#define WRAPFS_ROOT_INO     1

/* useful for tracking code reachability */
#define UDBG printk(KERN_DEFAULT "DBG:%s:%s:%d\n", __FILE__, __func__, __LINE__)

/* operations vectors defined in specific files */
extern const struct file_operations wrapfs_main_fops;
extern const struct file_operations wrapfs_dir_fops;
extern const struct inode_operations wrapfs_main_iops;
extern const struct inode_operations wrapfs_dir_iops;
extern const struct inode_operations wrapfs_symlink_iops;
extern const struct super_operations wrapfs_sops;
extern const struct dentry_operations wrapfs_dops;
extern const struct address_space_operations wrapfs_aops, wrapfs_dummy_aops;
extern const struct vm_operations_struct wrapfs_vm_ops;

extern int wrapfs_init_inode_cache(void);
extern void wrapfs_destroy_inode_cache(void);
extern int wrapfs_init_dentry_cache(void);
extern void wrapfs_destroy_dentry_cache(void);
extern int new_dentry_private_data(struct dentry *dentry);
extern void free_dentry_private_data(struct dentry *dentry);
extern struct dentry *wrapfs_lookup(struct inode *dir, struct dentry *dentry,
				    struct nameidata *nd);
extern struct inode *wrapfs_iget(struct super_block *sb,
				 struct inode *lower_inode);
extern int wrapfs_interpose(struct dentry *dentry, struct super_block *sb,
			    struct path *lower_path);
				

/* file private data */
struct wrapfs_file_info {
	struct file *lower_file;
	const struct vm_operations_struct *lower_vm_ops;
};

/* wrapfs inode data in memory */
struct wrapfs_inode_info {
	struct inode *lower_inode;
	struct inode vfs_inode;
	int dirty_bit;  //New bit to track changes in files
	int file_type; //New var to get file type, 0 for Regular File, 1 for Dir, 2 for Sym Link, -1 for Special File
	
	int int_type; //New bit to trach integrity type, for EXTRA CREDIT
	
};


/* wrapfs dentry data in memory */
struct wrapfs_dentry_info {
	spinlock_t lock;	/* protects lower_path */
	struct path lower_path;
};



/* wrapfs super-block data in memory */
struct wrapfs_sb_info {
	struct super_block *lower_sb;
};

/*
 * inode to private data
 *
 * Since we use containers and the struct inode is _inside_ the
 * wrapfs_inode_info structure, WRAPFS_I will always (given a non-NULL
 * inode pointer), return a valid non-NULL pointer.
 */
static inline struct wrapfs_inode_info *WRAPFS_I(const struct inode *inode)
{
	return container_of(inode, struct wrapfs_inode_info, vfs_inode);
}

/* dentry to private data */
#define WRAPFS_D(dent) ((struct wrapfs_dentry_info *)(dent)->d_fsdata)

/* superblock to private data */
#define WRAPFS_SB(super) ((struct wrapfs_sb_info *)(super)->s_fs_info)

/* file to private Data */
#define WRAPFS_F(file) ((struct wrapfs_file_info *)((file)->private_data))

/* file to lower file */
static inline struct file *wrapfs_lower_file(const struct file *f)
{
	return WRAPFS_F(f)->lower_file;
}

static inline void wrapfs_set_lower_file(struct file *f, struct file *val)
{
	WRAPFS_F(f)->lower_file = val;
}

/* inode to lower inode. */
static inline struct inode *wrapfs_lower_inode(const struct inode *i)
{
	return WRAPFS_I(i)->lower_inode;
}

static inline void wrapfs_set_lower_inode(struct inode *i, struct inode *val)
{
	WRAPFS_I(i)->lower_inode = val;
}


/* superblock to lower superblock */
static inline struct super_block *wrapfs_lower_super(
	const struct super_block *sb)
{
	return WRAPFS_SB(sb)->lower_sb;
}

static inline void wrapfs_set_lower_super(struct super_block *sb,
					  struct super_block *val)
{
	WRAPFS_SB(sb)->lower_sb = val;
}

/* path based (dentry/mnt) macros */
static inline void pathcpy(struct path *dst, const struct path *src)
{
	dst->dentry = src->dentry;
	dst->mnt = src->mnt;
}
/* Returns struct path.  Caller must path_put it. */
static inline void wrapfs_get_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&WRAPFS_D(dent)->lock);
	pathcpy(lower_path, &WRAPFS_D(dent)->lower_path);
	path_get(lower_path);
	spin_unlock(&WRAPFS_D(dent)->lock);
	return;
}
static inline void wrapfs_put_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	path_put(lower_path);
	return;
}
static inline void wrapfs_set_lower_path(const struct dentry *dent,
					 struct path *lower_path)
{
	spin_lock(&WRAPFS_D(dent)->lock);
	pathcpy(&WRAPFS_D(dent)->lower_path, lower_path);
	spin_unlock(&WRAPFS_D(dent)->lock);
	return;
}
static inline void wrapfs_reset_lower_path(const struct dentry *dent)
{
	spin_lock(&WRAPFS_D(dent)->lock);
	WRAPFS_D(dent)->lower_path.dentry = NULL;
	WRAPFS_D(dent)->lower_path.mnt = NULL;
	spin_unlock(&WRAPFS_D(dent)->lock);
	return;
}
static inline void wrapfs_put_reset_lower_path(const struct dentry *dent)
{
	struct path lower_path;
	spin_lock(&WRAPFS_D(dent)->lock);
	pathcpy(&lower_path, &WRAPFS_D(dent)->lower_path);
	WRAPFS_D(dent)->lower_path.dentry = NULL;
	WRAPFS_D(dent)->lower_path.mnt = NULL;
	spin_unlock(&WRAPFS_D(dent)->lock);
	path_put(&lower_path);
	return;
}

/* locking helpers */
static inline struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir = dget_parent(dentry);
	mutex_lock_nested(&dir->d_inode->i_mutex, I_MUTEX_PARENT);
	return dir;
}

static inline void unlock_dir(struct dentry *dir)
{
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(dir);
}


/*New Stuff Added*/
//#define EXTRA_CREDIT

extern int wrapfs_setxattr(struct dentry *dentry, const char *name, const void *value, size_t size, int flags);
extern ssize_t wrapfs_getxattr(struct dentry *dentry, const char *name, void *value, size_t size);
extern ssize_t wrapfs_listxattr(struct dentry *dentry, char *list, size_t size);
extern int wrapfs_removexattr(struct dentry *dentry, const char *name);
extern int wrapfs_compute_md5(char * integ_val, struct file *filp, int integrity_type);

#define USR_HAS_INT "user.has_integrity"
#define USR_INT_VAL "user.integrity_val"
#define MD5_SIGNATURE_SIZE 16
#define BLANK_MD5 "d41d8cd98f00b204e9800998ecf8427e"

#ifdef EXTRA_CREDIT

#define SHA1_SIGNATURE_SIZE 20
#define BLANK_SHA1 "da39a3ee5e6b4b0d3255bfef95601890afd80709"

#define USR_INT_TYP "user.integrity_type"
#define MD5_STR "md5"
#define SHA1_STR "sha"

#endif


#ifdef EXTRA_CREDIT

// Routine to get integrity type
static inline int get_int_typ(const struct inode *i)
{
	return WRAPFS_I(i)->int_type;
}

// Routine to set integrity type
static inline void set_int_typ(const struct inode *i)
{
	WRAPFS_I(i)->int_type = 1;
}

// Routine to clear integrity type
static inline void clear_int_typ(const struct inode *i)
{
	WRAPFS_I(i)->int_type = 0;
}

#endif


// Routine to get dirty bit
static inline int get_dirty_bit(const struct inode *i)
{
	return WRAPFS_I(i)->dirty_bit;
}

// Routine to set dirty bit
static inline void set_dirty_bit(const struct inode *i)
{
	WRAPFS_I(i)->dirty_bit = 1;
}

// Routine to clear dirty bit
static inline void clear_dirty_bit(const struct inode *i)
{
	WRAPFS_I(i)->dirty_bit = 0;
}

// Routine to get file type
static inline int get_file_type(const struct inode *i)
{
	return WRAPFS_I(i)->file_type;
}

// Routine to set file type
static inline void set_file_type(const struct inode *i, int val)
{
	WRAPFS_I(i)->file_type = val;
}



#endif	/* not _WRAPFS_H_ */
