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

//Set Extended Attributes
int wrapfs_setxattr(struct dentry *dentry, const char *name,const void *value, size_t size, int flags)
{
	
	int ret, file_type, err = -EOPNOTSUPP, val_size, i, integ_size = MD5_SIGNATURE_SIZE, integrity_type = 0;
	void * temp = NULL;
	char *integ_val = NULL, *md5_digest = NULL, *val_val, *tmp;
	struct file *lower_file = NULL;
	struct dentry *lower_dentry, *parent = NULL;
    struct path lower_path;
	
	
	//If EA = integrity_val, no permission to set
	ret =  strcmp(name ,  (char *) USR_INT_VAL); 
	if (!ret)
	{
		printk("No Permission to write integrigy value!\n");
		return -EACCES;
		
	} 
  
	/*New Stuff*/
	//Get path of lower dentry
    wrapfs_get_lower_path(dentry, &lower_path);
    lower_dentry = lower_path.dentry;
	file_type  = get_file_type(dentry->d_inode);
	/*New Stuff*/

	// Lock Parent Dentry
    parent = lock_parent(lower_dentry);
		
	//kmalloc temp to store has_integrity	
	temp = kmalloc( 2 , GFP_KERNEL );
	if (!temp)
	{
	printk("Couldn't kmalloc temp\n");
	err = -ENOMEM;
	goto out;
	}	 
	memset( temp , 0 , 2 );
	
	//Check for EA = has_integrity
	ret =  strcmp(name ,  (char *) USR_HAS_INT); 
	
	if (!ret)
	{
		//Only root user can modify has_integrity
		if ( current_uid() != 0 )
		{
			printk("Access Denied: Only Root user can modify has_integrity");
			err = -EACCES;
			goto out;
		}
		
		//Check File Type
		/*
		if ( file_type < 0 )
		{
			printk("Integrity can only be set for Regular Files,directories and Sym links\n");
			err = -EACCES;
			goto out;
		}
		*/
		
		
		val_size = (int) size;
		val_val = (char *) value;
		
		
		//Do sanity checks on value of has_integrity
		if (val_size != 1)
		{
			printk("has_integrity value size should be 1\n");
			err = -EPERM;
			goto out;
			
		}
		
		// For has_integrity = 0
		if ( val_val[0] == '0')
		{
			// Check for Directory
			if (!S_ISDIR(dentry->d_inode->i_mode))
			
			{
			
				//Check if has_integrity exists and get it's value
				ret = vfs_getxattr(lower_dentry, (char *) USR_HAS_INT, temp, (size_t) 1 );
			
				tmp = (char *) temp;
			
				if (ret == 1 && tmp[0] == '1')
				{
					ret = vfs_removexattr(lower_dentry, (char *) USR_INT_VAL);
					if (ret<0)
					{
						printk("Error removing integrity_val\n");
						err = ret;
						goto out;
					}
				
				}
			}
			
		}
		else if (val_val[0] == '1')
		{
			if (!S_ISDIR(dentry->d_inode->i_mode))
			
			{
				
				#ifdef EXTRA_CREDIT
				
				char * int_typ;
				int str_md5 = 0;
				int str_sha1 = 0;
				
				int_typ = kmalloc( 4 , GFP_KERNEL );
				if (!int_typ)
				{
				printk("Couldn't kmalloc int_typ\n");
				err = -ENOMEM;
				goto out;	
				}
				memset (int_typ , 0 , 4);
				
				//Get integrity_type EA
				ret = vfs_getxattr(lower_path.dentry, (char *) USR_INT_TYP, (void *) int_typ, (size_t) 3);
			
				str_md5 = strcmp(int_typ, (char *) MD5_STR);
				str_sha1 = strcmp(int_typ, (char *) SHA1_STR);
				
				if (ret == 3 && str_sha1 ==0)
				{
				set_int_typ(dentry->d_inode);
				integ_size = SHA1_SIGNATURE_SIZE;
				integrity_type = 1;
				}
				else 
				{
				clear_int_typ(dentry->d_inode);	
				integ_size = MD5_SIGNATURE_SIZE;
				integrity_type = 0;
				}			
				
				if (int_typ)
				kfree(int_typ);
				#endif
			   
				// Initializing integ_val to store MD5 values
				integ_val = kmalloc( integ_size + 1 , GFP_KERNEL );
				if (!integ_val)
				{
					printk("Couldn't kmalloc integ_val\n");
					err = -ENOMEM;
					goto out;
				} 
				memset( integ_val , 0 , integ_size + 1 );
				
				//Initializing md5_digest
				md5_digest = kmalloc( 2*integ_size + 1 , GFP_KERNEL );
				if (!md5_digest)
				{
					printk("Couldn't kmalloc md5_digest\n");
					err = -ENOMEM;
					goto out;
				} 
				memset( md5_digest , 0 , 2*integ_size + 1 );
			

				// Open File
				lower_file = dentry_open(lower_path.dentry, lower_path.mnt, flags, current_cred());
				if (IS_ERR(lower_file))
				{
					err = PTR_ERR(lower_file);
				}
						
				// Compute Integrity Value
				ret = wrapfs_compute_md5(integ_val,lower_file, integrity_type);
				if (ret<0)
				{
					printk("Error Computing Integrity\n");
					err = ret;
				goto out;
				}
			

				// Convert Integrity value to ASCII
				for(i=0 ; i<integ_size ; i++)
				{	
					sprintf( (md5_digest + 2*i), "%02x" , integ_val[i]&0xff);
				
				}

				// Set integrity_val
				ret = vfs_setxattr(lower_dentry, (char *) USR_INT_VAL, (void *) md5_digest, (ssize_t) 2*integ_size, flags);
				
				if (ret<0)
				{
					printk("Error Setting Integrity Value\n");
					err = ret;
					goto out;
				}
						
			}			
			
		}
		else
		{
			printk("has_integrity value should be 0 or 1\n");
			goto out;
		}
		
	}

	// Set EA
    err = vfs_setxattr(lower_dentry, (char *) name, (void *) value,	size, flags);
			
		if (err<0)
		{
			printk("Error Setting Has Integrity \n");
			goto out;
		}
    
	//Cleanup Routine for setxattr
    out:

    unlock_dir(parent);
	//wrapfs_put_lower_path(dentry, &lower_path);
	
	if(temp)
	kfree(temp);
	
	if(integ_val)
	kfree(integ_val);
	
	if(md5_digest)
	kfree(md5_digest);
	
	if(lower_file)
	fput(lower_file);   

	return err;
	
}



