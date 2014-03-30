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
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/scatterlist.h>


// This function reads a file in chunks of size PAGE_SIZE and incrementally computes its MD5 value
// Adapted from HW1 solution

int wrapfs_compute_md5(char * integ_val, struct file *filp, int integrity_type)
  {
    
	void *buf; 
	int bufLen = MD5_SIGNATURE_SIZE, ret = 0, file_size, err = 0;
	
	
	#ifdef EXTRA_CREDIT
		
	if (integrity_type == 1)	
	bufLen = SHA1_SIGNATURE_SIZE;
				
	#endif
	
    mm_segment_t oldfs;
    int bytes = 0, offset =0;
    struct crypto_hash *crypt_hash;
    struct hash_desc desc;
    struct scatterlist sg;
	
	
	//Initializing read buffer (Contents of file read in PAGE_SIZE chunks here)  
	buf = kmalloc(bufLen , GFP_KERNEL);
	if (!buf)
	{
	printk("sys_xintegrity: Couldn't kmalloc buf\n");
	kfree(buf);
	return -ENOMEM;
	}
	memset(buf , 0 ,bufLen );

	
  // Setting crypto hash to MD5 for computation of MD5
  #ifdef EXTRA_CREDIT
	if (integrity_type == 1)
	{
	crypt_hash = crypto_alloc_hash("sha1" , 0 , CRYPTO_ALG_ASYNC);
	}
	else 
	{
	crypt_hash = crypto_alloc_hash("md5" , 0 , CRYPTO_ALG_ASYNC);
	}
  #else 
	crypt_hash = crypto_alloc_hash("md5" , 0 , CRYPTO_ALG_ASYNC);
  #endif
  
  if(IS_ERR(crypt_hash))
    {
      printk("Unable to allocate struct crypto_hash");
      err =  -EPERM;
	  goto out_md5;
    }


  desc.tfm = crypt_hash;
  desc.flags = 0;

  
 // Initialization for crypto API 
 if ( crypto_hash_init(&desc) < 0) 
     {
       printk("Crypto_hash_init() failed\n");
       crypto_free_hash(crypt_hash);
       err =   -EPERM;
	    goto out_md5;
     }

 

    if (!filp->f_op->read)
      {
		err =  -2;
		goto out_md5;
		}
	  
    //Calculating file size
    file_size = i_size_read(filp->f_dentry->d_inode);

    oldfs = get_fs();
    set_fs(KERNEL_DS);

	
    //Sequentially reading file and computing MD5 in PAGE_SIZE chunks
    while(offset <= file_size)

    {

    filp->f_pos = offset;
   
   
    //Reading file
	bytes = filp->f_op->read(filp, buf, bufLen, &filp->f_pos);
	
	if (bytes < 0)
	{
		//printk("Bytes<0\n");
		err = -ENODATA;
		goto out_md5;
	}
	
	//printk("Bytes: %d\n" , bytes);
   
    if (bytes ==0)
      {
      break;
      }
   
   
    //Passing read buffer to crypto API
    sg_init_one(&sg , buf , bytes);

	
    //Updating the MD5 Hash value
    ret = crypto_hash_update(&desc, &sg, bytes);
    if (ret < 0)
       {
	 printk("Crypto_hash_update() failed for\n");
	 crypto_free_hash(crypt_hash);
	 err =  -EPERM;
	  goto out_md5;
       }
  
  
    //Increasing offset by no. of bytes read
    offset = offset + bytes;
    }

    set_fs(oldfs);

    ret = crypto_hash_final(&desc, integ_val );
    if (ret < 0) 
       {
	 printk("Crypto_hash_final() failed\n");
	 crypto_free_hash(crypt_hash);
	 err =  -EPERM;
	  goto out_md5;
       }

out_md5:

		if (err < 0)
		return err;
		else
		return bytes;
	
  }