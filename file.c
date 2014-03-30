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

#include "wrapfs.h"

static ssize_t wrapfs_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	int err;
	struct file *lower_file;
	struct dentry *dentry = file->f_path.dentry;

	lower_file = wrapfs_lower_file(file);
	err = vfs_read(lower_file, buf, count, ppos);
	/* update our inode atime upon a successful lower read */
	if (err >= 0)
		fsstack_copy_attr_atime(dentry->d_inode,
					lower_file->f_path.dentry->d_inode);

	return err;
}

static ssize_t wrapfs_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	int err = 0;
	struct file *lower_file;
	struct dentry *dentry = file->f_path.dentry;
	struct inode *w_inode = dentry->d_inode; // New 

	lower_file = wrapfs_lower_file(file);
	err = vfs_write(lower_file, buf, count, ppos);
	/* update our inode times+sizes upon a successful lower write */
	if (err >= 0) {
		fsstack_copy_inode_size(dentry->d_inode,
					lower_file->f_path.dentry->d_inode);
		fsstack_copy_attr_times(dentry->d_inode,
					lower_file->f_path.dentry->d_inode);
					
		set_dirty_bit(w_inode); //New
	}

	return err;
}

static int wrapfs_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	int err = 0;
	struct file *lower_file = NULL;
	struct dentry *dentry = file->f_path.dentry;

	lower_file = wrapfs_lower_file(file);
	err = vfs_readdir(lower_file, filldir, dirent);
	file->f_pos = lower_file->f_pos;
	if (err >= 0)		/* copy the atime */
		fsstack_copy_attr_atime(dentry->d_inode,
					lower_file->f_path.dentry->d_inode);
	return err;
}

static long wrapfs_unlocked_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	long err = -ENOTTY;
	struct file *lower_file;

	lower_file = wrapfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op)
		goto out;
	if (lower_file->f_op->unlocked_ioctl)
		err = lower_file->f_op->unlocked_ioctl(lower_file, cmd, arg);

out:
	return err;
}

#ifdef CONFIG_COMPAT
static long wrapfs_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	long err = -ENOTTY;
	struct file *lower_file;

	lower_file = wrapfs_lower_file(file);

	/* XXX: use vfs_ioctl if/when VFS exports it */
	if (!lower_file || !lower_file->f_op)
		goto out;
	if (lower_file->f_op->compat_ioctl)
		err = lower_file->f_op->compat_ioctl(lower_file, cmd, arg);

out:
	return err;
}
#endif

static int wrapfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	int err = 0;
	bool willwrite;
	struct file *lower_file;
	const struct vm_operations_struct *saved_vm_ops = NULL;

	/* this might be deferred to mmap's writepage */
	willwrite = ((vma->vm_flags | VM_SHARED | VM_WRITE) == vma->vm_flags);

	/*
	 * File systems which do not implement ->writepage may use
	 * generic_file_readonly_mmap as their ->mmap op.  If you call
	 * generic_file_readonly_mmap with VM_WRITE, you'd get an -EINVAL.
	 * But we cannot call the lower ->mmap op, so we can't tell that
	 * writeable mappings won't work.  Therefore, our only choice is to
	 * check if the lower file system supports the ->writepage, and if
	 * not, return EINVAL (the same error that
	 * generic_file_readonly_mmap returns in that case).
	 */
	lower_file = wrapfs_lower_file(file);
	if (willwrite && !lower_file->f_mapping->a_ops->writepage) {
		err = -EINVAL;
		printk(KERN_ERR "wrapfs: lower file system does not "
		       "support writeable mmap\n");
		goto out;
	}

	/*
	 * find and save lower vm_ops.
	 *
	 * XXX: the VFS should have a cleaner way of finding the lower vm_ops
	 */
	if (!WRAPFS_F(file)->lower_vm_ops) {
		err = lower_file->f_op->mmap(lower_file, vma);
		if (err) {
			printk(KERN_ERR "wrapfs: lower mmap failed %d\n", err);
			goto out;
		}
		saved_vm_ops = vma->vm_ops; /* save: came from lower ->mmap */
		err = do_munmap(current->mm, vma->vm_start,
				vma->vm_end - vma->vm_start);
		if (err) {
			printk(KERN_ERR "wrapfs: do_munmap failed %d\n", err);
			goto out;
		}
	}

	/*
	 * Next 3 lines are all I need from generic_file_mmap.  I definitely
	 * don't want its test for ->readpage which returns -ENOEXEC.
	 */
	file_accessed(file);
	vma->vm_ops = &wrapfs_vm_ops;
	vma->vm_flags |= VM_CAN_NONLINEAR;

	file->f_mapping->a_ops = &wrapfs_aops; /* set our aops */
	if (!WRAPFS_F(file)->lower_vm_ops) /* save for our ->fault */
		WRAPFS_F(file)->lower_vm_ops = saved_vm_ops;