ssize_t wrapfs_getxattr(struct dentry *dentry, const char *name, void *value, size_t size)
{
	struct dentry *lower_dentry;
	struct dentry *parent= NULL;
    int err = -EOPNOTSUPP;
	
    /*New Stuff*/
	struct path lower_path;
    wrapfs_get_lower_path(dentry, &lower_path);
    lower_dentry = lower_path.dentry;
	/*New Stuff*/
	
	// Get and Lock parent Dentry
    parent = lock_parent(lower_dentry);

	// Get EAs
	err = vfs_getxattr(lower_dentry, (char *) name, value, size);
	if (err<0)
	{
		printk("Error getting EA\n");
	}
	
	//Cleanup Routine

	unlock_dir(parent);
	wrapfs_put_lower_path(dentry, &lower_path);
	return err;
}



ssize_t wrapfs_listxattr(struct dentry *dentry, char *list, size_t size)
{
	struct dentry *lower_dentry = NULL;
	struct dentry *parent;
	int err = -EOPNOTSUPP;
	char *encoded_list = NULL;
	struct path lower_path;
	
	/*New Stuff*/
    wrapfs_get_lower_path(dentry, &lower_path);
    lower_dentry = lower_path.dentry;
	/*New Stuff*/

	// Get and Lock parent Dentry
	parent = lock_parent(lower_dentry);

	encoded_list = list;
	
	// Get EA list
	err = vfs_listxattr(lower_dentry, encoded_list, size);
	if (err<0)
	{
		printk("Error listing EAs\n");
	}
	//Cleanup Routine

	unlock_dir(parent);
	//wrapfs_put_lower_path(dentry, &lower_path);
	return err;
}



int wrapfs_removexattr(struct dentry *dentry, const char *name)
{

	struct dentry *lower_dentry = NULL;
	struct dentry *parent;
	int err = 0;
	int ret;
	void *temp = NULL;
	struct path lower_path;
	char *tmp;
	
	// No permission to remove integrity value
	ret =  strcmp(name ,  (char *) USR_INT_VAL); 
	if (!ret)
	{
		printk("No Permission to remove integrigy value!\n");
		return -EACCES;	
	}

	/*New Stuff*/
    wrapfs_get_lower_path(dentry, &lower_path);
    lower_dentry = lower_path.dentry;

	// Get and Lock parent Dentry
	parent = lock_parent(lower_dentry);
	
	// Kmallocing temporary Var
	temp = kmalloc( 2 , GFP_KERNEL );
	if (!temp)
	{
	printk("Couldn't kmalloc temp\n");
	err = -ENOMEM;
	goto out2;
	} 
	memset( temp , 0 , 2 );

	// Check for has_integrity
	ret =  strcmp(name ,  (char *) USR_HAS_INT); 
	if (!ret)
	{
		if ( current_uid() != 0 )
		{
			printk("Access Denied: Only Root user can remove has_integrity");
			err = -EACCES;
			goto out2;
		}
		
		else
		{
			//  Get has_integrity
			ret = vfs_getxattr(lower_dentry, (char *) USR_HAS_INT, temp, (size_t) 1 );
			
			tmp = (char *) temp;
			
			// Check for has_integrity = 1
			if (ret == 1 && tmp[0] == '1')
			{			
				//Have to remove integrity_val as well
				ret = vfs_removexattr(lower_dentry, (char *) USR_INT_VAL);
				if (ret<0)
				{
					err = ret;
					printk("Error removing integrity_val\n");
					goto out2;
				}
			}
			
		}
	}
	
	// Remove EA
	err = vfs_removexattr(lower_dentry, name);
	if (err<0)
	printk("Error Removing EA\n");
	
//Cleanup Routine
out2:

	unlock_dir(parent);
	//wrapfs_put_lower_path(dentry, &lower_path);
	
	if(temp)
	kfree(temp);

	return err;
}









