Filesystem with Integrity Calculation and Extended Attributes 
====

Homework for course : Operating Systems (Spring 2013)

Authors : Rishi Josan

Files added/modified

* fs/wrapfs/Makefile | Modified to included support for md5.c and xattr.x
* fs/wrapfs/file.c   | Modified
* fs/wrapfs/inode.c  | Modified
* fs/wrapfs/md5.c    | ADDED - Function to compute integrity (Either MD5 or SHA1)
* fs/wrapfs/wrapfs.h | Modified
* fs/wrapfs/xattr.c  | ADDED - Contains wrapfs extended attribute functions - set, get, list, remove
* hw2.config         | MODIFIED from .config - Kernel Config File
* README.md	         | This Readme

(For Extra Credit : Uncomment line 244 in wrapfs.h : #ifdef EXTRA_CREDIT)

1. Compiling and mounting
----

Compile the kernel as usual

    make
    make modules_install install

It is now necessary to mount the partitions and install the module appropriately

Install wrapfs Module

    insmod /usr/src/hw2-rjosan/fs/wrapfs/wrapfs.ko
	
Mount ext3 partition with extended attribute support	

    mount -t ext3 /dev/sdb1 /n/scratch -o user_xattr
	
Mount Wrapfs on top of ext3 with extended attribute support

    mount -t wrapfs /n/scratch /tmp -o user_xattr
	

Wrapfs if now mounted at /tmp


2. Functionality
----

**a. Working with "has_integrity" and "integrity_val"**

Once in the /tmp (wrapfs) directory, the user can use the "attr" and "setfattr" and "getattr" commands to work with extended attributes.

The has_integrity and integrity_value EAs work as follows.

A new file created in the root of the FS will have to EA by default. To set the "has_integrity" EA, execute the following command.

    attr -s has_integrity -V 0 newfile
	
This will set the "has_integrity" value to 0

Setting "has_integrity" value to 1 using the command above will also compute the MD5 checksum and store it in "integrity_val"

To list the attributes, execute the following command

    attr -l newfile
	
To view the value of a particular EA, say "integrity_val", execute:

    attr -g integrity_val newfile
	
To remove the value of an EA, say "has_integrity", execute:

    attr -r has_integrity newfile
	
Removing "has_integrity" or setting it to 0 will remove integrity_val as well


**b. Check for modifications**

Each time a file is opened, the wrapfs_open function in file.c also computes the MD5 for the file and verifies it with the stored integrity.

Whenever a file is written to, its update MD5 is computed in wrapfs_release in file.c and written to the "integrity_val" EA.

A dirty_bit is maintained in struct wrapfs_inode_info. This dirty bit is set whenever a file is written to and cleared and the MD5 is updated.
This dirty bit along with it's set/get/clear methods also ensure that an MD5 match doesn't fail on concurrent access. If dirty, the computed 
md5 is just stored and compared, If not dirty, the md5 is computed and compared. The code is added to file.c in the wrapfs_open function.


**c. "has_integrity" support for directories**

Directories' "has_integrity" values are inherited by the subdirectories and files under them. if 1, the ingetrities are computed and stored
in "integrity_val" EA. The wrapfs_create function store the MD5 checksum corresponding to blank files in the "interity_val" EA. Once these 
files are opened, the existing MD5 is compared. On wrpafs_close, the MD5 checksum is updated if the file was modified.

attr -s has_integrity -V 0 dir



**NOTES: **
1. For efficiency, when a new BLANK file is created under a directory with "has_integrity" = 1, the MD5 in NOT computed,
instead the MD5 value corresponding to a blank file is stored.
2. Printk statements have been kept to a minimum. Check attributes through "attr" command
3. If the parent directory does not have "has_integrity" EA, the children will inherit nothing from the parent.
4. It is preferred that users use "echo" and "cat" to modify and read files. Editors like VI and EMACS use buffers, they create new files
   and copy the contents to the original file. 

   
   
3. EXTRA CREDIT
----

Part A, of the extra credit has been implemented: Support for Multiple checksum algos : MD5 and SHA1

To compile this code, Uncomment the line 244 in wrapfs.h : #ifdef EXTRA_CREDIT.
All EC code has been encapsulated in ifdef statements

If a file has the "integrity_type" EA set to either "md5" or "sha" (NOTE: Enter "sha" NOT "sha1"), the integrity mentioned is computed and 
stored.
Directories with "integrity_type" EA set to either "md5" or "sha" will also be inherited by their subdirectories and files.

Please set "integrity_type" FIRST before setting "has_integrity" . If no "integrity_type" exists, MD5 is computed.
Changing integrity_type later will cause an Integrity mismatch.
If the "integrity_type" EA holds anything other than "sha" OR "md5" , MD5 will be computed, without throwing any error.

Use the following command to set "integrity_type"

attr -s integrity_type -V sha newfile
attr -s integrity_type -V md5 newfile

Setting and getting other EAs will work as described above. 