out:
	return err;
}

static int wrapfs_open(struct inode *inode, struct file *file)
{
	int err = 0, ret, i =0, test, mis=0;
	struct file *lower_file = NULL;
	struct path lower_path;
	struct dentry *parent = NULL, *lower_dentry;  
	char *stor_val = NULL, *comp_val = NULL, *md5_val = NULL, *md5_digest = NULL, *val = NULL;
	int integ_size = MD5_SIGNATURE_SIZE, integrity_type = 0;
	
	// Kmalloc val to store value of has_integrity	
	val = kmalloc( 1 , GFP_KERNEL );
	if (!val)
	{
	printk("Couldn't kmalloc val\n");
	err = -ENOMEM;
	goto out_err;
	}
	memset (val , 0 , 1);
	
	// Initialize dirty_bit when file is first opened
	if (get_dirty_bit(inode) != 1)
	clear_dirty_bit(inode);	
	
	// Store file type in test
	test = get_file_type(inode);
	
	// Get File type
	if (S_ISREG(inode->i_mode))
	{
		//printk("Regular File\n");
		set_file_type(inode, 0);

	}
	else if(S_ISDIR(inode->i_mode))
	{
		//printk("Directory\n");
		set_file_type(inode, 1);
	}
	else if(S_ISLNK(inode->i_mode))
	{
		//printk("Sym Link\n");
		set_file_type(inode, 2);
	}
	else
	{
		//printk("Special File\n");
		set_file_type(inode, -1);
	}
			

	

	/* don't open unhashed/deleted files */
	if (d_unhashed(file->f_path.dentry)) 
	{
		err = -ENOENT;
		goto out_err;
	}

	file->private_data =
		kzalloc(sizeof(struct wrapfs_file_info), GFP_KERNEL);
	if (!WRAPFS_F(file))
 {
		err = -ENOMEM;
		goto out_err;
	}

	/* open lower object and link wrapfs's file struct to lower's */
	wrapfs_get_lower_path(file->f_path.dentry, &lower_path);
	
	lower_dentry = lower_path.dentry; //New 
	
	// Open Lower File
	lower_file = dentry_open(lower_path.dentry, lower_path.mnt,
				 file->f_flags, current_cred());
				 
	// Check for err and perform cleanup
	if (IS_ERR(lower_file))
	{
	
		err = PTR_ERR(lower_file);
		lower_file = wrapfs_lower_file(file);
		if (lower_file)
		{
			wrapfs_set_lower_file(file, NULL);
			fput(lower_file); /* fput calls dput for lower_dentry */
		}
	}
	else
	{
		wrapfs_set_lower_file(file, lower_file);
	}

	if (err)
		{
		kfree(WRAPFS_F(file));
		}
	else
		{
		
			/* New code*/
			parent = lock_parent(lower_dentry);
			
			// Get has_integrity value
			ret = vfs_getxattr(lower_path.dentry, (char *) USR_HAS_INT, (void *) val, (size_t) 1);
			if (ret ==1 && val[0] == '1')
			{			

			
			#ifdef EXTRA_CREDIT
			
			char * int_typ;
			int str_md5 = 0;
			int str_sha1 = 0;
			
			int_typ = kmalloc( 3 , GFP_KERNEL );
			if (!int_typ)
			{
			printk("Couldn't kmalloc int_typ\n");
			err = -ENOMEM;
			goto out_err;
			}
			memset (int_typ , 0 , 4);
			
			
	
			ret = vfs_getxattr(lower_path.dentry, (char *) USR_INT_TYP, (void *) int_typ, (size_t) 3);
			
			str_md5 = strcmp(int_typ, (char *) MD5_STR);
			str_sha1 = strcmp(int_typ, (char *) SHA1_STR);
			
			if (ret == 3 && str_sha1 ==0)
			{
				set_int_typ(inode);
				integ_size = SHA1_SIGNATURE_SIZE;
				integrity_type = 1;		
			}
			else 
			{
				clear_int_typ(inode);	
				integ_size = MD5_SIGNATURE_SIZE;
				integrity_type = 0;	
			}
	
			if(int_typ)
			kfree(int_typ);
			
			#endif
			
			
			
			
			// Kmalloc store_val to store MD5
			stor_val = kmalloc( 2*integ_size + 1 , GFP_KERNEL );
			if (!stor_val)
			{
			printk("Couldn't kmalloc stor_val\n");
			err = -ENOMEM;
			goto out_err;
			}
			memset (stor_val , 0 , 2*integ_size + 1);
			
			// Kmalloc md5_val to store MD5
			md5_val = kmalloc( 2*integ_size + 1 , GFP_KERNEL );
			if (!md5_val)
			{
			printk("Couldn't kmalloc md5_val\n");
			err = -ENOMEM;
			goto out_err;
			}
			memset (md5_val , 0 , 2*integ_size + 1);
			
			// Kmalloc comp_val to store MD5
			comp_val = kmalloc( integ_size + 1 , GFP_KERNEL );
			if (!comp_val)
			{
			printk("Couldn't kmalloc comp_val\n");
			err = -ENOMEM;
			goto out_err;
			}
			memset (comp_val , 0 , integ_size + 1);
			
			// Kmalloc md5_digest to store MD5
			md5_digest = kmalloc( 2*integ_size + 1 , GFP_KERNEL );
			if (!md5_digest)
			{
			printk("Couldn't kmalloc md5_digest\n");
			err = -ENOMEM;
			goto out_err;
			} 
			memset( md5_digest , 0 , 2*integ_size + 1 );
	

			//Getting stored integrity value
			ret = vfs_getxattr(lower_path.dentry, (char *) USR_INT_VAL, (void *) stor_val, (size_t) 2*integ_size);
			
			
			if (ret == 2*integ_size )
			{
		
			memcpy(md5_val, stor_val, 2*integ_size);
			
			//Compute updated md5
			ret = wrapfs_compute_md5(comp_val,lower_file, integrity_type);

			if (ret < 0)
			printk("Error Computing MD5\n");
			if (ret >= 0)
			{
				//Copy MD5 value to md5_digest and convert to ASCII
				for(i=0 ; i<integ_size ; i++)
				{			
					sprintf( (md5_digest + 2*i), "%02x" , comp_val[i]&0xff);				
				}
						
				// If dirty, store computed md5 -  Do not match
				// If not dirty, compare stored and computed  
				if ( get_dirty_bit(inode) == 0)
				{
				
				ret = memcmp(md5_digest, md5_val , 2*integ_size);
							
				
				if (ret != 0)
				{
					printk("The stored and computed integrity values don't match!\n");
					mis = 1;
					err = -EPERM;
					goto out_err;
				}
				
				}
				
				else if ( get_dirty_bit(inode) == 1)
				
				{
				
				// Set integrity value
				err = vfs_setxattr(lower_path.dentry, (char *) USR_INT_VAL, (void *) md5_digest, (ssize_t) 2*integ_size, XATTR_REPLACE);
				
				}
			}
			
			}
				else 
				{
				
				if (!S_ISDIR(inode->i_mode))
				{
				//Integrity Value not Found/Incorrect
				printk("Integrity Value Tampered!\n");
				mis = 1;
				err = -EPERM;
				goto out_err;
				}
				}
			
			}
			else if (ret ==1 && val[0] == '0')
			{			
			//printk("Has Integrity : \n");
			//printk("%c\n" ,val[0]);								
			}
			 
		fsstack_copy_attr_all(inode, wrapfs_lower_inode(inode));
		
		}
		
//Cleanup routine		
out_err:

	if (mis)
	{
		lower_file = wrapfs_lower_file(file);
		if (lower_file)
		{
			wrapfs_set_lower_file(file, NULL);
			fput(lower_file); /* fput calls dput for lower_dentry */
		}
		
	kfree(WRAPFS_F(file));	
	}
	
	if(val)
	kfree(val);
	
	if(stor_val)
	kfree(stor_val);
	
	if(comp_val)
	kfree(comp_val);
	
	if(md5_val)
	kfree(md5_val);
	
	if(md5_digest)
	kfree(md5_digest);
	
	if(parent)
	unlock_dir(parent);
	
	return err;
	
}

static int wrapfs_flush(struct file *file, fl_owner_t id)
{
	int err = 0;
	struct file *lower_file = NULL;

	lower_file = wrapfs_lower_file(file);
	if (lower_file && lower_file->f_op && lower_file->f_op->flush)
		err = lower_file->f_op->flush(lower_file, id);

	return err;
}

/* release all lower object references & free the file info structure */
static int wrapfs_file_release(struct inode *inode, struct file *file)
{
	/*New Code*/
	int ret, err =0, i, is_dirty;
	struct file *lower_file;
	struct path lower_path;
	char *val = NULL, *md5_digest = NULL, *comp_val = NULL; 
	struct dentry *parent = NULL;

	int integ_size = MD5_SIGNATURE_SIZE, integrity_type = 0;
	
	#ifdef EXTRA_CREDIT
				
	if (get_int_typ(inode) == 1)
	{
	integ_size = SHA1_SIGNATURE_SIZE;
	integrity_type = 1;
	}
	
	#endif
	
	/* open lower object and link wrapfs's file struct to lower's */
	wrapfs_get_lower_path(file->f_path.dentry, &lower_path);
	lower_file = wrapfs_lower_file(file);

	is_dirty = get_dirty_bit(inode);
	
	// Kmalloc val to store has_integrity
	val = kmalloc( 1 , GFP_KERNEL );
	if (!val)
	{
	printk("Couldn't kmalloc val\n");
	err = -ENOMEM;
	goto out;
	}
	memset (val , 0 , 1);

	
	// Kmalloc comp_val to store MD5 value
	comp_val = kmalloc( integ_size + 1 , GFP_KERNEL );
	if (!comp_val)
	{
	printk("Couldn't kmalloc comp_val\n");
	err = -ENOMEM;
	goto out;
	}
	memset (comp_val , 0 , integ_size + 1);
	
		
	// Kmalloc MD5 digest to stor MD5 value
	md5_digest = kmalloc( 2*integ_size + 1 , GFP_KERNEL );
	if (!md5_digest)
	{
	printk("Couldn't kmalloc md5_digest\n");
	err = -ENOMEM;
	goto out;
	} 
	memset( md5_digest , 0 , 2*integ_size + 1 );
	
	// Get and lock parent dentry
	parent = lock_parent(lower_path.dentry);
	
	// Get has_integrity value
	ret = vfs_getxattr(lower_path.dentry, (char *) USR_HAS_INT, (void *) val, (size_t) 1);
	
	if (ret == 1 && val[0] == '1' && is_dirty == 1)
	{
		//Integrity is 1, Compute MD5 and update
		
		ret = wrapfs_compute_md5(comp_val,lower_file, integrity_type);
			
			if (ret < 0)
			printk("Error Computing MD5 in release\n");
			if (ret >= 0)
			{
				// Store MD5 value in md5_digest in ASCII
				for(i=0 ; i<integ_size ; i++)
				{			
					sprintf( (md5_digest + 2*i), "%02x" , comp_val[i]&0xff);				
				}
		
				// Set integrity Value
				err = vfs_setxattr(lower_path.dentry, (char *) USR_INT_VAL, (void *) md5_digest, (ssize_t) 2*integ_size, XATTR_REPLACE);
				
				clear_dirty_bit(inode);
			}
	}
	
//Cleanup Routine
out: 
	
	wrapfs_put_lower_path(file->f_path.dentry, &lower_path);
	if (lower_file) {
		wrapfs_set_lower_file(file, NULL);
		fput(lower_file);
	}

	kfree(WRAPFS_F(file));
	
	if(val)
	kfree(val);
	
	if(comp_val)
	kfree(comp_val);
	
	if(md5_digest)
	kfree(md5_digest);
	
	if(parent)
	unlock_dir(parent);
	return 0;
}

static int wrapfs_fsync(struct file *file, loff_t start, loff_t end,
			int datasync)
{
	int err;
	struct file *lower_file;
	struct path lower_path;
	struct dentry *dentry = file->f_path.dentry;

	err = generic_file_fsync(file, start, end, datasync);
	if (err)
		goto out;
	lower_file = wrapfs_lower_file(file);
	wrapfs_get_lower_path(dentry, &lower_path);
	err = vfs_fsync_range(lower_file, start, end, datasync);
	wrapfs_put_lower_path(dentry, &lower_path);
out:
	return err;
}

static int wrapfs_fasync(int fd, struct file *file, int flag)
{
	int err = 0;
	struct file *lower_file = NULL;

	lower_file = wrapfs_lower_file(file);
	if (lower_file->f_op && lower_file->f_op->fasync)
		err = lower_file->f_op->fasync(fd, lower_file, flag);

	return err;
}

const struct file_operations wrapfs_main_fops = {
	.llseek		= generic_file_llseek,
	.read		= wrapfs_read,
	.write		= wrapfs_write,
	.unlocked_ioctl	= wrapfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= wrapfs_compat_ioctl,
#endif
	.mmap		= wrapfs_mmap,
	.open		= wrapfs_open,
	.flush		= wrapfs_flush,
	.release	= wrapfs_file_release,
	.fsync		= wrapfs_fsync,
	.fasync		= wrapfs_fasync,
};

/* trimmed directory options */
const struct file_operations wrapfs_dir_fops = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.readdir	= wrapfs_readdir,
	.unlocked_ioctl	= wrapfs_unlocked_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= wrapfs_compat_ioctl,
#endif
	.open		= wrapfs_open,
	.release	= wrapfs_file_release,
	.flush		= wrapfs_flush,
	.fsync		= wrapfs_fsync,
	.fasync		= wrapfs_fasync,
};
